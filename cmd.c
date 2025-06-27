#include "yar.h"
#include <sys/syscall.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <tty/tty_functions.h>
#include <time.h>

#ifdef NOSYSPIDFD
// JA: FIXME HACK to compile on older ubuntu system like pt-psml
//     where there is there is non pidfd and fcntl header problems
#include <linux/pidfd.h>
#include <linux/wait.h>
static int
pidfd_open(pid_t pid, unsigned int flags)
{
  return syscall(__NR_pidfd_open, pid, flags);
}
int open(const char *pathname, int flags);
int fcntl(int fd, int cmd, ... /* arg */ );
#else
#include <sys/pidfd.h>
#endif


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
// NYI: FYI: logging not yet implemented
static int
cmdttyProcessOutput(cmd_t *this, uint32_t evnts)
{
  char c;
  tty_t *tty = &(this->cmdtty);
  int fd = tty->dfd;
  int n;

  // read the a character from std out/err of the command process via
  // cmdtty dom
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
    int i        = cmdbufNtoI(this->bufn); // account for circular buffer
    this->buf[i] = c;                      // store character in buffer
    assert(this->bufn+1 > this->bufn);     // yikes we rolled over
    this->bufn++;                          // inc n -- bytes since last flush

    int written;                           
    written = ttyWriteChar(&(this->clttty), c, NULL); // write data to clt tty
    if (written != 1) NYI;
    n = written;
    if (GBLS.bcstflg) {
      if (!GBLS.linebufferbcst) { 
	written = ttyWriteChar(&GBLS.bcsttty, c, NULL); //write data to bcst tty
	if (written != 1) NYI;
	n += written;
      } else {
	if (cmdbufNtoI(this->bufn) == cmdbufNtoI(this->bufstart)) {
	  this->bufof++; // buffer just wrapped
	  EPRINT("cmd:%s line overflowed output buffer: start:%zu m:%zu of:%d\n",
		 this->name, this->bufstart, this->bufn, this->bufof);
	}
	if (c=='\n') {
	  // Write cmd prefix to tty if enabled
	  if (GBLS.prefixbcst && this->bcstprefix && this->bcstprefixlen > 0) {
	    written = ttyWriteBuf(&GBLS.bcsttty, this->bcstprefix,
				  this->bcstprefixlen,
				  NULL);
	    assert(written == this->bcstprefixlen);
	    // we don't include prefix in count of data written to the tty
	  }
	  int len;
	  int end = i;          // end of line is last location we put data
	  if (this->bufof==0) {
	    // Handle no over flow cases -> buffer holds a complete line 
	    int start = cmdbufNtoI(this->bufstart);
	    if (start <= end) {
	      // Line does not wrap the buffer (end is >= start)
	      len = (end - start) + 1;
	      written = ttyWriteBuf(&GBLS.bcsttty,
				    (char *)&(this->buf[start]),
				    len,           
				    NULL);
	      assert(written == len);
	      n += written;
	    } else {
	      // Line wraps across end of the buffer (no overflow and start > end) 
	      // Requires two writes: 
	      //   1. beginning of the line is from start to buffer end
	      len = cmdbufSize - start;
	      ASSERT(len>=1);
	      written= ttyWriteBuf(&GBLS.bcsttty,
				   (char *)&(this->buf[start]),
				   len,
				   NULL);
	      assert(written == len);
	      n += written;
	      //   2. end of the line is from buffer start to end
	      len = end + 1;
	      written = ttyWriteBuf(&GBLS.bcsttty,
				    (char *)&(this->buf[0]),
				    len,
				    NULL);
	      assert(written == len);
	      n += written;
	    }
	  } else {
	    // handle overflow cases
	    //  start is irrelevant last bufsize bytes written are from
	    //  end+1 to bufend and 0 to end
	    if ( (end+1) < cmdbufSize ) {
	      len = cmdbufSize - (end+1);
	      written = ttyWriteBuf(&(GBLS.bcsttty),
				    (char *)&(this->buf[end+1]),
				    len,
				    NULL);
	      assert(len == written);
	      n += written;
	    }
	  // given wrap data is from 0  to end
	    len = end+1;
	    written = ttyWriteBuf(&(GBLS.bcsttty),
				  (char *)&(this->buf[0]),
				  len,
				  NULL);
	    assert(written == len);
	    n += written;
	    this->bufof = 0;            // reset overflow count
	  }
	  this->bufstart = this->bufn;  // record start location of next line
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
  while (cmdttyProcessOutput(this,0)>0) {
    VLPRINT(2, "%p: got data after HUP", this->name);
  }
}
  
// EVENT HANDLERS
// pidfd event -- should only be POLLIN indicating death of the command pr
static evnthdlrrc_t
cmdPidEvent(void *obj, uint32_t evnts, int epollfd)
{
  cmd_t       *this = obj;
  int            fd = this->pidfd;
  double startdelay = 0.0;
  
  if (verbose(2)) {
    cmdDump(this, stderr, "pidEvent on:\n  ");
  }
  if (evnts & EPOLLIN) {
    VLPRINT(2, "EPOLLIN(%x)\n", EPOLLIN);
    siginfo_t info;
    int es = waitid(P_PIDFD, fd,  &info, WEXITED);
    if (es<0) {
      perror("waitpid after deteching child death failed");
      assert(0);
    }
    this->exitstatus = info.si_status;
    if (verbose(1)) {
      cmdDump(this, stderr, "*** Command Died:\n  ");
    }
    // remove pid fd from poll set
    if (epollfd != -1) {
      struct epoll_event dummyev;
      if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &dummyev) == -1) {
	perror("epoll_ctl: EPOLL_CTL_DEL fd");
	assert(0);
      }
    }
    // reset fields
    this->pidfd = -1;
    this->pid   = -1;

    // cleanup on exit logic (takes precedence)
    if (this->deleteonexit) {
      VPRINT("%s: pid:%d cleaning up on exit status:%d\n",
	     this->name, this->pid,  this->exitstatus); 
      cmd_t *cmd=NULL;
      HASH_FIND_STR(GBLS.cmds, this->name, cmd);
      cmdCleanup(this);
      if (cmd != NULL) {
	ASSERT(this==cmd);
	HASH_DEL(GBLS.cmds, cmd);
	free(cmd);
	if (HASH_COUNT(GBLS.cmds) == 0 && GBLS.exitonidle) {
	  cleanup();
	  exit(EXIT_SUCCESS);
	}
      }
      goto done;
    } 
    
    // restart on exit logic
    // global restart behavior takes precedent but a command can
    // be forced not to restart if needed 
    if (GBLS.restart && this->restart) {
      if (this->exitstatus == 0) startdelay=GBLS.restartcmddelay;
      else startdelay=GBLS.errrestartcmddelay;
      assert(cmdStart(this, true, epollfd, startdelay));
      this->restartcnt++;
      VPRINT("%s: restarted pid:%d restartcnt:%d\n",
	     this->name, this->pid, this->restartcnt);
    }
    
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
    cmdttyProcessOutput(this, evnts);  // data on this fd is output from command
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

evnthdlrrc_t
cmdCltttyNotify(void *obj, uint32_t mask, int epollfd)
{
  cmd_t *this = obj;
  VPRINT("%s: mask=%04x, ifd=%d: ", this->name, mask, epollfd);
  switch (mask) {
  case IN_OPEN:
    if (verbose(1)) {
      fprintf(stderr, "OPEN: cmdtty:%s(%s) count:%d\n", 
	      this->cmdtty.link, this->cmdtty.path,
	      this->cmdtty.opens);
    }
    if (cmdStart(this, true, epollfd,0.0)) {
      VPRINT("%s started pidfd=%d pid=%d\n", this->name, this->pidfd, this->pid);
    }
    mask = mask & ~IN_OPEN;
    break;
  case IN_CLOSE_WRITE:
  case IN_CLOSE_NOWRITE:
    if (verbose(1)) {
      fprintf(stderr, "CLOSE: cmdtty:%s(%s) count:%d\n", 
	      this->cmdtty.link, this->cmdtty.path,
	      this->cmdtty.opens);
    }
    if (cmdStop(this, epollfd, false)) {
      VPRINT("%s stopped exitstatus:%d\n", this->name, this->exitstatus);
    }
    mask = mask & ~IN_OPEN;
    mask = mask & ~IN_CLOSE;
    break;
  default:
    EPRINT("Unexpected notify case: mask:0x%x\n",
	   mask);
    NYI;
  }  
  return EVNT_HDLR_SUCCESS;
}

// EXTERNALS
extern void
cmdDump(cmd_t *this, FILE *f, char *prefix)
{
  assert(this);
  int i=cmdbufNtoI(this->bufn);
  int c=this->buf[i];
  asciistr_t charstr = { 0,0,0,0 };
  ascii_char2str(i, charstr);
  
  fprintf(f, "%scmd: this=%p pid=%ld pidfd=%d name=%s exitstatus=%d\n"
	  "    restart=%d restartcnt=%d deleteonexit=%d\n    stopstr=\"%s\"\n"
	  "    cmdstr=\"%s\"\n    cmdline=\"%s\"\n    bcstprefix=\"%s\"(len=%d)"
	  " delay=%f log=%s bufn=%lu bufstart=%lu bufof=%d "
	  "lastwrite=%ld:%ld lastchar:buf[%d]=%02x(%s)\n"
	  , prefix, this,
	  (long)this->pid, this->pidfd, this->name, this->exitstatus,
	  this->restart, this->restartcnt, this->deleteonexit, this->stopstr,
	  this->cmdstr, this->cmdline, this->bcstprefix, this->bcstprefixlen,
	  this->delay, this->log, this->bufn, this->bufstart, this->bufof,
	  this->lastwrite.tv_sec, this->lastwrite.tv_nsec, i, c, charstr);
  
  if (this->bufn) {
    if (!cmdbufWrapped(this->bufn)) {
      hexdump(f, this->buf, this->bufn);
    } else {
      // JA FIXME: this could probably be made to work for both cases but I 
      // don't want to think about it 
      size_t start = cmdbufDataStart(this->bufn);
      char n = cmdbufSize - start; 
      hexdump(f, &(this->buf[start]),  n);
      hexdump(f, &(this->buf[0]), cmdbufSize - n);
    }
  }
  ttyDump(&(this->cmdtty), f, "    cmdtty: ");
  ttyDump(&(this->clttty), f, "    clttty: ");
}

extern bool
cmdInit(cmd_t *this, char *cmdstr, char *name, char *cmdline, double delay,
	char *ttylink, char *log)
{
  if (log != NULL) NYI;             // need to open log and add write to it ;-)
  assert(cmdstr && name && cmdline);
  this->cmdstr            = cmdstr;
  this->stopstr           = NULL;
  this->name              = name;
  this->bcstprefixlen     = strlen(name)+2;   // +2 for ": " do no include null
  this->bcstprefix        = malloc(this->bcstprefixlen+1); // +1 for null
  snprintf(this->bcstprefix, this->bcstprefixlen+1, "%s" ": ", this->name);
  this->cmdline           = cmdline;
  this->delay             = delay;
  this->log               = log;
  this->bufn              = 0;
  this->bufstart          = 0;
  this->bufof             = 0;
  this->pid               = -1;
  this->pidfd             = -1;
  this->exitstatus        = -1;
  this->restartcnt        = 0;
  this->restart           = true;           
  this->deleteonexit      = GBLS.exitonidle; 
  this->lastwrite.tv_sec  = 0;
  this->lastwrite.tv_nsec = 0;
  this->pidfded           = (evntdesc_t){ NULL, NULL };
  ttyInit(&(this->cmdtty), NULL);
  ttyInit(&(this->clttty), ttylink);
  return true;
}

// NOTE caller has to register this new process with epoll loop!
// FIXME: JA: add cmdRegisterPidFd
extern bool
cmdStart(cmd_t *this, bool raw, int epollfd, double startdelay)
{
  pid_t cpid;
  // we expect the cmdtty to be created with both the dom and sub sides
  // open in the yar process
  ASSERT(this &&
	 this->cmdtty.dfd != -1 && this->cmdtty.sfd != -1);

  if (cmdIsRunning(this)) return false;
  
  // NOTE: the sub-tty has been created and is being
  //       watched via inotify for opens.  When fork/exec'd command process
  //       opens the slave we will get and event and bump the open count
  cpid = fork();
  // **** WARNING RUNNING IN BOTH PARENT AND CHILD
  if (cpid == -1) {
    perror("fork");
    assert(cpid != -1);
  }
  
  if (cpid==0) {
    // CHILD: Run the command line in the new process
    // inspired by pty_fork form tlpi-dist
    
    // create a new session 
    assert(setsid() != -1);

    // we do a new open on the sub-tty to count it as an open of the tty
    // by the child
    int subfd = open(this->cmdtty.path, O_RDWR);   
    if (subfd == -1) {
      perror("open of sub-tty in child");
      assert(subfd != -1);
    }
    
    // make the cmd sub-tty the controlling tty
    assert(ioctl(subfd, TIOCSCTTY, 0) != -1); 

    // Duplicate sub-tty to be child's stdin, stdout, and stderr 
    assert(dup2(subfd, STDIN_FILENO) == STDIN_FILENO);
    assert(dup2(subfd, STDOUT_FILENO) == STDOUT_FILENO);
    assert(dup2(subfd, STDERR_FILENO) == STDERR_FILENO);

    // avoid leaking another file descriptor close subfd if needed 
    if (subfd > STDERR_FILENO) assert(close(subfd)!=-1);
    
    if (raw) ttySetRaw(STDIN_FILENO, NULL);

    // from tlpi-dist/pty/script.c
    char *shell = getenv("SHELL");
    if (shell == NULL || *shell == '\0') shell = "/bin/sh";

    if (startdelay>0.0) {
      // pause for delay seconds before starting (can be a fractionaly value)
      // this is useful in the case of restarts to trottle
      // spam command execution
      delaysec(startdelay);
    }
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
    cmdRegisterProcessEvents(this, epollfd);
    return true;
  }
}

extern bool
cmdStop(cmd_t *this, int epollfd, bool force)
{
  
  if (!cmdIsRunning(this)) return false;
			     
  if (force == false && (this->clttty.opens != 0 || GBLS.bcsttty.opens != 0)) {
    return false;
  }

  // send stop string if there is one
  {
    char *cptr=NULL;
    if (this->stopstr) cptr=this->stopstr;
    else if (GBLS.stopstr) cptr=GBLS.stopstr;
    if (cptr) {
      // always send a newline first (motivated by ipmitool semantics)
      int n=cmdWriteChar(this,'\n');
      if ( n != 1 ) {
	EPRINT("  write returned: n=%d\n", n);
	NYI;
      }      
      for (; *cptr; cptr++) {
	int n=cmdWriteChar(this,*cptr);
	if ( n != 1 ) {
	  EPRINT("  write returned: n=%d\n", n);
	  NYI;
	}
	// hack use 
	delaysec(this->delay);
      }
    }
    VPRINT("stopstr: sent: \\n%s\n",
	   (this->stopstr) ? this->stopstr : GBLS.stopstr);
  }
  
  // remove cmd pidfd from epoll as we know we are stopping it
  if (epollfd != -1) {
    struct epoll_event dummyev;
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, this->pidfd, &dummyev) == -1) {
      perror("epoll_ctl: EPOLL_CTL_DEL fd");
      assert(0);
    }
  }
 
  // kill the cmd process
  {
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
    this->exitstatus = info.si_status;
    VLPRINT(1, "  exit status=%d\n", this->exitstatus);
    close(this->pidfd);
  }
  // reset fields
  this->pidfd = -1;
  this->pid   = -1;
  
  return true;
}

extern bool
cmdCreate(cmd_t *this)
{
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
  if (!ttyCreate(&this->clttty, ed,
		 (evntdesc_t){.obj=this, .hdlr=cmdCltttyNotify },
		 true)) return false;

  // Create tty that is connected to a NEW forked child process
  ed = (evntdesc_t){ .obj = this, .hdlr = cmdCmdttyEvent };
  if (!ttyCreate(&(this->cmdtty), ed,
		 (evntdesc_t){NULL, NULL},
		 true)) return false;
  return true;
}

extern bool
cmdRegisterttyEvents(cmd_t *this, int epollfd)
{
  // make sure we are registered for events of clttty
  // Note we purposefully can register for clt tty events before the command
  // process events to allow clt tty operation to lazy start the command process
  assert(ttyRegisterEvents(&(this->clttty),epollfd));
  
  // Register for io events from this command's process via its dom
  //    cmdtty.
  //   (eg. let's us know when the command process has produced data on 
  //        its standard output or standard error streams via cmd's sub tty)
  //   event descriptor was setup at the time we created the cmdtty
  assert(ttyRegisterEvents(&(this->cmdtty),epollfd));

  return true;
}

extern bool
cmdRegisterProcessEvents(cmd_t *this, int epollfd)
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
  return true;
}

extern bool
cmdCleanup(cmd_t *this)
{
  assert(this);
  if (verbose(2)) {
    cmdDump(this, stderr, "clean: ");
  }
  
  cmdStop(this, -1, true);
  VLPRINT(1, "  exit status=%d\n", this->exitstatus);

  ttyCleanup(&(this->cmdtty));
  ttyCleanup(&(this->clttty));

  if (this->bcstprefix) {
    free(this->cmdstr);
    free(this->bcstprefix);
    this->cmdstr        = NULL;
    this->name          = NULL;
    this->cmdline       = NULL;
    this->log           = NULL;
    this->bcstprefix    = NULL;
    this->bcstprefixlen = 0;
  }
  return true;
}
