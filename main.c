//012345678901234567890123456789012345678901234567890123456789012345678901234567
#include "yar.h"
#include <getopt.h>
#include <signal.h>

#define DEFAULT_BTTY_LINK "btty"

globals_t GBLS = {
  .verbose = 0,
  .cmds    = NULL
};

static void
usage(char *name)
{
  fprintf(stderr,
	  "USAGE: %s [-v] <name,pty,log,delay,cmd> [<name,pty,log,delay,cmd>]\n"
	  " Yet Another Relay\n",
	  name);
}

static void
GBLSDump(FILE *f)
{
  fprintf(f, "GBLS.verbose=%d\n", GBLS.verbose);
  ttyDump(&GBLS.btty, stderr, "GBLS.btty: ");
  fprintf(f, "GBLS.cmds:");
  {
      cmd_t *cmd, *tmp;
      HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
	cmdDump(cmd, stderr, "\n  ");
      }
  }
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
    // change log to NULL
    *delay   = 0.0;
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
      EPRINT("Failed to initCmd(0x%p,%s,%s,%f,%s,%s)", cmd, name, cmdline,
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
    if (n) cnt++;
  }
  return cnt;
}

static bool
argsParse(int argc, char **argv)
{
    char opt;
    
    while ((opt = getopt(argc, argv, "b:hv")) != -1) {
    switch (opt) {
    case 'b':
      GBLS.btty.link=optarg;
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
bcastfdEventHandler(void *obj, uint32_t evnts, int epollfd)
{
  assert(obj == &GBLS.btty);
  VLPRINT(2, "btty FD  evnts:%08x", evnts);
  if (evnts & EPOLLIN) {
    VLPRINT(2,"EPOLLIN(%x)\n", EPOLLIN);
    char c;
    int n = ttyReadChar(&GBLS.btty, &c);
    if (verbose(2)) {
      if (n) fprintf(stderr, "btty: %c(%02x)\n", c, c);
      else fprintf(stderr, "bgtty: n=%d\n", n);
    }
    GBLSCmdsWriteChar(c);
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
    evntdesc_t bttyfded = { .hdlr=bcastfdEventHandler, &GBLS.btty }; 
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
      VLPRINT(2, "%d/%d: ed: 0x%p (.hdlr=0x%p .obj=Ox%p) evnts:0x%08x\n",
	      n, nfds, ed, ed->hdlr, ed->obj, evnts);
      assert(ed->hdlr);
      // call handler registered for this event source 
      erc = ed->hdlr(ed->obj, evnts, epollfd);
      if (erc == EVNT_HDLR_EXIT_LOOP) {
	VLPRINT(1, "eventhandler returned exiting loop rc"
		" hdlr:0x%p obj:0x%p evnts:%08x\n", ed->hdlr, ed->obj, evnts);
	rc = true;
	goto done;
      } else if (erc == EVNT_HDLR_FAILED) {
	EPRINT("event handler failed hdlr:0x%p obj:0x%p evnts:%08x\n",
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
