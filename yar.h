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


//#define ASSERTS_OFF
//#define VERBOSE_CHECKS_OFF

#ifdef ASSERTS_OFF
#define ASSERT(...)
#else
#define ASSERT(...) assert(__VA_ARGS__)
#endif

typedef enum {
  EVNT_HDLR_SUCCESS=0,
  EVNT_HDLR_FAILED=-1,
  EVNT_HDLR_EXIT_LOOP=1 } evnthdlrrc_t;
typedef  evnthdlrrc_t (* evnthdlr_t)(void *, uint32_t);
typedef struct {
  evnthdlr_t hdlr;
  void *obj;
} evntdesc_t;

#define CMD_BUFSIZE 4096
#define TTY_MAX_PATH 256
typedef struct {
  char      path[TTY_MAX_PATH];// tty path (slave)
  char     *link;              // link path to tty path
  uint64_t  rbytes;            // number of bytes read from tty
  uint64_t  wbytes;            // number of bytes written to tty
  int       fd;                // fd used to communicate with tty (master)
  int       ifd;               // inotifyfd
  int       ccnt;              // count of active clients (current opens of tty)
} tty_t;

typedef struct  {
  uint8_t buf[CMD_BUFSIZE];   // buffer of data read from command
  UT_hash_handle hh;          // hashtable handle
  tty_t   cmdtty;             // tty command process is connected to
  tty_t   clttty;             // tty clients can use to talk directly to cmd
  char   *name;               // user defined name (link is by default name)
  char   *cmdline;            // shell command line of command  
  char   *log;                // path to log (copy of all data written and read)
  evntdesc_t pidfded;         // pidfd event descriptor;
  double  delay;              // time between writes
  pid_t   pid;                // process id of running command
  size_t  n;                  // number of bytes in buffer
  int     pidfd;              // pid fd to monitor for termination
} cmd_t;

typedef struct {
  cmd_t *cmds;                // hashtable of cmds
  tty_t  btty;                // broadcast tty 
  int    verbose;             // verbosity level 
} globals_t;

extern globals_t GBLS;

extern void cleanup();
#define EPRINT(fmt, ...) {fprintf(stderr, "%s: " fmt, __func__, __VA_ARGS__);}
static inline void EEXIT() {
  cleanup();
  exit(EXIT_FAILURE);
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

