//012345678901234567890123456789012345678901234567890123456789012345678901234567
#include "yar.h"
#include <getopt.h>
#include <signal.h>

#define DEFAULT_BTTY_LINK "btty"

globals_t GBLS = {
  .slowestcmd = NULL,
  .verbose    = 0,
  .cmds       = NULL
};

static void
usage(char *name)
{
  fprintf(stderr,
	  "USAGE: %s [-v] [-b broadcast tty link path] [-d <default read delay sec>] <name,pty,log,delay,cmd> [<name,pty,log,delay,cmd>]\n"
	  " Yet Another Relay\n",
	  name);
}

static void
GBLSDump(FILE *f)
{
  fprintf(f, "GBLS.verbose=%d\n", GBLS.verbose);
  fprintf(f, "GBLS.defaultcmddelay=%f\n", GBLS.defaultcmddelay);
  ttyDump(&GBLS.btty, stderr, "GBLS.btty: ");
  fprintf(f, "GBLS.cmds:");
  {
      cmd_t *cmd, *tmp;
      HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
	cmdDump(cmd, stderr, "\n  ");
      }
  }
  fprintf(f, "GBLS.slowestcmd=%p", GBLS.slowestcmd);
  if (GBLS.slowestcmd) fprintf(f, "(%s)\n", GBLS.slowestcmd->name);
  else fprintf(f, "\n");
}

static bool
cmdstrParse(char *cmdstr, char **name, char **cmdline,
	    double *delay, char **ttylink, char **log)
{
  char *nptr, *orig; // next token pointer, original cmdstr
  bool rc=true;
  
  orig = malloc(strlen(cmdstr));
  strcpy(orig, cmdstr);
  
  nptr = strsep(&cmdstr, ",");  // parse name
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT("Bad command name: %s\n", orig);
    rc = false;
    goto done;
  }
  *name=nptr;
  
  nptr = strsep(&cmdstr, ",");     // parse ttylink
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT("Bad ttylink: %s\n", orig);
    rc = false;
    goto done;
  }
  if (*nptr == 0) { // none specified
    *ttylink = *name;
  } else {
    *ttylink = nptr;
  }

  nptr = strsep(&cmdstr, ",");     // parse log
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT("Bad log path: %s\n", orig);
    rc = false;
    goto done;
  }
  if (*nptr == 0) { // none specified
    // change log to NULL
    *log = NULL;
  } else {
    *log = nptr;
  }

  nptr = strsep(&cmdstr, ",");   // parse delay value string
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT("Bad delay: %s\n", orig);
    rc = false;
    goto done;
  }
  if (*nptr==0) { // none specified
     *delay = GBLS.defaultcmddelay;
  } else {
    errno = 0;
    *delay = strtod(nptr, NULL);
    if (errno != 0) {
      perror("bad delay value");
      rc = false;
      goto done;
    }
  }

  // commandline is everthing that list left (avoid parsing incase command
  // line uses , itself (eg socat)
  if (cmdstr == NULL || *cmdstr == 0) {
    EPRINT("bad cmdline: command must not be empty: %s\n", orig);
    rc = false;
    goto done;
  }
  *cmdline=cmdstr;
 done:
    free(orig);
    return rc;
}

static bool
GBLSAddCmd(char *cmdstr)
{
  char *name, *cmdline, *ttylink, *log;
  double delay;
  cmd_t *cmd;
  if (!cmdstrParse(cmdstr, &name, &cmdline, &delay, &ttylink,
		   &log)) return false;
  // check to see if name is already used
  HASH_FIND_STR(GBLS.cmds, name, cmd);
  if (cmd == NULL) {
    // new command
    cmd=malloc(sizeof(cmd_t));
    if (!cmdInit(cmd, name, cmdline, delay, ttylink, log)) {
      EPRINT("Failed to initCmd(%p,%s,%s,%f,%s,%s)", cmd, name, cmdline,
	     delay, ttylink, log);
      free(cmd);
      return false;
    }
    if (!cmdCreate(cmd, true)) {
      cmdDump(cmd, stderr, "Failed to create");
      cmdCleanup(cmd);
      free(cmd);
      return false;
    }
    if (GBLS.slowestcmd) {
      if (delay > GBLS.slowestcmd->delay) GBLS.slowestcmd = cmd;
    } else {
      GBLS.slowestcmd = cmd;
    }
    HASH_ADD_KEYPTR(hh, GBLS.cmds, cmd->name, strlen(cmd->name), cmd);
  } else {
    EPRINT("%s: command names must be unique. %s already used:",
	   cmdstr, name);
    cmdDump(cmd, stderr, "\n  ");
    return false;
  }
  return true;
}

