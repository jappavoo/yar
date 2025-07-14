//012345678901234567890123456789012345678901234567890123456789012345678901234567
#include "yar.h"
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>

#define DEFAULT_BCSTTTY_LINK "bcsttty"

// forward declartions
static evnthdlrrc_t monEvent(void *obj, uint32_t evnts, int epollfd);

globals_t GBLS;

static int monExit(int, int);
static int monAdd(int, int);
static int monIdleExit(int, int);
static int monDel(int, int);
static int monList(int, int);
static int monToggleSilent(int, int) {
  GBLS.mon.silent = !GBLS.mon.silent;
  if (GBLS.mon.silent) { monprintf("monitor silent: true\n"); }
  else { monprintf("monitor silent: false\n"); }
  return 0;
}
static int monToggleRestart(int, int) {
  GBLS.restart = !GBLS.restart;
  if (GBLS.restart) { monprintf("global restart commands: true\n"); }
  else { monprintf("global restart commands: false\n"); }
  return 0;
}
static int monToggleLB(int, int) {
  GBLS.linebufferbcst = !GBLS.linebufferbcst;
  if (GBLS.linebufferbcst) { monprintf("line buffer broadcast output: "
				       "true\n"); }
  else { monprintf("line buffer broadcast output: false\n"); }
  return 0;
}
static int monTogglePrefix(int, int) {
  GBLS.prefixbcst = !GBLS.prefixbcst;
  if (GBLS.prefixbcst) { monprintf("prefix broadcast output: true\n"); }
  else { monprintf("prefix broadcast output : false\n"); }
  return 0;
}
static int monVerboseInc(int, int) {
  GBLS.verbose++;
  if (GBLS.verbose<0) GBLS.verbose=0;
  monprintf("verbosity: %d\n", GBLS.verbose); 
  return 0;
}
static int monVerboseDec(int, int) {
  GBLS.verbose--;
  if (GBLS.verbose<0) GBLS.verbose=0;
  monprintf("verbosity: %d\n", GBLS.verbose);
  return 0;
}
static int monHelp(int, int);

struct MonCmdDesc {
  char *name;
  char *usage;
  moncmd_t cmd;
} MonCmds[] = {
  {.name = "help", .usage="display 'yar' usage documentation",
   .cmd=monHelp },
  {.name = "add",  .usage="add command line instance requires command "
                          "specification:\n"
                          "\t\t<name,pty,log,delay,command line>",
   .cmd=monAdd },
  {.name = "a",    .usage=NULL, .cmd=monAdd },
  {.name = "del",  .usage="delete command line instance requires command name:"
                          "\n\t\t<name>",
   .cmd=monDel },
  {.name = "d",  .usage=NULL, .cmd=monDel },
  {.name = "list", .usage="[-l|-d] list current command line instances.\n"
                          "\t\t-l long listing, -d debug dump.",
   .cmd=monList },
  {.name = "l", .usage=NULL, .cmd=monList },
  {.name = "exit", .usage="exit yar",
   .cmd=monExit },
  {.name = "e", .usage=NULL, .cmd=monExit },
  {.name = "idleexit", .usage="stop command line restarts and exit when command"
                              " lines terminate",
   .cmd=monIdleExit },
  {.name="ie", .usage=NULL, .cmd=monIdleExit },
  {.name="restart", .usage="toggle restart behavior",
   .cmd=monToggleRestart },
  {.name="r", .usage=NULL, .cmd=monToggleRestart },
  {.name = "quit", .usage=NULL, .cmd=monExit },
  {.name = "q", .usage=NULL, .cmd=monExit },
  {.name = "line", .usage="toggle broadcast tty line buffering",
   .cmd = monToggleLB },
  {.name = "lb", .usage=NULL, .cmd = monToggleLB },  
  {.name = "pre", .usage="toggle broadcast tty prefixing",
   .cmd = monTogglePrefix },  
  {.name = "p", .usage=NULL,
   .cmd = monTogglePrefix },
  {.name = "v+", .usage="increase debug verbosity",
   .cmd = monVerboseInc },
  {.name = "v-", .usage="decrease debug verbosity",
   .cmd = monVerboseDec },
  {.name = "silent", .usage="unsilence/silence monitor output",
   .cmd = monToggleSilent },
  {.name = NULL,   .cmd=NULL }            // mark end of command array
};

static void
monusage(FILE *fp)
{
  if (fp == GBLS.mon.fileptr && GBLS.mon.tty.opens == 0) return;
  for (int i=0; MonCmds[i].cmd != NULL; i++) {
    struct MonCmdDesc *cmd = &(MonCmds[i]);
    if (cmd->usage != NULL) {
      if (fp == GBLS.mon.fileptr) {
	monprintf( "\t'%s'\t%s\n", cmd->name, cmd->usage);
      } else {
	fprintf(fp, "\t'%s'\t%s\n", cmd->name, cmd->usage);
      }
    }
  }
}

