//012345678901234567890123456789012345678901234567890123456789012345678901234567
#include "yar.h"
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>

#define DEFAULT_BCSTTTY_LINK "bcsttty"

// forward declartion
static evnthdlrrc_t monEvent(void *obj, uint32_t evnts, int epollfd);

globals_t GBLS = {
  .mon                = { .ed = {.obj = &(GBLS.mon), .hdlr = &monEvent },
			  .n = 0 }, 
  .slowestcmd         = NULL,
  .verbose            = 0,
  .cmds               = NULL,
  .linebufferbcst     = false,
  .prefixbcst         = false,
  .bcstflg            = false,
  .defaultcmddelay    = 0.0,
  .restartcmddelay    = 0.0,
  .errrestartcmddelay = 10.0
};

static void
usage(char *name)
{
  fprintf(stderr,
	  "USAGE: %s [-v] [-b broadcast tty link path]"
	  " [-d <default read delay sec>] [-l] [-p]"
	  " <name,pty,log,delay,cmd> [<name,pty,log,delay,cmd>]\n"
	  " Yet Another Relay\n"
	  "-l enable line buffering of output from commands to broadcast tty\n"
	  "-p enable prefix output from commands to broadcat tty with command"
	  " name\n",
	  name);
}

static void
GBLSDump(FILE *f)
{
  fprintf(f, "GBLS.verbose=%d\n", GBLS.verbose);
  fprintf(f, "GBLS.defaultcmddelay=%f\n", GBLS.defaultcmddelay);
  fprintf(f, "GBLS.restartcmddelay=%f\n", GBLS.restartcmddelay);
  fprintf(f, "GBLS.errrestartcmddelay=%f\n", GBLS.errrestartcmddelay);
  ttyDump(&GBLS.bcsttty, stderr, "GBLS.bcsttty: ");
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
GBLSAddCmd(char *cstr, cmd_t **cmdptr)
{
  char *cmdstr,*name, *cmdline, *ttylink, *log;
  double delay;
  cmd_t *cmd;
  size_t n;
  
  // WE ASSUME cmdstr is a properly null terminated string!
  n=strlen(cstr);
  if (n==0) {
    return false;
  }
  n=n+1; // add one for null termination
  cmdstr=malloc(n);
  strncpy(cmdstr, cstr, n);
  assert(cmdstr[n]==0);
  
  if (!cmdstrParse(cmdstr, &name, &cmdline, &delay, &ttylink,
		   &log)) return false;
  // check to see if name is already used
  HASH_FIND_STR(GBLS.cmds, name, cmd);
  if (cmd == NULL) {
    // new command
    cmd=malloc(sizeof(cmd_t));
    if (!cmdInit(cmd, cmdstr, name, cmdline, delay, ttylink, log)) {
      EPRINT("Failed to initCmd(%p,%s,%s,%f,%s,%s)", cmd, name, cmdline,
	     delay, ttylink, log);
      free(cmdstr);
      free(cmd);
      return false;
    }

    if (!cmdCreate(cmd)) { 
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
    if (cmdptr) *cmdptr = cmd;
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
    int opt;
    
    while ((opt = getopt(argc, argv, "b:d:e:hlpr:v")) != -1) {
    switch (opt) {
    case 'b':
      GBLS.bcsttty.link=optarg;
      break;
    case 'd':
      errno = 0;
      GBLS.defaultcmddelay = strtod(optarg, NULL);
      if (errno != 0) {
	perror("bad delay value");
	return  false;
      }
      break;
    case 'e':
      errno = 0;
      GBLS.errrestartcmddelay = strtod(optarg, NULL);
      if (errno != 0) {
	perror("bad restart error delay value");
	return  false;
      }
      break;      
    case 'h':
      usage(argv[0]);
      return false;
    case 'l' :
      GBLS.linebufferbcst = true;
      break;
    case 'p':
      GBLS.prefixbcst = true;
      break;
    case 'r':
      errno = 0;
      GBLS.restartcmddelay = strtod(optarg, NULL);
      if (errno != 0) {
	perror("bad restart error delay value");
	return  false;
      }
      break;      
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
    if (!GBLSAddCmd(args[i],NULL)) {
      return false;
    }
  }
  
  if (anum>1) GBLS.bcstflg=true;

  if (verbose(1)) GBLSDump(stderr); 
  return true;
}

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

extern void
fdSetnonblocking(int fd)
{
  int flags;
  
  flags = fcntl(fd, F_GETFL);
  assert(flags!=-1);
  flags |= O_NONBLOCK;
  flags = fcntl(fd, F_SETFL, flags);
  assert(flags!=-1);
}


void 
bcstttyRegisterEvents(int epollfd)
{
  if (GBLS.bcstflg) ttyRegisterEvents(&(GBLS.bcsttty), epollfd);
}

evnthdlrrc_t
bcstttyNotify(void *obj, uint32_t mask, int epollfd)
{
  tty_t *this = obj;
  cmd_t *cmd, *tmp;

  switch (mask) {
  case IN_OPEN:
    if (verbose(1)) {
      fprintf(stderr, "OPEN: bcsttty:%s(%s) count:%d\n", 
	      this->link, this->path, this->opens);
    }
    // poke all commands to ensure they are started 
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      if (cmdStart(cmd, true, epollfd, 0.0)) {
	VPRINT("%s started pidfd=%d pid=%d\n", cmd->name, cmd->pidfd, cmd->pid);
      }
    }
    mask = mask & ~IN_OPEN;
    break;
  case IN_CLOSE_WRITE:
  case IN_CLOSE_NOWRITE:
    if (verbose(1)) {
      fprintf(stderr, "CLOSE: bcstty:%s(%s) count:%d\n", 
	      this->link, this->path, this->opens);
    }
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      if (cmdStop(cmd, epollfd, false)) {
	VPRINT("%s started pidfd=%d pid=%d\n", cmd->name, cmd->pidfd, cmd->pid);
      }
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

evnthdlrrc_t
bcstttyEvent(void *obj, uint32_t evnts, int epollfd)
{
  tty_t *tty = obj;
  int     fd = tty->dfd;
  assert(obj == &GBLS.bcsttty);
  VLPRINT(3,"START: BCSTTY: tty(%p):%s(%s) fd:%d evnts:0x%08x\n", tty, 
	  tty->link, tty->path, fd, evnts);
  if (evnts & EPOLLIN) {
    char c;
    // pretend we are reading characters for the slowest command
    VLPRINT(3, "bcsttty(%p)\n", obj);
    int n = ttyReadChar(&GBLS.bcsttty, &c,
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
      VLPRINT(2, "<--- BCSTTY: END: EIN: tty(%p):%s(%s) fd:%d evnts:0x%08x "
	      "n=%d\n", tty, tty->link, tty->path, fd, evnts, n);
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

void
bcstttyCreate() {
  if (GBLS.bcstflg) {
    evntdesc_t ed  = { .obj = &(GBLS.bcsttty), .hdlr = bcstttyEvent };
    evntdesc_t ned = { .obj = &(GBLS.bcsttty), .hdlr = bcstttyNotify };
    if (!ttyCreate(&GBLS.bcsttty, ed, ned, true)) EEXIT();
  }
}

// Monitor CLI code 
typedef int (*moncmd_t)(int,int);

int
monExit(int args, int epollfd)
{
  VLPRINT(3, "%s:start: args=%d epollfd=%d\n", __func__, args, epollfd);
  cleanup();
  exit(EXIT_SUCCESS);
  VLPRINT(3, "%s:end: args=%d epollfd=%d\n", __func__, args, epollfd);
  return 0;
}

int
monAdd(int args, int epollfd)
{
  int rc=0;
  VLPRINT(3, "%s:start: args=%d epollfd=%d\n", __func__, args, epollfd);
  if (args != 0) {
    char *cmdstr=&GBLS.mon.line[args];
    cmd_t *cmd;
    if (GBLSAddCmd(cmdstr, &cmd)) {
      if (!cmdRegisterttyEvents(cmd, epollfd)) {
	EPRINT("failed to register ttyEvents (%s)\n", cmdstr);
	rc = -1;
      } else {
	if (GBLS.bcstflg == false && (HASH_COUNT(GBLS.cmds) > 1)) {
	  // incase we now have more than one command we might need
	  // to create the broadcast tty 
	  GBLS.bcstflg = true;
	  bcstttyCreate();
	  bcstttyRegisterEvents(epollfd);
	} else {
	  // if the broadcast tty is currently open then start command
	  // immediately
	  if (GBLS.bcsttty.opens>0 && cmdStart(cmd, true, epollfd, 0.0)) {
	    VPRINT("%s started pidfd=%d pid=%d\n", cmd->name, cmd->pidfd, cmd->pid);
	  }
	}
      }
    } else {
      EPRINT("failed to add %s\n", cmdstr);
      rc = -1;
    }
  } else{
    fprintf(stderr, "USAGE: add <cmd string>\n");
    rc = -1;
  }
  VLPRINT(3, "%s:end: args=%d epollfd=%d\n", __func__, args, epollfd);
  return rc;
}

int
monDel(int args, int epollfd)
{
  char *name;
  cmd_t *cmd;
  
  if (args == 0) {
    fprintf(stderr, "USAGE: del <cmd name>\n");
    return -1;
  }
  
  name = &GBLS.mon.line[args];

  HASH_FIND_STR(GBLS.cmds, name, cmd);
  if (cmd == NULL) {
    fprintf(stderr, "%s is not a current command.\n", name);
    return -1;
  }

  VPRINT("cleanup up cmd %s\n", cmd->name);
  cmdCleanup(cmd);
  HASH_DEL(GBLS.cmds, cmd);
  free(cmd);
  
  return 0;
}

int
monList(int args, int epollfd)
{
  cmd_t *cmd, *tmp;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    printf("%s %s %s\n", cmd->name,
	   cmd->clttty.link, cmd->cmdline);
  }
  return 0;
}

struct MonCmdDesc {
  char *name;
  moncmd_t cmd;
} MonCmds[] = {
  {.name = "exit", .cmd=monExit },
  {.name = "add",  .cmd=monAdd },
  {.name = "del",  .cmd=monDel },
  {.name = "list", .cmd=monList },
  {.name = NULL,   .cmd=NULL }            // mark end of command array
};

static void
monProcess(int epollfd)
{
  char *cmd = GBLS.mon.line;
  int   n    = GBLS.mon.n;
  int   i, j, args=0;
  
  if (n<=0) return;
  for (i=0; i<MON_LINELEN; i++) {
    if (cmd[i] == ' ') {
      cmd[i]='\0';
      args=i+1;
      break;
    }
    if (cmd[i]=='\0') break;
  }

  // we did not find a null or new line??
  if (cmd[i]!=0) {
    EPRINT("mon: invalid cmd line: i=%d\n",i);
    return;
  }
  
  // check for command and execute 
  for (j=0; MonCmds[j].cmd != NULL; j++) {
    if (strncmp(cmd, MonCmds[j].name, i)==0) {
      int rc = MonCmds[j].cmd(args, epollfd);
      if (rc==0) fprintf(stderr, "OK\n");
      else fprintf(stderr, "FAILED\n");
      return;
    }
  }
  
  fprintf(stderr,"yar: %s: command not found\n", cmd);
}

static evnthdlrrc_t
monEvent(void *obj, uint32_t evnts, int epollfd)
{
  assert(obj == &GBLS.mon);
  int     fd = STDIN_FILENO;

  VLPRINT(3,"START: mon: fd:%d evnts:0x%08x\n", fd, evnts);
  if (evnts & EPOLLIN) {
    int curn = GBLS.mon.n;
    // leave room for null termination
    int n = read(STDIN_FILENO, &GBLS.mon.line[curn], MON_LINELEN - curn);
    assert(n>=0);
    curn+=n;
    
    assert(curn <= MON_LINELEN);
    if (curn == MON_LINELEN && GBLS.mon.line[curn]!='\n') {
      EPRINT("exceeded MON_LINELEN=%d...ignoring\n", MON_LINELEN);
      GBLS.mon.n = 0;
    } else {
      GBLS.mon.n = curn;
      if (GBLS.mon.line[curn-1]=='\n') {
	GBLS.mon.line[curn-1]='\0';
	VLPRINT(3,"mon: line: %s", GBLS.mon.line);
	monProcess(epollfd);
	GBLS.mon.n = 0;
      }
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
  VLPRINT(3, "END: mon: fd:%d evnts:0x%08x\n", fd, evnts);
  return EVNT_HDLR_SUCCESS;
}

  
static bool
monitorRegisterEvents(mon_t *this, int epollfd)
{
  const int fd=STDIN_FILENO;
  struct epoll_event ev;

  ASSERT(this);
  fdSetnonblocking(fd);
  
  ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // Level 
  ev.data.ptr = &this->ed;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
    perror("epoll_ctl: monitor failed");
    return false;
  }
  return true;
}

#define MAX_EVENTS 1024
// epoll code is based on example from the man page
static bool
theLoop()
{
  bool rc;
  int epollfd;
  // create the kernel event poll object
  {
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
      perror("epoll_create1");
      return false;
    }
  }

  // register for the stdin monitor interface events
  monitorRegisterEvents(&(GBLS.mon), epollfd);
  
  // register for the broadcast client interface events
  bcstttyRegisterEvents(epollfd);
  
  // cmd now register for events when started as part of lazy start
  // register for the events for all the initial commands
  {
    cmd_t *cmd, *tmp;
    // iterate over the commmands in the commands hash table (created
    // and stored in the hash table during command line arg procesing)
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      if (!cmdRegisterttyEvents(cmd, epollfd)) return false;
    }
  }
  
  // loop: detect events and dispatch handlers
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
  if (GBLS.bcstflg) ttyCleanup(&GBLS.bcsttty);
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
  if (!ttyInit(&GBLS.bcsttty, DEFAULT_BCSTTTY_LINK)) EEXIT();
  
  atexit(cleanup);
  signal(SIGTERM, signalhandler);
  signal(SIGINT, signalhandler);

  if (!argsParse(argc, argv)) EEXIT();

  // create the broadcast tty
  bcstttyCreate();
  
  if (!theLoop()) EEXIT();
  
  return EXIT_SUCCESS;
}

asciistr_t ascii_nonprintable[32] = {
  [0] = "NUL", [1] = "SOH", [2] = "STX", [3] = "ETX", [4] = "EOT", [5] = "ENQ",
  [06] = "ACK", [7] = "\\a", [8] = "\\b", [9] = "\\t", [10] = "\\n", [11] = "\\v",
  [12] = "\\f", [13] = "\\r", [14] = "SO", [15] = "SI", [16] = "DLE",
  [17] = "DC1", [18] = "DC2", [19] = "DC3", [20] = "DC4", [21] = "NAK",
  [22] = "SYN", [23] = "ETB", [24] = "CAN", [25] = "EM", [26] = "SUB",
  [27] = "ESC", [28] = "FS", [29] = "GS", [30] = "RS", [31] = "US"
};
