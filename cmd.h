#ifndef __YAR_CMD_H__
#define __YAR_CMD_H__

extern void cmdDump(cmd_t *this, FILE *f, char *prefix);
extern bool cmdInit(cmd_t *this, char *name, char *cmdline, double delay,
		    char *ttylink, char *log);
extern bool cmdCreate(cmd_t *this, bool raw);
extern bool cmdCleanup(cmd_t *this);
__attribute__((unused)) static int 
cmdWriteChar(cmd_t *this, char c)
{
  return ttyWriteChar(&(this->cmdtty), c, &(this->lastwrite));
}

#endif
