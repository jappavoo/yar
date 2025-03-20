#include "yar.h"
#include <sys/syscall.h>
#include <sys/pidfd.h>
#include <pty/pty_fork.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <tty/tty_functions.h>

// Macros to help with circular cmdbuffer management --
//   This likely could be improved but wrote for comprehension not performance
// Translate total a byte stream index into a fixed buffer sized offset
#define cmdbufSize         ( sizeof(((cmd_t *)0)->buf) ) 
#define cmdbufNtoI(n)      ( n %  cmdbufSize )
#define cmdbufWrapped(n)   ( n >= cmdbufSize )
// Length of active data in buffer accouting for wrapping 
#define cmdbufDataLen(n)   ( cmdbufWrapped(n) ? cmdbufSize : n )
// Location of first character of data accounting for buffer wrapping
#define cmdbufDataStart(n) ( cmdbufWrapped(n) ? cmdbufNtoI(n) : 0 )
// location of last character OF accounting for buffer wrapping
#define cmdbufDataEnd(n)   ( cmdbufWrapped(n) ? cmdbufNtoI(n)-1 : n-1 )


extern bool
cmdInit(cmd_t *this, char *name, char *cmdline, double delay, char *ttylink,
	char *log)
{
  assert(name && cmdline); 
  this->name       = name;
  this->cmdline    = cmdline;
  this->delay      = delay;
  this->log        = log;
  this->n          = 0;
  this->pid        = -1;
  this->pidfd      = -1;
  this->exitstatus = -1;
  this->retry      = true;
  ttyInit(&(this->cmdtty), NULL);
  ttyInit(&(this->clttty), ttylink);
  return true;
}

extern bool
cmdReset(cmd_t *this)
{
  // add code here
  return false;
}

extern void
cmdDump(cmd_t *this, FILE *f, char *prefix)
{
  assert(this);
  int i=cmdbufNtoI(this->n);
  int lastchar=this->buf[i];
  fprintf(f, "%scmd: this=0x%p pid=%ld pidfd=%d name=%s exitstatus=%d\n"
	  "    cmdline=\"%s\" \n    delay=%f log=%s n=%lu"
	  " lastchar:buf[%d]=02x(%c)\n", prefix, this,
	  (long)this->pid, this->pidfd, this->name, this->exitstatus,
	  this->cmdline, this->delay, this->log, this->n,i,lastchar);
  if (this->n && verbose(3)) {
    if (!cmdbufWrapped(this->n)) {
      hexdump(f, this->buf, this->n);
    } else {
      // JA FIXME: this could probably be made to work for both cases but I 
      // don't want to think about it 
      size_t start = cmdbufDataStart(this->n);
      char n = cmdbufSize - start; 
      hexdump(f, &(this->buf[start]),  n);
      hexdump(f, &(this->buf[0]), cmdbufSize - n);
    }
  }
  ttyDump(&(this->cmdtty), f, "    cmdtty: ");
  ttyDump(&(this->clttty), f, "    clttty: ");
}

static int
cmdttyBufIn(cmd_t *this)
{
  char c;
  int n = ttyReadChar(&(this->cmdtty), &c);
  if (n==0) {
    VLPRINT(2, "%p: read returned 0\n", this);
    NYI;
  } else if (n==1)  {
    VLPRINT(2, "cmdttyBufIn:%s(%p):%02x(%c)\n", this->name,
	    this, c, c);
    int i        = cmdbufNtoI(this->n);    // account for circular buffer
    this->buf[i] = c;                      // store character in buffer
    this->n++;                             // inc n -- bytes since last flush        
    n = ttyWriteChar(&(this->clttty), c);  // send data to our client tty
    if (n != 1) NYI;
    n = ttyWriteChar(&GBLS.btty, c);       // send data to broadcast tty
    if (n != 1) NYI;
    if (verbose(3)) cmdDump(this, stderr, "DATA-IN:");
  } else {
    EPRINT("read returned: %d\n", n);
    perror("read");
    NYI;
  }
  return n;
}

