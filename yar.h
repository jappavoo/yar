#ifndef __YAR_H__
#define __YAR_H__

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
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

//#define ASSERTS_OFF
//#define VERBOSE_CHECKS_OFF

#ifdef ASSERTS_OFF
#define ASSERT(...)
#else
#define ASSERT(...) assert(__VA_ARGS__)
#endif

#define NSEC_IN_SECOND 1000000000
#define CMD_BUFSIZE 4096
#define TTY_MAX_PATH 256
#define DEFAULT_CMD_DELAY 0.0
#define CLOCK_SOURCE CLOCK_MONOTONIC

typedef enum {
  EVNT_HDLR_SUCCESS=0,
  EVNT_HDLR_FAILED=-1,
  EVNT_HDLR_EXIT_LOOP=1 } evnthdlrrc_t;
typedef  evnthdlrrc_t (* evnthdlr_t)(void *, uint32_t, int);
typedef struct {
  evnthdlr_t hdlr;
  void *obj;
} evntdesc_t;

typedef struct {
  char      path[TTY_MAX_PATH];// tty path (slave)
  char     *link;              // link path to tty path
  uint64_t  rbytes;            // number of bytes read from tty
  uint64_t  wbytes;            // number of bytes written to tty
  uint64_t  delaycnt;          // number of times we delayed reading tty
  int       mfd;               // fd used to communicate with tty (master)
  int       sfd;               // fd used to hold a reference to the slave
                               // tty if we want to keep it alive
  int       ifd;               // inotifyfd
  int       ccnt;              // count of active clients (current opens of tty)
} tty_t;

typedef struct  {
  uint8_t buf[CMD_BUFSIZE];   // buffer of data read from command
  UT_hash_handle hh;          // hashtable handle
  tty_t   cmdtty;             // tty used to communicate with the cmd process
  tty_t   clttty;             // tty clients can use to talk directly to cmd
  evntdesc_t pidfded;         // pidfd event descriptor;
  evntdesc_t cmdttyed;        // cmdtty event descriptor;
  evntdesc_t cltttyed;        // clttty event descriptor;
  struct timespec lastwrite;  // timestamp of last write
  char   *name;               // user defined name (link is by default name)
  char   *cmdline;            // shell command line of command  
  char   *log;                // path to log (copy of all data written and read)
  double  delay;              // time between writes
  pid_t   pid;                // process id of running command
  size_t  n;                  // number of bytes buffered since last flush
  int     pidfd;              // pid fd to monitor for termination
  int     exitstatus;         // exit status if command terminates
  bool    retry;              // retry if exit
} cmd_t;

typedef struct {
  cmd_t *cmds;                // hashtable of cmds
  cmd_t *slowestcmd;          // pointer to the slowest cmd so that we can pace
                              // broadcast tty reads based on this command
                              // this approach avoids us having to implement
                              // our own write buffering to delay writes
                              // rather we pace reads and let the data
                              // buffer in the kernel tty port 
  tty_t  btty;                // broadcast tty
  double defaultcmddelay;     // default value for sending data to commands
  int    verbose;             // verbosity level 
} globals_t;

extern globals_t GBLS;

extern void cleanup();
#define EPRINT(fmt, ...) {fprintf(stderr, "%s: " fmt, __func__, __VA_ARGS__);}
static inline void EEXIT() {
  cleanup();
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

#define NYI { fprintf(stderr, "%s: %d: NYI\n", __func__, __LINE__); }

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

#endif