static int
GBLSCmdsWriteChar(char c)
{
  int n, cnt=0;
  cmd_t *cmd, *tmp;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n=cmdWriteChar(cmd, c);
    if ( n != 1 ) {
      EPRINT("  write returned: n=%d\n", n);
      NYI;
    }
    if (n) cnt++;
  }
  return cnt;
}

static bool
argsParse(int argc, char **argv)
{
    char opt;
    
    while ((opt = getopt(argc, argv, "b:d:hv")) != -1) {
    switch (opt) {
    case 'b':
      GBLS.btty.link=optarg;
      break;
    case 'd':
      errno = 0;
      GBLS.defaultcmddelay = strtod(optarg, NULL);
      if (errno != 0) {
	perror("bad delay value");
	return  false;
      }
      break;
     case 'h':
      usage(argv[0]);
      return false;
    case 'v':
      GBLS.verbose++;
      break;
    default:
      usage(argv[0]);
      return false;
    }
  } 

  int anum=argc-optind;
  char **args=&(argv[optind]);
    
  if (anum < 1) {
    usage(argv[0]);
    return false;
  }
  
  for (int i=0; i<anum; i++) {
    VLPRINT(3, "args[%d]=%s\n", i, args[i]);
    if (!GBLSAddCmd(args[i])) {
      return false;
    }
  }
  
  if (verbose(1)) GBLSDump(stderr); 
  return true;
}

#if 0 
extern void
delaysec(double delay)
{
  struct timespec thedelay     = { .tv_sec = 0, .tv_nsec = 0 };
  struct timespec ndelay       = { .tv_sec = 0, .tv_nsec = 0 };
  struct timespec nrem         = { .tv_sec = 0, .tv_nsec = 0 };
  if (delay >= 1.0) {
    thedelay.tv_sec = (time_t)delay;
    delay = delay - thedelay.tv_sec;
  }
  thedelay.tv_nsec = delay * (double)NSEC_IN_SECOND;
  ndelay = thedelay; 
  while (nanosleep(&ndelay,&nrem)<0) {
      ndelay = nrem;
  }
}
#endif

extern void
fdSetnonblocking(int fd)
{
  int flags;
  
  flags = fcntl(fd, F_GETFD);
  assert(flags!=-1);
  flags |= O_NONBLOCK;
  flags = fcntl(fd, F_SETFD);  
}

