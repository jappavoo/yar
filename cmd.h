#ifndef __YAR_CMD_H__
#define __YAR_CMD_H__

#define CMD_BUFSIZE 4096

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
  char   *cmdstr;              // pointer if space allocated for cmd str  
  char   *name;               // user defined name (link is by default name)
  char   *bcstprefix;         // prefix to use if enabled 
  char   *cmdline;            // shell command line of command  
  char   *log;                // path to log (copy of all data written and read)
  char   *stopstr;            // string to send when stopping takes precedence 
                              // over GBLS.stopstr
  double  delay;              // time between writes
  pid_t   pid;                // process id of running command
  size_t  bufn;               // number of bytes buffered [0..SIZE_MAX]
  size_t  bufstart;           // start since last flush of buffer [0..SIZE_MAX]
  int     bufof;              // number of times a line has overflowed the buf
  int     bcstprefixlen;      // length of prefix without null;
  int     pidfd;              // pid fd to monitor for termination
  int     exitstatus;         // exit status if command terminates
  int     restartcnt;         // count of restarts
  bool    restart;            // restart this command if it exits
  bool    deleteonexit;       // delete this command if it exits 
} cmd_t;

extern void cmdDump(cmd_t *this, FILE *f, char *prefix);
extern bool cmdInit(cmd_t *this, char *cmdstr, char *name, char *cmdline,
		    double delay, char *ttylink, char *log, bool iszeroed);
extern bool cmdCreate(cmd_t *this);
extern bool cmdStart(cmd_t *this, bool raw, int epollfd, double startdelay);
extern bool cmdStop(cmd_t *this, int epollfd, bool force);
extern bool cmdRegisterttyEvents(cmd_t *this, int epollfd);
extern bool cmdRegisterProcessEvents(cmd_t *this, int epollfd);
extern bool cmdCleanup(cmd_t *this);

__attribute__((unused)) static inline bool cmdIsRunning(cmd_t *this)
{
  return ( this->pid != -1 ); 
}

__attribute__((unused)) static inline int cmdWriteChar(cmd_t *this, char c)
{
  return ttyWriteChar(&(this->cmdtty), c, &(this->lastwrite));
}

#endif
