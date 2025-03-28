#include "yar.h"
#include <sys/syscall.h>
#include <sys/pidfd.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <tty/tty_functions.h>
#include <time.h>

// MACROS to help with circular cmdbuffer management --
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

// MISC
static int
cmdttyBufOutput(cmd_t *this, uint32_t evnts)
{
  char c;
  tty_t *tty = &(this->cmdtty);
  int fd = tty->dfd;
  int n;
  
  n  = ttyReadChar(tty, &c, NULL, 0);
  if (n==0) {
    VLPRINT(2, "%p: read returned 0\n", this);
    NYI;
  } else if (n==1)  {
    if (evnts && verbose(2)) {
      asciistr_t charstr;
      ascii_char2str(c, charstr);
      fprintf(stderr,"cmdttyEvent: ---> CMDTTY: START: EIN: tty(%p):%s(%s) fd:%d"
		 " evnts:0x%08x cmd:%p(%s)\n"
	      "ttyReadChar:    %p:%s(%s) fd:%d c:%02x(%s) %s n:%d\n",
	      tty, tty->link, tty->path, fd,
	      evnts, this, this->name,
	      tty, tty->link, tty->path, fd, c, charstr,
	      (ascii_isprintable(c)) ? "" : "^^^^ NOT PRINTABLE ^^^^",
	      n);
      
    }
    int i        = cmdbufNtoI(this->n);    // account for circular buffer
    this->buf[i] = c;                      // store character in buffer
    assert(this->n+1 > this->n);           // yikes we rolled over
    this->n++;                             // inc n -- bytes since last flush
    
    n = ttyWriteChar(&(this->clttty), c, NULL);  // send data to our client tty
    if (n != 1) NYI;
    if (!GBLS.linebufferbcst) { 
      n += ttyWriteChar(&GBLS.bcsttty, c, NULL);   // send data to broadcast tty
      if (n != 2) NYI;
    } else {
      if (c=='\n') {
	if (GBLS.prefixbcst && this->bcstprefix && this->bcstprefixlen > 0) {
	  ttyWriteBuf(&GBLS.bcsttty, this->bcstprefix, this->bcstprefixlen,
		      NULL);
	}
	// got a new line write complete line to the broadcast tty
	int start = cmdbufNtoI(this->start);
	int end   = i;
	if (start <= end) {
	  n += ttyWriteBuf(&GBLS.bcsttty, (char *)&(this->buf[start]),
			   (end - start) + 1, NULL);
	  this->start = this->n;
	  //	  if (n!=(len+1)) NYI;
	} else {
	  NYI;
	}
      }
    }
    if (evnts && verbose(2)) {
      fprintf(stderr, "cmdttyEvent: <--- CMDTTY: END: EIN: tty(%p):%s(%s) fd:%d"
	     " evnts:0x%08x n:%d cmd:%p(%s)\n",
	     tty, tty->link, tty->path, fd, evnts, n, this, this->name);      
    }
    if (verbose(3)) cmdDump(this, stderr, "CMD OUTPUT BUFFERED:");
  } else {
    EPRINT("read returned: %d\n", n);
    perror("read");
    NYI;
  }
  return n;
}

static void
cmdttyDrain(cmd_t *this)
{
  // drain any remaining data???
  while (cmdttyBufOutput(this,0)>0) {
    VLPRINT(2, "%p: got data after HUP", this->name);
  }
}
  
