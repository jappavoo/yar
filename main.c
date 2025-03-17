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
  *name    = "thecmd";
  *cmdline = cmdstr;
  *delay   = 0.0;
  *ttylink = *name;
  *log     = NULL;
  return true;
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
    if (!cmdCreate(cmd)) {
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
bcastfdEventHandler(void *obj, uint32_t evnts)
{
  assert(obj == &GBLS.btty);
  VLPRINT(2, "btty FD  evnts:%08x", evnts);
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

#define MAX_EVENTS 1024
// epoll code is based on example from the man page
static bool
theLoop()
{
  int epollfd;
  struct epoll_event ev, events[MAX_EVENTS];
  evnthdlrrc_t erc;
  evntdesc_t *ed;
  uint32_t evnts;
  
  epollfd = epoll_create1(0);
  assert(epollfd != -1);
  evntdesc_t bttyfded = { .hdlr=bcastfdEventHandler, &GBLS.btty };
  
  // epoll man page recommends non-blocking io for edgetriggered use 
  fdSetnonblocking(GBLS.btty.fd);
  ev.events   = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
  ev.data.ptr = &bttyfded;

  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, GBLS.btty.fd, &ev) == -1 ) {
    perror("epoll_ctl: GBLS.btty");
    return false;
  }
    
  for (;;) {
    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      if (errno == EINTR) continue;
      return false;
    }
    for (int n = 0; n < nfds; ++n) {
      ed = events[n].data.ptr;
      evnts = events[n].events;
      assert(ed);
      VLPRINT(2, "ed: 0x%p (.hdlr=0x%p .obj=Ox%p) evnts:0x%08x\n",
	      ed, ed->hdlr, ed->obj, evnts);
      assert(ed->hdlr);
      // call handler registered for this event source
      erc = ed->hdlr(ed->obj, evnts);
      if (erc == EVNT_HDLR_EXIT_LOOP) {
	VLPRINT(1, "eventhandler returned exiting loop rc"
		" hdlr:0x%p obj:0x%p evnts:%08x\n", ed->hdlr, ed->obj, evnts);
	break;
      } else if (erc == EVNT_HDLR_FAILED) {
	EPRINT("event handler failed hdlr:0x%p obj:0x%p evnts:%08x\n",
	       ed->hdlr, ed->obj, evnts);
	break;
      }
    }
  }
  return erc;
}

extern 
void cleanup(void)
{
  VPRINT("%s", "exiting\n");
  ttyCleanup(&GBLS.btty);
  {
    cmd_t *cmd, *tmp;
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      VPRINT("%s", "cleanup up cmd:");
      if (verbose(1)) {
	cmdDump(cmd, stderr, "\n  ");
      }
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
  if (!ttyCreate(&GBLS.btty)) EEXIT();

  if (!theLoop()) EEXIT();
  
  return EXIT_SUCCESS;
}