static void
usage(char *name, FILE *fp)
{
  if (fp == GBLS.mon.fileptr && GBLS.mon.tty.opens == 0) return;
  fprintf(fp,
"USAGE: %s [Global Options] [[name,pty,log,delay,command line]...]\n"
"Yet Another Relay (yar)\n"
"-----------------------\n\n"	  
  "'yar' strives to be a generic \"command\" manager that supports\n"
  "i/o broadcasting and coalescing.  Its goal is to make it easier\n"
  "to manage and automate a set of parrallel operations.  For example,\n"
  "you can use yar to control several remote systems by specifying a\n"
  "set of ssh command that it will automatically start (and restart).\n"
  "For each ssh it will create a tty file that you can read and write\n"
  "data to interact with one of the ssh instances.  However, it will\n"
  "also create a broadcast tty that when written to will send the data\n"
  "to all the ssh sessions (starting them if needed).  'yar' will also\n" 
  "coalese the output received from all the commands and write it\n"
  "to the broadcast tty.\n\n"
	  
  "By default 'yar' it will not disambiguate the output,  rather it\n"
  "will write the raw data from any command immediately to the\n"
  "broadcast tty, leaving that task of demuliplexing/demangling it to\n"
  "you.  This allows you to use commands who's output might already\n"
  "include enough information to be demultiplex.  However, to support\n"
  "the common use of commands that produce ascii line oriented data\n"
  "you can use the '-l' and '-p'  flags.  '-l' tells 'yar' to line\n"
  "buffer the output from a command and only write complete lines at\n"
  "a time from a to the broadcast tty.  the '-l' flag tells 'yar' to\n"
  "prefix the data written to the broadcast tty with the 'name' of the\n"
  "command. Using the '-l' and '-p' flag together let's you easily\n"
  "decompose the output from parallel command in a meaninful way.\n"
  "Command 'names' are discussed below.\n\n"

  "It is important to remember that since 'yar' also creates command\n"
  "specific ttys you can demultiplex the output by reading these ttys,\n"
  "or avoid broadcasting by writting to a specific command via its\n"
  "specific ttys.  'yar' is designed to be flexible and encourage\n"
  "creative use and prompt \"parallel\" thinking.\n\n"

  "Commands: At the core of 'yar' are commands.  Command can be an\n"
  "arbitrary shell command line.  To run the command line, yar will fork\n"
  "a child process and use the users default shell to execute the command.\n"
  "To specify you must use a 'yar' command specification. The syntax is\n"
  "as follows:\n\n"
	  
  "    <name>,[pty link name],[log],[delay],<command line>\n\n"
	  
  " <name>: is a required unique name you must provide to identify this\n"
  " command line instance.  Eg. \n"
  "           'csa2,,,,ssh csa2.bu.edu'\n"
  " would associate the name 'csa2'  with an instance of the command\n"
  " 'ssh csa2.bu.edu'.\n\n"
	  
  " [pty link name]: 'yar' will create a pty (see man pty) for the\n"
  " input and output of each command line instance. Additionally, 'yar'\n"
  " will create a symbolic link to the pty so that you can easily read\n"
  " and  write from and to the command line instance. By default the\n"
  " link will be created in the current directory you started yar in \n"
  " its file name will be the same as the <name> you gave the to the\n"
  " command line instance. Eg. in the example above\n"
  " 'csa2,,,,ssh csa2.bu.edu' the the link create will be name 'csa2'\n"
  " in the directory you started yar.  If you would like to overide this\n"
  " name you can specify it via the [pty link name] value of the\n"
  " specification.  Eg.  'csa2,/tmp/bu2tty,,,ssh csa2.bu.edu' would\n"
  " overide the default and path of the link to the pty will be\n"
  " '/tmp/bu2tty'.  Opening this pty, which can be done via its link,\n"
  " will start the command instance running if it is not already running.\n"
  " Eg. 'cat /tmp/bu2tty' will start the ssh and read and display any\n"
  " data it produces to you.  Similarly to send data to the instance you\n"
  " could do something like 'cat > /tmp/bu2tty' or \n"
  "'echo \"echo hello world\" > /tmp/bu2tty'.  A common and convient\n"
  " way to interactively work with the command line instance would be\n"
  " to use the 'socat' command.  Eg. 'socat - /tmp/bu2tty'.  This would\n"
  " allow you to work with command as if you has launched by hand.\n\n"

  " [log] If specified 'yar' will log all the data written to and read\n"
  " from the command (when either the command's pty or the broadcast pty\n"
  " is open). This is NOT YET IMPLEMENTED please consider adding \n"
  " support to this -- eg. write the code\n\n"

  " [delay] If specified a delay will be added between reading bytes\n"
  " from the command's pty that is [delay] seconds.  This value can be\n"
  " expressed as a floating point number eg. \n"
  "       'csa2,/tmp/bu2tty,,0.01,ssh csa2.bu.edu'\n"
  " specifies that the when data is written to the command line\n"
  " via its pty then a delay of .01 seconds will be added between\n"
  " reading each character.  This in turn will force the data written\n"
  " to the command to be written at a rate that reflects this delay.\n"
  " See the global -d option for the default behavour if the value this\n"
  " value is omitted from a command specification\n\n"

  " <command line> every command specification must include a shell\n"
  " command line to run.  For each specification yar will manage the\n"
  " execution of this command line as follows:\n"
  "  1. It will keep one instance of the command line running as long\n"
  "     as either the command's pty is one or the broadcast pty is open.\n"
  "  2. It will terminate the command when all opens of the command pty\n"
  "     have been closed.\n"
  "  3. If the command exits, with out 'yar' explicitly terminating it,\n"
  "     'yar' will automatically restart it.  If it exits failing exit\n"
  "     status (non-zero exit status) it will delay the restart\n"
  "     by the error restart delay (see global option '-e <delay>' below).\n"
  "     If it exits with a success exist status (zero exit status) 'yar'\n'"
  "     will delay the restart by the restart delay (see global option\n"
  "     '-r <delay>' below).\n"
  " In this way 'yar' can be used to effectively \"baby sit\" a command.\n"
  " If you hold the command's pty open (or the broadcast pty), 'yar'\n"
  " will ensure a single instance will be kept running respawing as need\n"
  " (trottled by the delay values specified using '-r' and '-e').\n"
  " Note: Each command specification represents an independent instance\n"
  "       that 'yar' manages. Howerver, what the command line is for each\n"
  "       instance is completely up to you.  As such you can have 'yar'\n"
  "       create and manage multiple instances of the same command line.\n"
  "       eg. both of these are valid uses of 'yar':\n"
  "         1.  yar -l -p 'bu1,,,,ssh bu' 'bu2,,,,ssh bu'\n"
  "         2.  yar -l -p 'bu1,,,,ssh csa1.bu.edu' "
  "'bu2,,,,ssh csa2.bu.edu'\n\n"
	  
  "Global Options:\n"
  " -h print this usage message\n"
  " -b <path> path name for broadcast tty link (default %s)\n"
  " -d <delay sec> default value to pace all tty reads and there by\n"
  "    trottle the rate at which data is written to commands.\n"
  "    For example, if you passed \"-d 1.25\", then by default bytes\n"
  "    will not be read faster than 1.25 seconds even if they are\n"
  "    This is a crude way tp pace the rate at which data is written to\n"
  "    commands. Note this value will be used if the per cmd delay\n"
  "    value is not specified. Reads broadcast tty will be paced to the\n"
  "    slowest value specified (default %f)\n"
  " -r <delay sec> delay between restarting a command that exits with\n"
  "    success (default %f) \n"
  " -e <delay sec> delay between restarting a command that exist with\n"
  "    failure (default %f)\n"
  " -l enable line buffering of output from commands when written to \n"
  "    the broadcast tty (note even if -l is specified data from a commnd\n"
  "    will NOT be line buffered to the command's pty rather only data\n"
  "    written to the broadcast tty will be line buffered.\n"
  " -m <diretory path> the directory in which the monitor tty link will\n"
  "    be created in.  The link's name is process id (pid) of yar.\n"
  " -p enable prefixing the output from commands written to the\n"
  "    broadcast tty with the specified name for the command.\n"
  " -s <string> this sting will be sent to the command line when\n"
  "    stopping it.  If specified a newline will always be prepended.\n"
  " -v increase debug message verbosity.  This option can be used\n"
  "    multiple times to the verbosity Eg. -v versus -vv etc.\n\n"

  "File System: In addition to controlling 'yar' via its command line\n"
  "arguments, 'yar' provides a file system interface.  Yar will create a\n"
  "directory named <pid>.fs when it starts the location of this directory\n"
  "by default will be in the working directory that 'yar' was started in.\n"
  "use the '-f <dir>' option to explicitly set the location.  In this\n"
  "in this directory you will find files that let you interact with the 'yar'\n"
	  "process.  The folling documents these files.\n",
	  	  name, DEFAULT_BCSTTTY_LINK,
	  GBLS.defaultcmddelay, GBLS.restartcmddelay, GBLS.errrestartcmddelay);
  yarfsUsage(fp);
	  
  fprintf(fp, 
  "\nMonitor: In addition to controlling 'yar' via its command line arguments\n"
  "'yar' provides a simple monitoring interface on a seperate tty.\n"
  "A link to this tty will be the pid of the yar process suffixed with\n"
  "'.mon' (eg. '12345.mon'). By default the link is created in the working\n"
  "directory from which yar was launched. This default can be\n"
  "overridden with the '-m' option.  The monitor interface provides a\n"
  "set of monitor commands that lets you control and inspect various\n"
  "aspects of the  running 'yar'.\n"
	  "Monitor commands:\n");
  monusage(fp);

}

