#include "yar.h"
#include <sys/syscall.h>
#include <sys/pidfd.h>
#include <pty/pty_fork.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>

extern bool
cmdInit(cmd_t *this, char *name, char *cmdline, double delay, char *ttylink,
	char *log)
{
  assert(name && cmdline); 
  this->name    = name;
  this->cmdline = cmdline;
  this->delay   = delay;
  this->log     = log;
  this->n       = 0;
  this->pid     = -1;
  this->pidfd   = -1;
  ttyInit(&(this->cmdtty), NULL);
  ttyInit(&(this->clttty), ttylink);
  return true;
}

extern void
cmdDump(cmd_t *this, FILE *f, char *prefix)
{
  assert(this);
  fprintf(f, "%scmd: this=0x%p pid=%ld pidfd=%d name=%s cmdline=\"%s\" delay=%f"
	  " log=%s n=%lu", prefix, this, (long)this->pid, this->pidfd,
	  this->name, this->cmdline, this->delay, this->log, this->n);
  if (this->n) hexdump(f, this->buf, this->n);
  ttyDump(&(this->cmdtty), f, "\n    cmdtty: ");
  ttyDump(&(this->clttty), f, "    clttty: ");
}

static evnthdlrrc_t
cmdPidEvent(void *obj, uint32_t evnts)
{
  cmd_t *this = obj;
  if (verbose(2)) {
    cmdDump(this, stderr, "pidEvent on:\n  ");
  }
  if (evnts & EPOLLIN) {
    VLPRINT(2,"EPOLLIN(%x)\n", EPOLLIN);
    evnts = evnts & ~EPOLLIN;
  }
  if (evnts & EPOLLHUP) {
    VLPRINT(2,"EPOLLHUP(%x)\n", EPOLLHUP);
    evnts = evnts & ~EPOLLHUP;
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2,"EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    evnts = evnts & ~EPOLLRDHUP;
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2,"EPOLLERR(%x)\n", EPOLLERR);
    evnts = evnts & ~EPOLLRDHUP;
  }
  if (evnts != 0) {
    VLPRINT(2,"unknown events evnts:%x", evnts);
  }
  return EVNT_HDLR_SUCCESS;
}


extern bool
cmdCreate(cmd_t *this)
{
  assert(this);
  assert(this->cmdline);

  pid_t cpid = ptyFork(&(this->cmdtty.fd),
		       this->cmdtty.path, sizeof(this->cmdtty.path),
		       NULL, NULL);
  if (cpid == -1) {
    perror("ptyfork");
    return false;
  }

  if (cpid==0) {
    // child
    // from tlpi-dist/pty/script.c
    VLPRINT(1, "child: %ld\n", (long)getpid())
    char *shell = getenv("SHELL");
    if (shell == NULL || *shell == '\0') shell = "/bin/sh";
    // from system manpage
    execlp(shell, shell, "-c", this->cmdline, (char *) NULL);
    perror("execlp");
    return false;
  }
  // parent
  // see man pidfd_open (if restrictions due to fork are a problem then
  // we might need to switch to our own "ptyclone" to have more control
  this->pidfd = pidfd_open(cpid, PIDFD_NONBLOCK);
  this->pid   = cpid;
  this->pidfded.hdlr = cmdPidEvent;
  this->pidfded.obj  = this;
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
  }
  return true;
}