// EVENT HANDLERS
// pidfd event -- should only be POLLIN indicating death of the command pr
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
cmdCmdttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t *this = obj;
  tty_t *tty = &(this->cmdtty);
  int fd = tty->dfd;
  
  ASSERT(ttyIsCmdtty(tty));
  VLPRINT(3,"START: CMDTTY tty(%p):%s(%s) fd:%d evnts:0x%08x"
	    " cmd:%s(%p)\n", tty, tty->link, tty->path, fd, evnts,
	  this->name, this);
  if (evnts & EPOLLIN) {
    cmdttyBufOutput(this, evnts);    // data in on this fd is output from command
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    // now that we are keeping the slave open when we create the
    // master I don't thnk we should see this
    VLPRINT(2, "EPOLLHUP(%x)\n", EPOLLHUP);
    fprintf(stderr, "**************************************************\n");
    fprintf(stderr, "********** FUCKING SHIT!!!!!!!!!!!!!!!************\n");
    fprintf(stderr, "**************************************************\n");
    cmdttyDrain(this);
    {
      if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
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
  VLPRINT(3,"END : CMDTTY tty(%p):%s(%s) fd:%d evnts:0x%08x"
	  " cmd:%s(%p)\n", tty, tty->link, tty->path, fd, evnts,
	  this->name, this);
  return EVNT_HDLR_SUCCESS;
}

// clttty event -- master side of client pty used by clients to read and
//                 write data to and from the command 
static evnthdlrrc_t
cmdCltttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t *this = obj;
  tty_t *tty  = &(this->clttty);
  int    fd   = tty->dfd;

  ASSERT(ttyIsClttty(tty));
  VLPRINT(3,"START: CLTTTY tty(%p):%s(%s) fd:%d evnts:0x%08x"
	  " cmd:%s(%p)\n", tty, tty->link, tty->path, fd, evnts,
	  this->name, this);
  if (evnts & EPOLLIN) {
    char c;
    int n = ttyReadChar(&this->clttty, &c, &(this->lastwrite), this->delay);
    if (n) {
      if (verbose(2)) {
	  asciistr_t charstr;
	  ascii_char2str((int)c, charstr);
	  VPRINT("---> CLTTTY: START: EIN: tty(%p):%s(%s) fd:%d"
		 " evnts:0x%08x cmd:%p(%s)\n"
		 "ttyReadChar:    %p:%s(%s) fd:%d c:%02x(%s) %s n:%d\n",
		 tty, tty->link, tty->path, fd, evnts, this,
		 this->name, tty, tty->link, tty->path, fd, c, charstr,
		 (ascii_isprintable(c)) ? "" : "^^^^ NOT PRINTABLE ^^^^",
		 n);
	}
      n=cmdWriteChar(this,c);
      if ( n != 1 ) {
	EPRINT("  write returned: n=%d\n", n);
	NYI;
      }
      VLPRINT(2, "--->  CLTTTY: END: EIN: tty(%p):%s(%s) fd:%d"
	      " evnts:0x%08x n:%d cmd:%p(%s)\n",
	      tty, tty->link, tty->path, fd, evnts, n, this, this->name);      
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
  VLPRINT(3,"END : CLTTTY tty(%p):%s(%s) fd:%d evnts:0x%08x"
	  " cmd:%s(%p)\n", tty, tty->link, tty->path, fd, evnts,
	  this->name, this);
  return EVNT_HDLR_SUCCESS;
}

// EXTERNALS
extern void
cmdDump(cmd_t *this, FILE *f, char *prefix)
{
  assert(this);
  int i=cmdbufNtoI(this->n);
  int c=this->buf[i];
  asciistr_t charstr = { 0,0,0,0 };
  ascii_char2str(i, charstr);
  
  fprintf(f, "%scmd: this=%p pid=%ld pidfd=%d name=%s exitstatus=%d\n"
	  "    cmdline=\"%s\" \n    bcstprefix=\"%s\"(len=%d)"
	  " delay=%f log=%s n=%lu start=%lu "
	  "lastwrite=%ld:%ld lastchar:buf[%d]=%02x(%s)\n"
	  , prefix, this,
	  (long)this->pid, this->pidfd, this->name, this->exitstatus,
	  this->cmdline, this->bcstprefix, this->bcstprefixlen, this->delay,
	  this->log, this->n, this->start,
	  this->lastwrite.tv_sec, this->lastwrite.tv_nsec, i, c, charstr);
  
  if (this->n) {
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

extern bool
cmdInit(cmd_t *this, char *name, char *cmdline, double delay, char *ttylink,
	char *log)
{
  assert(name && cmdline); 
  this->name              = name;
  this->bcstprefixlen     = strlen(name)+2;
  this->bcstprefix        = malloc(this->bcstprefixlen);
  snprintf(this->bcstprefix, this->bcstprefixlen, "%s:", this->name);
  this->cmdline           = cmdline;
  this->delay             = delay;
  this->log               = log;
  this->n                 = 0;
  this->start             = 0;
  this->pid               = -1;
  this->pidfd             = -1;
  this->exitstatus        = -1;
  this->retry             = true;
  this->lastwrite.tv_sec  = 0;
  this->lastwrite.tv_nsec = 0;
  this->pidfded           = (evntdesc_t){ NULL, NULL };
  ttyInit(&(this->cmdtty), NULL);
  ttyInit(&(this->clttty), ttylink);
  return true;
}

extern bool
cmdCreate(cmd_t *this, bool raw)
{
  pid_t cpid;
  evntdesc_t ed;
  assert(this);
  assert(this->cmdline);
  // We use pidfd to track exits of the command processes.  Right now
  // we are using fork to create these processes, however we might
  // need to switch to using clone2 so that we can more precisely meet
  // the requirements for using pidfd's.  See man pidfd_open for
  // the restrictions.

  // First try and create a client tty for this command so that we
  // can fail early, before we try and create the ocommand process
  ed = (evntdesc_t){ .obj = this, .hdlr = cmdCltttyEvent };
  if (!ttyCltttyCreate(&this->clttty, ed, true)) return false;

  // Create tty that is connected to a NEW forked child process
  ed = (evntdesc_t){ .obj = this, .hdlr = cmdCmdttyEvent };
  if (!ttyCmdttyForkCreate(&(this->cmdtty), ed, &cpid)) return false;
  
  // **** WARNING RUNNING IN BOTH PARENT AND CHILD
  
  if (cpid==0) {
    // CHILD: Run the command line in the new process
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
    NYI;
  } else {
      // PARENT: finish setup the command object for the newly created child
    this->pidfd        = pidfd_open(cpid, PIDFD_NONBLOCK);
    fcntl(this->pidfd, F_SETFD, FD_CLOEXEC);
    this->pid          = cpid;
    this->pidfded      = (evntdesc_t){ .hdlr = cmdPidEvent, .obj = this };
    return true;
  }
}

extern bool
cmdRegisterEvents(cmd_t *this, int epollfd)
{
  struct epoll_event ev;
  ASSERT(this && epollfd != -1);
  // 1) Register for process events (termination) for this cmd
  //    (eg. allow us to detect if the command process terminates)
  {
    ASSERT(this->pidfd != -1 && this->pidfded.obj == this &&
	   this->pidfded.hdlr == cmdPidEvent);
    ev.data.ptr = &(this->pidfded);
    ev.events  = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR | EPOLLET; // Edge
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, this->pidfd, &ev) == -1 ) {
      perror("epoll_ctl: this->pidfd");
      return false;
    }
  }

  // 2) Register for io events from this command's process via its dom
  //    cmdtty.
  //   (eg. let's us know when the command process has produced data on 
  //        its standard output or standard error streams via cmd's sub tty)
  //   event descriptor was setup at the time we created the cmdtty
  assert(ttyRegisterEvents(&(this->cmdtty),epollfd));
  
  // Now take care of cmd's client tty interface
  assert(ttyRegisterEvents(&(this->clttty),epollfd));
  
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
  // not sure we really want to do this... will need to readdress this
  // when restarting a commandline
  if (this->bcstprefix) {
    free(this->bcstprefix);
    this->bcstprefix    = NULL;
    this->bcstprefixlen = 0;
  }
  return true;
}