static void
GBLSDump(FILE *f)
{
  if (f == GBLS.mon.fileptr && GBLS.mon.tty.opens == 0) return;
  fprintf(f, "GBLS.pid=%" PRIdMAX "\n", (intmax_t)GBLS.pid);
  fprintf(f, "GBLS.verbose=%d\n", GBLS.verbose);
  fprintf(f, "GBLS.logpath=%s GBLS.logfile=%p\n", GBLS.logpath, GBLS.logfile);
  fprintf(f, "GBLS.stopstr=%s\n", GBLS.stopstr);
  fprintf(f, "GBLS.defaultcmddelay=%f\n", GBLS.defaultcmddelay);
  fprintf(f, "GBLS.restartcmddelay=%f\n", GBLS.restartcmddelay);
  fprintf(f, "GBLS.errrestartcmddelay=%f\n", GBLS.errrestartcmddelay);
  fprintf(f, "GBLS: restart=%d linebufferbst:%d prefixbcst:%d bcstflg:%d "
	  "exitonidle:%d keeplog:%d\n",
	  GBLS.restart, GBLS.linebufferbcst, GBLS.prefixbcst, GBLS.bcstflg,
	  GBLS.exitonidle, GBLS.keeplog);
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

static bool checkpath(char *path, int type)
{
  struct stat st;
  int rc = stat(path, &st);
  if ( rc == 0) {
    if (type) {
      if ((st.st_mode & S_IFMT)==type) {
	return true;
      } else {
	return false;
      }
    }
    return true;
  } 
  return false;
}

// modifies the cmdstr string (places nulls at appopriate points)
static bool
cmdspecParse(char *cmdstr, char **name, char **cmdline,
	    double *delay, char **ttylink, char **log, FILE *f)
{
  char  *orig=NULL, *nptr=NULL; // next token pointer, original cmdstr
  bool rc=true;
  
  orig = strdup(cmdstr);
  
  nptr = strsep(&cmdstr, ",");  // parse name
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT(f, "Bad command name: %s\n", orig);
    rc = false;
    goto done;
  }
  *name=nptr;
  
  nptr = strsep(&cmdstr, ",");     // parse ttylink
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT(f, "Bad ttylink: %s\n", orig);
    rc = false;
    goto done;
  }
  if (*nptr == 0) { // none specified leave NULL for cmd init to sort out
    *ttylink = NULL;
  } else {
    *ttylink = nptr;
  }

  // if the ttylink already exists something went wrong
  if (checkpath(*ttylink, 0)) {
    EPRINT(f, "%s already exists.\n", *ttylink);
    rc = false;
    goto done;
  }
  
  nptr = strsep(&cmdstr, ",");     // parse log
  if (nptr == NULL || cmdstr == NULL) {
    EPRINT(f, "Bad log path: %s\n", orig);
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
    EPRINT(f, "Bad delay: %s\n", orig);
    rc = false;
    goto done;
  }
  if (*nptr==0) { // none specified
     *delay = GBLS.defaultcmddelay;
  } else {
    errno = 0;
    *delay = strtod(nptr, NULL);
    if (errno != 0) {
      EPRINT(f, "bad delay value: errno=%d\n", errno);
      rc = false;
      goto done;
    }
  }

  // commandline is everthing that list left (avoid parsing incase command
  // line uses , itself (eg socat)
  if (cmdstr == NULL || *cmdstr == 0) {
    EPRINT(f, "bad cmdline: command must not be empty: %s\n", orig);
    rc = false;
    goto done;
  }
  *cmdline=cmdstr;
 done:
  if (orig) free(orig);
  return rc;
}

