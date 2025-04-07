#ifndef __YAR_CMD_H__
#define __YAR_CMD_H__

extern void cmdDump(cmd_t *this, FILE *f, char *prefix);
extern bool cmdInit(cmd_t *this, char *name, char *cmdline, double delay,
		    char *ttylink, char *log);
extern bool cmdCreate(cmd_t *this);
extern bool cmdStart(cmd_t *this, bool raw, int epollfd);
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
