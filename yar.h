#ifndef __YAR_H__
#define __YAR_H__

#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sched.h>
#include <uthash.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <time.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <limits.h>
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#include <fuse_lowlevel.h>

// yar include files
#include "event.h"
#include "tty.h"
#include "cmd.h"
#include "fs.h"

//#define ASSERTS_OFF
//#define VERBOSE_CHECKS_OFF

#ifdef ASSERTS_OFF
#define ASSERT(...)
#else
#define ASSERT(...) assert(__VA_ARGS__)
#endif

#define NSEC_IN_SECOND 1000000000
#define MON_LINELEN 4096
#define DEFAULT_CMD_DELAY 0.0
#define CLOCK_SOURCE CLOCK_MONOTONIC

typedef int (*moncmd_t)(int,int);

// Monitor Object
//  Provides a line oriented control interface to the yar process
//  
typedef struct monitor {
  char        line[MON_LINELEN]; // line buffer
  tty_t       tty;               // tty used to communicate with monitor clients
  evntdesc_t  ed;                // event descriptor to receive tty i/o events
  FILE       *fileptr;           // filepointer for fprintf calls
  int         n;                 // number of bytes buffered
  bool        silent;            // silent do produce an output -- no prompt,
                                 // no monitor command output, no result code
                                 // and no error messages!
} mon_t;

#define monprintf(...) if (GBLS.mon.tty.opens != 0 && !GBLS.mon.silent)		\
    { fprintf(GBLS.mon.fileptr, __VA_ARGS__); fflush(GBLS.mon.fileptr); }

// This structs defines all global data structures/variables
// that can be access from any function/method via the single GBLS instance
// of this struct
// all char pointers are malloced
typedef struct {
  tty_t  bcsttty;             // broadcast tty
  mon_t  mon;                 // monitor object: control interface to yar
  fs_t   fs;                  // filesystem object: control interface to yar
  cmd_t *cmds;                // hashtable of cmds
  cmd_t *slowestcmd;          // pointer to the slowest cmd so that we can pace
                              // broadcast tty reads based on this command
                              // this approach avoids us having to implement
                              // our own write buffering to delay writes
                              // rather we pace reads and let the data
                              // buffer in the kernel tty port
  char **initialcmdspecs;     // cmd specs passed as command line args
  char  *stopstr;             // a string to send to a command line when
                              // stopping
  char  *cwd;                 // current working directory path
  char  *bcstttylink;         // path of broadcast tty link
  char  *monttylinkdir;       // path of directory that monitor tty should be in
  char  *fsmntptdir;          // path of directory that yar fs should be in
  char  *logdir;              // path of directory that yar log should be in
  char  *logpath;             // path of log used when daemonized (logdir/<pid>.log)
  FILE  *logfile;             // file pointer of log when daemonized
  double defaultcmddelay;     // default value for sending data to commands
  double restartcmddelay;     // delay restarting command if exited with success
  double errrestartcmddelay;  // delay restarting command if exited with failure
  pid_t  pid;                 // pid of this yar processs
  int    verbose;             // verbosity level
  int    initialcmdspecscnt;  // number of initial cmd specs
  int    signal;              // signal handler will set this to signal number
  bool   linebufferbcst;      // if true output from commands sent to broadcast
                              // tty will be line buffered to avoid interleaving
                              // within a line (max line size is CMD_BUF_SIZE).
  bool   prefixbcst;          // prefix writes to broadcast from cmd with cmd
                              // name
  bool   bcstflg;             // create a broadcast tty
  bool   restart;             // globally controls if commands should be 
			      // restarted
  bool   exitonidle;          // exit if all commands are removed
  bool   cmddelonexit;        // global value for new commands to delete if the
                              // exit
  bool   keeplog;             // do not delete log on exit
  bool   exitsignaled;        // set by signal handlers to trigger exit logic in
                              // theLoop
  bool   uselog;              // use a log file for stdout and err and use
                              // /dev/null for stdin
  bool   daemonize;           // start as daemon
} globals_t;
extern globals_t GBLS;

extern void cleanup();

// Error print
#define EPRINT(f, fmt, ...) {						\
    if (f==GBLS.mon.fileptr) { monprintf("%s: " fmt, __func__, __VA_ARGS__); } \
    else fprintf(f, "%s: " fmt, __func__, __VA_ARGS__);			\
  }

// Error Exit
static inline void EEXIT() {
  exit(EXIT_FAILURE);
}

// true if in ascii range
__attribute__((unused)) static bool
ascii_isvalid(int c) { return (c>=0 && c<=127); }

// returns true if printable false if not
__attribute__((unused)) static bool
ascii_isprintable(int c) { return ( (c>=' ' && c<='~') ||
					 (c=='\n' || c=='\r' || c=='\t') ); }

typedef char asciistr_t[4];
extern asciistr_t ascii_nonprintable[32];

// ascii character to string
// if in ascii range return true and set first four values of str to string
// encoding of ascii value. 
__attribute__((unused)) static bool
ascii_char2str(int c, asciistr_t str)
{
  assert(str);
  if (!ascii_isvalid(c)) {
    str[0]=0;
    return false;
  }
  if (c>=' ' && c<='~') {
    str[0]=(char)c; str[1]=0; str[2]=0; str[3]=0;
  } else {
    if (c == 127) {
      // handle del 127 as a special case 
      str[0]='D'; str[1]='E'; str[2]='L'; str[3]=0;
    } else {
      // rest of non-printable ascii are densely packed and can be looked up
      assert(c>=0 && c<=(sizeof(ascii_nonprintable)/sizeof(asciistr_t)));
      str[0]=ascii_nonprintable[c][0];
      str[1]=ascii_nonprintable[c][1];
      str[2]=ascii_nonprintable[c][2];
      str[3]=ascii_nonprintable[c][3];
    }
  }
  return true;
}

extern void fdSetnonblocking(int fd);

extern void delaysec(double delay);

extern char * cwdPrefix(const char *path);


#define NYI { fprintf(stderr, "%s: %d: NYI\n", __func__, __LINE__); assert(0); }

#ifdef VERBOSE_CHECKS_OFF
static inline bool verbose(int l) { return 0; }
#define VLPRINT(VL, fmt, ...)
#define VPRINT(fmt, ...)
#else
static inline bool verbose(int l) { return GBLS.verbose >= l; }
#define VLPRINT(VL, fmt, ...)  {					\
    if (verbose(VL)) {							\
	  fprintf(stderr, "%s: " fmt, __func__, __VA_ARGS__);		\
    } }

#define VPRINT(fmt, ...) VLPRINT(1, fmt, __VA_ARGS__)

#endif

#include "hexdump.h"
#include "tty.h"
#include "cmd.h"
#include "fs.h"
#include "yarfs.h"

#endif