static bool
GBLSAddCmd(char *cstr, cmd_t **cmdptr, FILE *f)
{
  char *cmdstr,*name, *cmdline, *ttylink, *log;
  double delay;
  cmd_t *cmd;
  
  // WE ASSUME cmdstr is a properly null terminated string!
  cmdstr=strdup(cstr);
  
  if (!cmdspecParse(cmdstr, &name, &cmdline, &delay, &ttylink,
		   &log, f)) return false;
  // check to see if name is already used
  HASH_FIND_STR(GBLS.cmds, name, cmd);
  if (cmd == NULL) {
    // new command
    cmd=malloc(sizeof(cmd_t));
    if (!cmdInit(cmd, cmdstr, name, cmdline, delay, ttylink, log, false)) {
      EPRINT(f, "Failed to initCmd(%p,%s,%s,%f,%s,%s)", cmd, name, cmdline,
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
    EPRINT(f, "%s: command names must be unique. %s already used:",
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
      EPRINT(stderr, "  write returned: n=%d\n", n);
      NYI;
    }
    if (n) cnt++;
  }
  return cnt;
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
    EPRINT(stderr, "Unexpected notify case: mask:0x%x\n",
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
void monGreeting() { monprintf("yar[%d]> ", GBLS.pid); }

int
monIdleExit(int args, int epollfd)
{
  VLPRINT(3, "%s:start: args=%d epollfd=%d\n", __func__, args, epollfd);
  
  GBLS.exitonidle = true;
  // if we are idle (no commands) exit  now 
  if (HASH_COUNT(GBLS.cmds) == 0) {
    cleanup();
    exit(EXIT_SUCCESS);
  } else {
    // otherwise mark all current commands to delete themselves
    // if they exit ... last command to exit will also
    // cleanup yar
    cmd_t *cmd, *tmp;
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      cmd->deleteonexit = true;
    }
  }
  return 0;

  VLPRINT(3, "%s:end: args=%d epollfd=%d\n", __func__, args, epollfd);
}

int
monExit(int args, int epollfd)
{
  VLPRINT(3, "%s:start: args=%d epollfd=%d\n", __func__, args, epollfd);
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
    if (GBLSAddCmd(cmdstr, &cmd, GBLS.mon.fileptr)) {
      if (!cmdRegisterttyEvents(cmd, epollfd)) {
	monprintf("failed to register ttyEvents (%s)\n", cmdstr);
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
	    VPRINT("%s started pidfd=%d pid=%d\n", cmd->name, cmd->pidfd,
		   cmd->pid);
	  }
	}
      }
    } else {
      monprintf("failed to add %s\n", cmdstr);
      rc = -1;
    }
  } else{
    monprintf("USAGE: add <cmd string>\n");
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
    monprintf("USAGE: del <cmd name>\n");
    return -1;
  }
  
  name = &GBLS.mon.line[args];

  HASH_FIND_STR(GBLS.cmds, name, cmd);
  if (cmd == NULL) {
     monprintf("%s is not a current command.\n", name);
    return -1;
  }

  VPRINT("cleanup up cmd %s\n", cmd->name);
  cmdCleanup(cmd);
  HASH_DEL(GBLS.cmds, cmd);
  free(cmd);
  if (HASH_COUNT(GBLS.cmds) == 0 && GBLS.exitonidle) {
    cleanup();
    exit(EXIT_SUCCESS);
  }
  return 0;
}