evnthdlrrc_t
bcsttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  tty_t *tty = obj;
  int     fd = tty->mfd;
  assert(obj == &GBLS.btty);
  VLPRINT(3,"START: BCSTTY: tty(%p):%s(%s) fd:%d evnts:0x%08x\n", tty, 
	  tty->link, tty->path, fd, evnts);
  if (evnts & EPOLLIN) {
    char c;
    // pretend we are reading characters for the slowest command
    VLPRINT(3, "btty(%p)\n", obj);
    int n = ttyReadChar(&GBLS.btty, &c,
			&(GBLS.slowestcmd->lastwrite),
			GBLS.slowestcmd->delay);
    if (n) {
      if (verbose(2)) {
	asciistr_t charstr;
	ascii_char2str((int)c, charstr);
	VPRINT("---> BCSTTY: START: EIN: tty(%p):%s(%s) fd:%d evnts:0x%08x\n"
	       "ttyReadChar:    %p:%s(%s) fd:%d n:%d: %02x(%s)\n",
	       tty, tty->link, tty->path, fd, evnts,
	       tty, tty->link, tty->path, fd, n, c, charstr);
      }
      n=GBLSCmdsWriteChar(c);
      VLPRINT(2, "<--- BCSTTY: END: EIN: tty(%p):%s(%s) fd:%d evnts:0x%08x n=%d\n",
	      tty, tty->link, tty->path, fd, evnts, n);
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
  VLPRINT(3, "END : BCSTTY: tty(%p):%s(%s) fd:%d evnts:0x%08x\n",
	      tty, tty->link, tty->path, fd, evnts);
  return EVNT_HDLR_SUCCESS;
}

#define MAX_EVENTS 1024
// epoll code is based on example from the man page
static bool
theLoop()
{
  bool rc;
  int epollfd;
  struct epoll_event ev;

  // create the kernel poll object
  {
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
      perror("epoll_create1");
      return false;
    }
  }
  // register broadcast pty poll set 
  {
    evntdesc_t bttyfded = { .hdlr=bcsttyEvent, &GBLS.btty }; 
    // we use non blocking reads and watch for EAGAIN on writes
    // to see when we have exhausted the kernel tty port buffer
    fdSetnonblocking(GBLS.btty.mfd);
    ev.data.ptr = &bttyfded;
    // edge triggered
    ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // | EPOLLET;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, GBLS.btty.mfd, &ev) == -1 ) {
      perror("epoll_ctl: GBLS.btty");
      return false;
    }
  }

  // register initial commands to the poll set
  {
    cmd_t *cmd, *tmp;
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      ev.data.ptr = &cmd->pidfded;
      // edge triggered
      ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR | EPOLLET;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, cmd->pidfd, &ev) == -1 ) {
	perror("epoll_ctl: cmd->pidfd");
	return false;
      }
      // level triggered 
      ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR;
      ev.data.ptr = &cmd->cmdttyed;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, cmd->cmdtty.mfd, &ev) == -1 ) {
	perror("epoll_ctl: cmd->cmdtty.fd");
	return false;
      }
      ev.data.ptr = &cmd->cltttyed;
      if (epoll_ctl(epollfd, EPOLL_CTL_ADD, cmd->clttty.mfd, &ev) == -1 ) {
	perror("epoll_ctl: cmd->clttty.mfd");
	return false;
      }
    }
  }

  // detect and process events
  for (;;) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      if (errno == EINTR) continue;
      rc = false;
      goto done;
    }
    for (int n = 0; n < nfds; ++n) {
      evnthdlrrc_t erc;
      evntdesc_t *ed = events[n].data.ptr;
      uint32_t evnts = events[n].events;
      assert(ed);
      VLPRINT(3, "%d/%d: ed:%p (.hdlr=0x%p .obj=Ox%p) evnts:0x%08x\n",
	      n, nfds, ed, ed->hdlr, ed->obj, evnts);
      assert(ed->hdlr);
      // call handler registered for this event source 
      erc = ed->hdlr(ed->obj, evnts, epollfd);
      if (erc == EVNT_HDLR_EXIT_LOOP) {
	VLPRINT(1, "eventhandler returned exiting loop rc"
		" hdlr:%p obj:0x%p evnts:%08x\n", ed->hdlr, ed->obj, evnts);
	rc = true;
	goto done;
      } else if (erc == EVNT_HDLR_FAILED) {
	EPRINT("event handler failed hdlr:%p obj:0x%p evnts:%08x\n",
	       ed->hdlr, ed->obj, evnts);
	rc = false;
	goto done;
      }
    }
  }
  
  // Exit logic
 done:
  return rc;
}

extern
void cleanup(void)
{
  VPRINT("%s", "exiting\n");
  ttyCleanup(&GBLS.btty);
  {
    cmd_t *cmd, *tmp;
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      VPRINT("cleanup up cmd %s\n", cmd->name);
      cmdCleanup(cmd);
      HASH_DEL(GBLS.cmds, cmd);
      free(cmd);
    }
  }
}

void
signalhandler(int num)
{
  VLPRINT(1, "num:%d\n", num);
  EEXIT();
}

int main(int argc, char **argv)
{
  // initialize all global data structures to ensure
  // correct cleanup behavior incase of early termination
  // the read delay of the broadcast tty will be set to the
  // largest delay of any of the commands
  if (!ttyInit(&GBLS.btty, DEFAULT_BTTY_LINK)) EEXIT();
  
  atexit(cleanup);
  signal(SIGTERM, signalhandler);
  signal(SIGINT, signalhandler);

  if (!argsParse(argc, argv)) EEXIT();

  // create the broadcast tty
  if (!ttyCreate(&GBLS.btty, true)) EEXIT();

  if (!theLoop()) EEXIT();
  
  return EXIT_SUCCESS;
}

asciistr_t ascii_nonprintable[32] = {
  [0] = "0", [1] = "SOH", [2] = "STX", [3] = "ETX", [4] = "EOT", [5] = "ENQ",
  [06] = "ACK", [7] = "\\a", [8] = "\b", [9] = "\\t", [10] = "\\n", [11] = "\\v",
  [12] = "\\f", [13] = "\\r", [14] = "SO", [15] = "SI", [16] = "DLE",
  [17] = "DC1", [18] = "DC2", [19] = "DC3", [20] = "DC4", [21] = "NAK",
  [22] = "SYN", [23] = "ETB", [24] = "CAN", [25] = "EM", [26] = "SUB",
  [27] = "ESC", [28] = "FS", [29] = "GS", [30] = "RS", [31] = "US"
};
