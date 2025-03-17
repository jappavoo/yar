#include "yar.h"
#include <syscall.h>

extern bool
cmdInit(cmd_t *this, char *name, char *cmdline, double delay, char *ttylink,
	char *log)
{
  assert(name && cmdline); 
  this->name    = name;
  this->cmdline = cmdline;
  this->delay   = delay;
  this->log     = log;
  this->n       = 0;
  this->pid     = -1;
  this->pidfd   = -1;
  ttyInit(&(this->cmdtty), NULL);
  ttyInit(&(this->clttty), ttylink);
  return true;
}

extern void
cmdDump(cmd_t *this, FILE *f, char *prefix)
{
  assert(this);
  fprintf(f, "%scmd: this=0x%p pid=%ld pidfd=%d name=%s cmdline=%s delay=%f "
	  "log=%s n=%lu", prefix, this, (long)this->pid, this->pidfd, this->name,
	  this->cmdline, this->delay, this->log, this->n);
  if (this->n) hexdump(f, this->buf, this->n);
  ttyDump(&(this->cmdtty), f, "\n    cmdtty: ");
  ttyDump(&(this->clttty), f, "    clttty: ");
}

#if 0
static int
pidfd_open(pid_t pid, unsigned int flags)
{
  return syscall(SYS_pidfd_open, pid, flags);
}
#endif

extern bool
cmdCreate(cmd_t *this)
{
  assert(this);
  assert(this->cmdline);
  return true;
}

extern bool
cmdCleanup(cmd_t *this)
{
  return false;
}