int
monList(int args, int epollfd)
{
  char *flgs=&GBLS.mon.line[args];
  cmd_t *cmd, *tmp;
  bool lflg=false;
  bool dflg=false;
  
  if (args) {
    if (strncmp(flgs, "-l", 3)==0) lflg=true;
    if (strncmp(flgs, "-d", 3)==0) dflg=true;
  }
  
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    monprintf("%s", cmd->name);
    if (lflg) {
      monprintf(" tty:%s pid:%d restarts:%d cmdline:%s\n",
	     cmd->clttty.link, cmd->pid, cmd->restartcnt, cmd->cmdline);
    } else if (dflg) {
      if (GBLS.mon.tty.opens !=0 ) cmdDump(cmd,GBLS.mon.fileptr, "\n");
    } else monprintf("\n");
  }
  return 0;
}

int
monHelp(int args, int epollfd)
{
  usage("yar", GBLS.mon.fileptr);
  return 0;
}

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
    EPRINT(stderr, "mon: invalid cmd line: i=%d\n",i);
    return;
  }
  
  // check for command and execute 
  for (j=0; MonCmds[j].cmd != NULL; j++) {
    if (strncmp(cmd, MonCmds[j].name, i+1)==0) {
      int rc = MonCmds[j].cmd(args, epollfd);
      if (rc==0) { monprintf("OK\n"); }
      else { monprintf("FAILED\n"); }
      monGreeting();
      return;
    }
  }
  
  monprintf("yar: %s: command not found\n", cmd);
  monusage(GBLS.mon.fileptr);
  monprintf("FAILED\n");
  monGreeting();
}

