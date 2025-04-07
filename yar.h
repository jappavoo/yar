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

// TTY Object
// Uses a UNIX PTY pair of ttys, dom-tty (aka master in traditional UNIX lingo)
// and sub-tty (aka slave in tranditional UNIX lingo), to represent a terminal
// communication channel.  The two core uses in yar are:
//   1) CLIENT tty interfaces that can be used to send and receive
//      bytes to and from yar.  By default yar creates one client
//      tty object for each command lauched to serve as a client interface
//      to the command.  The sub-tty will have a link created in the file system
//      that clients can read and write data from.  Yar will take care of
//      relaying data to the command process as its sees fit.
//      Specifically, yar will use the dom-tty to send and receive data to the
//      sub-tty.   Yar also provides at least one broadcast client tty  
//      to allow clients to transmit and recieve data from all the
//      command process.    Additionally, Yar can publish other client
//      ttys to provide addtional interfaces to yar.
//   2) COMMAND tty interfaces used to connect yar to the standard
//      in, out and errot of the command processes yar launches.  In this
//      case the sub-tty is NOT published in filesystem with a link,
//      rather the link field will be null.  The command process will be the
//      only user of the sub-tty (Yar will connect the process to it prior
//      to exec).  The dom-tty will be used for internal communciation
//      betweem Yar and the command connected to the sub-tty.
typedef struct {
  char      path[TTY_MAX_PATH];// tty path (sub-tty dev path)
  char     *link;              // link path to tty path (null for command ttys)
  uint64_t  rbytes;            // number of bytes read from tty
  uint64_t  wbytes;            // number of bytes written to tty
  uint64_t  delaycnt;          // number of times we delayed reading the tty
  int       dfd;               // dom fd : use by yar to communicate
                               // bytes to and from the dom-tty which is
                               // connected to the sub-tty
  int       sfd;               // sub fd : for client tty's yar opens the 
                               // sub-tty to keep it alive regardless of
                               // clients existing (a process that opens
                               // the sub-tty path to communicate it commands)
  int       ifd;               // inotify fd to track opens and closes
                               // in the case of a client tty
  int       iwd;               // an inotify watch descriptor 
  int       opens;             // count current opens of the tty (via its
                               // sub-tty).  For command ttys we expect
                               // this to be only 0 if the command is not
                               // running or one if it is running.  For
                               // client ttys opens could be 0 or more
                               // depending how many clients have the
                               // sub-tty open via its link path.
  evntdesc_t dfded;            // dom fd event descriptor
  evntdesc_t ifded;            // inotify fd event descriptor
  evntdesc_t ned;              // external notify event descriptor 
} tty_t;

// CMD Object
typedef struct  {
  uint8_t buf[CMD_BUFSIZE];   // buffer of data read from command
  UT_hash_handle hh;          // hashtable handle
  tty_t   cmdtty;             // command tty used to internally communicate
                              // with the command process
  tty_t   clttty;             // client tty  used to communicate with external
                              // clients
  evntdesc_t pidfded;         // pidfd event descriptor  
  struct timespec lastwrite;  // timestamp of last write
  char   *name;               // user defined name (link is by default name)
  char   *bcstprefix;         // prefix to use if enabled 
  char   *cmdline;            // shell command line of command  
  char   *log;                // path to log (copy of all data written and read)
  double  delay;              // time between writes
  pid_t   pid;                // process id of running command
  size_t  bufn;               // number of bytes buffered [0..SIZE_MAX]
  size_t  bufstart;           // start since last flush of buffer [0..SIZE_MAX]
  int     bufof;              // number of times a line has overflowed the buf
  int     bcstprefixlen;      // length of prefix without null;
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
  tty_t  bcsttty;             // broadcast tty
  double defaultcmddelay;     // default value for sending data to commands
  bool   linebufferbcst;      // if true output from commands sent to broadcast
                              // tty will be line buffered to avoid interleaving
                              // within a line (max line size is CMD_BUF_SIZE).
  bool   prefixbcst;          // prefix writes to broadcast from cmd with cmd
                              // name
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

extern void fdSetnonblocking(int fd);

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

#endif

