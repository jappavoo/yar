#ifndef __YAR_TTY_H__
#define __YAR_TTY_H__

extern void ttyDump(tty_t *this, FILE *f, char *prefix);
extern bool ttyInit(tty_t *this, char *ttylink);
extern bool ttyCreate(tty_t *this);
extern bool ttyCleanup(tty_t *this);

#endif