static evnthdlrrc_t
monEvent(void *obj, uint32_t evnts, int epollfd)
{
  assert(obj == &GBLS.mon);
  int     fd = GBLS.mon.tty.dfd;

  VLPRINT(3,"START: mon: fd:%d evnts:0x%08x\n", fd, evnts);
  if (evnts & EPOLLIN) {
    int curn = GBLS.mon.n;
    // leave room for null termination
    ASSERT(curn<MON_LINELEN);
    int n = read(fd, &GBLS.mon.line[curn], 1);
    assert(n==1);
    curn+=n;
    assert(curn <= MON_LINELEN);
    
    if (curn == MON_LINELEN && GBLS.mon.line[curn]!='\n') {
      EPRINT(stderr, "exceeded MON_LINELEN=%d...ignoring\n", MON_LINELEN);
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


evnthdlrrc_t
monNotify(void *obj, uint32_t mask, int epollfd)
{
  mon_t *this = obj;
  assert(this == &GBLS.mon);
  switch (mask) {
  case IN_OPEN:
    monGreeting();
    mask = mask & ~IN_OPEN;
    break;
  case IN_CLOSE_WRITE:
  case IN_CLOSE_NOWRITE:
    mask = mask & ~IN_CLOSE;
    break;
  default:
    EPRINT(stderr, "Unexpected notify case: mask:0x%x\n",
	   mask);
    NYI;
  }  
  return EVNT_HDLR_SUCCESS;
}

void
monCleanup()
{
  VPRINT("%p\n", &(GBLS.mon));
  ttyCleanup(&GBLS.mon.tty);
  GBLS.mon.n = 0;
  GBLS.mon.fileptr = NULL;
}

void
monttyCreate() {
  evntdesc_t ed  = { .obj = &(GBLS.mon), .hdlr = monEvent  };
  evntdesc_t ned = { .obj = &(GBLS.mon), .hdlr = monNotify };
    if (!ttyCreate(&GBLS.mon.tty, ed, ned, true)) EEXIT();
    GBLS.mon.fileptr = fdopen(GBLS.mon.tty.dfd, "a");
    if (GBLS.mon.fileptr == NULL) {
      perror("fdonpen on GBLS.mon.tty.dfd");
      EEXIT();
    }
}

static void
monitorRegisterEvents(int epollfd)
{
  ttyRegisterEvents(&(GBLS.mon.tty), epollfd);
}

static bool
monTtyLink(char *link, int n, char *dir)
{
  int rc;
  if (dir) {
    rc = snprintf(link, n, "%s/%" PRIdMAX ".mon", dir, (intmax_t)GBLS.pid);
  } else {
    rc = snprintf(link, n, "%" PRIdMAX ".mon",  (intmax_t)GBLS.pid);    
  }
  if (rc < 0 || rc >= n) {
    perror("snprintf");
    return false;
  }
  return true;
}

static void
monInit(bool initdir, char *dir, bool iszeroed)
{
  char tmp[PATH_MAX];

  if (!iszeroed) {
    bzero(&(GBLS.mon), sizeof(GBLS.mon));
  }
  GBLS.mon = (mon_t){ .ed = {.obj = &(GBLS.mon),
			     .hdlr = &monEvent },
		      .silent = true };
  if (initdir) {
    if (!monTtyLink(tmp, PATH_MAX, dir)) EEXIT();
    if (!ttyInit(&(GBLS.mon.tty), tmp, true)) EEXIT();
  } else {
    if (!ttyInit(&(GBLS.mon.tty), NULL, true)) EEXIT();
  }
}

static bool checkfd(int fd)
{
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    fprintf(stderr, "fstat on %d failed errno=%d\n", fd, errno);
    if (errno == EBADFD) {
      fprintf(stderr, "EBADFD: %d\n", fd);
    }
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

  // register for the monitor interface events
  monitorRegisterEvents(epollfd);

  // register for the fs interface events
  fsRegisterEvents(&GBLS.fs, epollfd);
  
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
    errno = 0;
    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      if (verbose(1)) perror("epoll_wait");
      if (errno == EINTR) {
	// process signal handler state
	if (GBLS.exitsignaled) {
	  VPRINT("%s: EINTR: Exited Signaled %d\n", __func__, GBLS.signal); 
	  rc = false;
	  goto done;
	}
	VLPRINT(3, "%s: EINTR: Continuing\n", __func__);
	continue;
      }
      if (errno == EINVAL) {
	// I don't know why this is happening
	// once we added logging
	//  trigger -> run yar, ctl-z, bg, enter
	EPRINT(stderr, "FAIL:errno=%d epollfd=%d ME=%d checkfd=%d\n",
	       errno, epollfd, MAX_EVENTS, checkfd(epollfd));
	continue;
      }
      rc = false;
      EPRINT(stderr, "FAIL:errno=%d epollfd=%d ME=%d checkfd=%d\n",
	     errno, epollfd, MAX_EVENTS, checkfd(epollfd));
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
	EPRINT(stderr, "event handler failed hdlr:%p obj:0x%p evnts:%08x\n",
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

// avoid any real work only update GBLS
static bool
argsParse(int argc, char **argv)
{
    int opt;
    
    while ((opt = getopt(argc, argv, "DKL:b:d:e:f:hlm:pr:s:v")) != -1) {
    switch (opt) {
    case 'D':
      GBLS.daemonize = true;
      break;
    case 'K':
      GBLS.keeplog = true;
      break;
    case 'L':
      GBLS.uselog  = true;
      GBLS.logpath = strdup(optarg);
      break;
    case 'b':
      GBLS.bcstttylink = strdup(optarg);
      if (checkpath(GBLS.bcstttylink, 0)) {
	fprintf(stderr, "ERROR: %s already exists\n", GBLS.bcstttylink);
	return false;
      }
      // if user explicitly specifies a path for the broadcast tty
      // the force its creation at startup
      GBLS.bcstflg=true;
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
    case 'f':
      GBLS.fsmntptdir = strdup(optarg);
      break;
    case 'h':
      usage(argv[0],stderr);
      return false;
    case 'l' :
      GBLS.linebufferbcst = true;
      break;
    case 'm':
      GBLS.monttylinkdir = strdup(optarg);
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
    case  's':
      GBLS.stopstr = strdup(optarg);
      break;
    case 'v':
      GBLS.verbose++;
      break;
    default:
      usage(argv[0],stderr);
      return false;
    }
  } 

  int anum=argc-optind;
  char **args=&(argv[optind]);
    
  if (anum < 1) return true;
  
  for (int i=0; i<anum; i++) {
    VLPRINT(3, "args[%d]=%s\n", i, args[i]);
    char *tmp = strdup(args[i]);
    char *name, *cmdline, *ttylink, *log;
    double delay;
    
    if (!cmdspecParse(tmp, &name, &cmdline, &delay, &ttylink, &log, stderr)) {
      // failed to parse cmd spec
      free(tmp);
      return false;
    }
    free(tmp);
  }
  GBLS.initialcmdspecs=args;
  GBLS.initialcmdspecscnt=anum;

  if (anum>1) GBLS.bcstflg=true;
  
  if (verbose(1)) GBLSDump(stderr); 
  return true;
}

extern
void cleanup(void)
{
  VPRINT("GBLS:%p\n", &GBLS);
  if (GBLS.bcstflg) ttyCleanup(&GBLS.bcsttty);
  {
    cmd_t *cmd, *tmp;
    HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
      VLPRINT(3, "Cleanup up cmd %s\n", cmd->name);
      cmdCleanup(cmd);
      HASH_DEL(GBLS.cmds, cmd);
      free(cmd);
    }
  }
  GBLS.slowestcmd = NULL;
  fsCleanup(&(GBLS.fs));
  monCleanup();
  if (GBLS.logfile) {
    fclose(GBLS.logfile);
    if (!GBLS.keeplog) {
      if (!remove(GBLS.logpath)) perror("failed to remove logfile");
    }
    GBLS.logfile=NULL;
  }
  if (GBLS.logpath) { free(GBLS.logpath); GBLS.logpath=NULL;  }
  GBLS.initialcmdspecs = NULL;   // points to argv values
  GBLS.initialcmdspecscnt = 0;
  if (GBLS.stopstr) { free(GBLS.stopstr); GBLS.stopstr = NULL; }
  if (GBLS.bcstttylink) { free(GBLS.bcstttylink); GBLS.bcstttylink = NULL; }
  if (GBLS.monttylinkdir) {
    free(GBLS.monttylinkdir);
    GBLS.monttylinkdir = NULL;
  }
  if (GBLS.fsmntptdir) { free(GBLS.fsmntptdir); GBLS.fsmntptdir = NULL; }
  if (GBLS.cwd) { free(GBLS.cwd); GBLS.cwd = NULL; }
}

void
exitsignalhandler(int sig)
{
  GBLS.exitsignaled = true;
  GBLS.signal = sig;
}

static bool
setlogpath(char *dir)
{
  int rc;
  char path[PATH_MAX];
  if (dir) {
    rc = snprintf(path, 1024, "%s/%" PRIdMAX ".log", dir, (intmax_t)GBLS.pid);
  } else {
    rc = snprintf(path, 1024, "%" PRIdMAX ".log", (intmax_t)GBLS.pid);
  }
  if (rc<0 || rc>=1024) {
    perror("snprintf");
    return false;
  }
  if (GBLS.logpath) free(GBLS.logpath);
  GBLS.logpath=strdup(path);
  return true;
}

bool 
openlog(char *dir)
{
  setlogpath(dir);
  VPRINT("SWITCHING stdout and error to %s.\n", GBLS.logpath);
  if (freopen("/dev/null", "r", stdin) == NULL) {
    perror("Error redirecting stdin");
    return false;
  }
  if (freopen(GBLS.logpath, "a", stdout) == NULL) {
    perror("Error redirecting stdout");
    return false;
  }
  setbuf(stdout, NULL);

  // keep copy of log file pointer
  GBLS.logfile = stdout;
  VLPRINT(2, "GBLS.logfile=%p\n", GBLS.logfile);
  
  if (freopen(GBLS.logpath, "a", stderr) == NULL) {
    perror("Error redirecting stderr");
    return false;
  }
  setbuf(stderr, NULL);

  return true;
}

void
GBLSInit()
{
  bzero(&GBLS, sizeof(GBLS));

  GBLS = (globals_t) {
    .verbose            = 0,
    .pid                = -1,
    .linebufferbcst     = false,
    .prefixbcst         = false,
    .bcstflg            = false,
    .restart            = true,
    .exitonidle         = false,
    .keeplog            = false,
    .defaultcmddelay    = 0.0,
    .restartcmddelay    = 5.0,
    .errrestartcmddelay = 10.0
  };
  // do an first round of init calls to get things in a sane
  // state incase we have to call cleanup before these
  // objects can get configured correctly
  ttyInit(&(GBLS.bcsttty),NULL,true);
  fsInit(&(GBLS.fs),false,NULL,true);
  monInit(false,NULL,true);
}

char * cwdPrefix(const char *path) {
  if (path) {
    char npath[PATH_MAX];
    snprintf(npath, PATH_MAX, "%s/%s", GBLS.cwd, path);
    return strdup(npath);
  }
  return strdup(GBLS.cwd);
}

void setsignalhandlers()
{
  signal(SIGALRM, exitsignalhandler);
  signal(SIGTERM, exitsignalhandler);
  signal(SIGINT, exitsignalhandler);
  signal(SIGHUP, exitsignalhandler);
  signal(SIGKILL, exitsignalhandler);
  signal(SIGUSR1, exitsignalhandler);
  signal(SIGVTALRM, exitsignalhandler);
  signal(SIGUSR2, exitsignalhandler);
  signal(SIGPIPE, exitsignalhandler);
  signal(SIGIO, exitsignalhandler);
}

int main(int argc, char **argv)
{
  // initialize all global data structures to ensure
  // correct cleanup behavior incase of early termination
  // the read delay of the broadcast tty will be set to the
  // largest delay of any of the commands
  GBLSInit();
  // parse arguments potentially updating GBLS
  if (!argsParse(argc, argv)) EEXIT();

  // daemonize: do NOT change directory or close stdin,out,err
  if (GBLS.daemonize) assert(daemon(1,1)==0);
  
  GBLS.pid = getpid();
  GBLS.cwd = getcwd(NULL,0);   // memory is malloced
  
  // report pid to stdout before closing original stdout, err and in
  printf("%" PRIdMAX "\n", (intmax_t)GBLS.pid);
  fflush(stdout);
  
  // use log file if needed other stdout, err and in are let
  // untouched
  if (GBLS.uselog || GBLS.daemonize) {
    if (!openlog((GBLS.logpath))? GBLS.logpath : GBLS.cwd) EEXIT();
  }
  
  atexit(cleanup);    // from this point on exits will trigger cleanups 
  setsignalhandlers();
  
  // ok lets start creating resources
  // create the initial set of command lines from the specs passed in
  // via argv
  for (int i=0; i<GBLS.initialcmdspecscnt; i++) {
    char *cmdspec = GBLS.initialcmdspecs[i];
    VLPRINT(3, "creating from cmdspec[%d]=%s\n", i, cmdspec);
    assert(GBLSAddCmd(cmdspec,NULL,stderr));
  }

  // init broadcast tty
  if (GBLS.bcstttylink == NULL) {
    GBLS.bcstttylink = cwdPrefix(DEFAULT_BCSTTTY_LINK); // mallocs
  }
  if (!ttyInit(&GBLS.bcsttty, GBLS.bcstttylink, true)) EEXIT();
 
  // create the broadcast tty
  bcstttyCreate();

  // init fs
  if (GBLS.fsmntptdir==NULL) GBLS.fsmntptdir = cwdPrefix(NULL); // mallocs
  if (!fsInit(&(GBLS.fs), true,  GBLS.fsmntptdir, true)) EEXIT();
  // create the fs
  if (!fsCreate(&(GBLS.fs), argv[0], yarfsCreate)) EEXIT();

  // init monitor tty
  if (GBLS.monttylinkdir) GBLS.monttylinkdir = cwdPrefix(NULL); // mallocs
  monInit(true, GBLS.monttylinkdir, true);  
  // create the monitor tty
  monttyCreate();

  if (!theLoop()) EEXIT();

  return EXIT_SUCCESS;
}

asciistr_t ascii_nonprintable[32] = {
  [0] = "NUL", [1] = "SOH", [2] = "STX", [3] = "ETX", [4] = "EOT", [5] = "ENQ",
  [06] = "ACK", [7] = "\\a", [8] = "\\b", [9] = "\\t", [10] = "\\n",
  [11] = "\\v", [12] = "\\f", [13] = "\\r", [14] = "SO", [15] = "SI",
  [16] = "DLE", [17] = "DC1", [18] = "DC2", [19] = "DC3", [20] = "DC4",
  [21] = "NAK", [22] = "SYN", [23] = "ETB", [24] = "CAN", [25] = "EM",
  [26] = "SUB", [27] = "ESC", [28] = "FS", [29] = "GS", [30] = "RS",
  [31] = "US"
};