void
cmdttyDrain(cmd_t *this)
{
  // drain any remaining data???
  while (cmdttyBufIn(this)>0) {
    VLPRINT(2, "%p: got data after HUP", this->name);
  }
}
  
// EVENT HANDLERS

// pidfd event -- should only be POLLIN indicating death of the command process
static evnthdlrrc_t
cmdPidEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t *this = obj;
  int      fd = this->pidfd;
  if (verbose(2)) {
    cmdDump(this, stderr, "pidEvent on:\n  ");
  }
  if (evnts & EPOLLIN) {
    VLPRINT(2, "EPOLLIN(%x)\n", EPOLLIN);
    cmdttyDrain(this);
    // remove from event set
    {
      // for backwards compatiblity we use dummy versus NULL see bugs section
      // of man epoll_ctl
      struct epoll_event dummyev;
      if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &dummyev) == -1) {
	perror("epoll_ctl: EPOLL_CTL_DEL fd");
	assert(0);
      }
      close(fd);
      this->pidfd = -1;
    }
    cmdDump(this, stderr, "***Command Died:\n  ");
    cmdReset(this);
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    VLPRINT(2, "EPOLLHUP(%x)\n", EPOLLHUP);
    assert(0);
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2, "EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    assert(0);
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2, "EPOLLERR(%x)\n", EPOLLERR);
    assert(0);
  }
  if (evnts != 0) {
    VLPRINT(2, "unknown events evnts:%x", evnts);
    assert(0);
  }
 done:
  return EVNT_HDLR_SUCCESS;
}

// cmdtty event -- master side of command pty used to read and write data
//                 to and from the command process and detect errors
static evnthdlrrc_t
cmdttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t *this = obj;
  int fd = this->cmdtty.mfd;
  VLPRINT(2,"Events:0x%08x on fd:%d\n", evnts, fd);
  if (evnts & EPOLLIN) {
    VLPRINT(2, "EPOLLIN(%x)\n", EPOLLIN);
    cmdttyBufIn(this);
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    // now that we are keeping the slave open when we create the
    // master I don't thnk we should see this
    VLPRINT(2, "EPOLLHUP(%x)\n", EPOLLHUP);
    assert(0);
    cmdttyDrain(this);
    {
      // for backwards compatiblity we use dummy versus NULL see bugs section
      // of man epoll_ctl
      struct epoll_event dummyev;
      if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &dummyev) == -1) {
	perror("epoll_ctl: EPOLL_CTL_DEL fd");
	assert(0);
      }
      close(fd);
      cmdDump(this, stderr, "HUP on cmdtty:"); 
    }
    evnts = evnts & ~EPOLLHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2, "EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    evnts = evnts & ~EPOLLRDHUP;
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2, "EPOLLERR(%x)\n", EPOLLERR);
    evnts = evnts & ~EPOLLERR;
  }
  if (evnts != 0) {
    VLPRINT(2, "unknown events evnts:%x", evnts);
  }
 done:
  return EVNT_HDLR_SUCCESS;
}

// clttty event -- master side of client pty used by clients to read and
//                 write data to and from the command 
static evnthdlrrc_t
cltttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t *this = obj;
  tty_t *tty  = &(this->clttty);
  int    fd   = tty->mfd;
  VLPRINT(2,"Events:0x%08x on cmd:%s(0x%p) clttty:%s(%s) fd:%d\n", evnts,
	  this->name, this, tty->link, tty->path, fd);
  if (evnts & EPOLLIN) {
    VLPRINT(2,"EPOLLIN(%x)\n", EPOLLIN);
    char c;
    int n = ttyReadChar(&this->clttty, &c);
    if (verbose(2)) {
      if (n) fprintf(stderr, "  got: %c(%02x)\n", c, c);
      else {
	fprintf(stderr,   "  read returned: n=%d\n", n);
	NYI;
      }
    }
    n=cmdWriteChar(this,c);
    if (n!=1) {
      fprintf(stderr,   "  write returned: n=%d\n", n);
      NYI;
    }
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    VLPRINT(2,"EPOLLHUP(%x)\n", EPOLLHUP);
    evnts = evnts & ~EPOLLHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2,"EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2,"EPOLLERR(%x)\n", EPOLLERR);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts != 0) {
    VLPRINT(2,"unknown events evnts:%x", evnts);
  }
 done:
  return EVNT_HDLR_SUCCESS;
}

extern bool
cmdCreate(cmd_t *this, bool raw)
{
  pid_t cpid; 
  assert(this);
  assert(this->cmdline);
  // We use pidfd to track exits of the command processes.  Right now
  // we are using fork to create these processes, however we might
  // need to switch to using clone2 so that we can more precisely meet
  // the requirements for using pidfd's.  See man pidfd_open for
  // the restrictions.

  // First try and create a client tty for this command so that we
  // can fail early, before we try and create the command process
  {
    if (!ttyCreate(&this->clttty, true)) return false;
  }

  // Create new child process with a new pty connecting this process (the parent)
  // to it -- from tlpi library
  {
    cpid = ptyFork(&(this->cmdtty.mfd),
		   this->cmdtty.path, sizeof(this->cmdtty.path),
		   NULL, NULL);
    if (cpid == -1) {
      perror("ptyfork");
      return false;
    }
    fcntl(this->cmdtty.mfd, F_SETFD, FD_CLOEXEC);
  }

  // Child: Run the command line in the new process
  {
    if (cpid==0) {
    if (raw) ttySetRaw(STDIN_FILENO, NULL);
    // from tlpi-dist/pty/script.c
    char *shell = getenv("SHELL");
    if (shell == NULL || *shell == '\0') shell = "/bin/sh";
    // See system manpage for how this was modelled
    // execute the shell command line as if it were passed via -c eg.
    // $ $SHELL -c '((i=0)); while :; do echo $i:hello; sleep 2; ((i++)); done'
    execlp(shell,           // executable 
	   shell,           // argv[0]    
	   "-c",            // argv[1]
	   this->cmdline,   // argv[2]
	   (char *) NULL);  // argv[3] terminating null
    perror("execlp");
    return false;
  }
  }
  
  // Parent: finish setup the command object for the newly created
  // child
  {
    this->pidfd         = pidfd_open(cpid, PIDFD_NONBLOCK);
    fcntl(this->pidfd, F_SETFD, FD_CLOEXEC);
    this->pid           = cpid;
    this->pidfded.hdlr  = cmdPidEvent;
    this->pidfded.obj   = this;
    this->cmdttyed.hdlr = cmdttyEvent;
    this->cmdttyed.obj  = this;
    this->cltttyed.hdlr = cltttyEvent;
    this->cltttyed.obj  = this;
  }
  return true;
}

extern bool
cmdCleanup(cmd_t *this)
{
  assert(this);
  if (verbose>0) {
    cmdDump(this, stderr, "clean: ");
  }
  if (this->pid>0) {
    siginfo_t info;
    int es;
    struct pollfd pollfd;
    int ready;
    assert(kill(this->pid, SIGTERM)==0);
  retry:
    pollfd.fd = this->pidfd;
    pollfd.events = POLLIN;
    ready = poll(&pollfd, 1, 100); // give it 100 millseconds to exit cleanly
    if (ready<0) {
      if (errno==EINTR) goto retry; else {
	perror("poll"); assert(0);
      }
    }
    if (ready == 0) {
      // timed out ... moving on to SIGKILL
      EPRINT("%s did not die with SIGTERM moving on to SIGKILL!", this->name);
      cmdDump(this, stderr, "\n  ");
      assert(kill(this->pid, SIGKILL)==0);
      goto retry;
    }
    
    es = waitid(P_PIDFD, this->pidfd,  &info, WEXITED);
    if (es<0) {
      perror("waitpid after SIGTERM");
      assert(0);
    }
    VLPRINT(1, "  exit status=%d\n", info.si_status);
    ttyCleanup(&(this->cmdtty));
    ttyCleanup(&(this->clttty));
  }
  return true;
}

