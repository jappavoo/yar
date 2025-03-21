#ifndef __YAR_TTY_H__
#define __YAR_TTY_H__

extern void ttyDump(tty_t *this, FILE *f, char *prefix);
extern bool ttyInit(tty_t *this, char *ttylink);
extern bool ttyCreate(tty_t *this, bool raw);
extern bool ttyCleanup(tty_t *this);
extern int  ttyWriteChar(tty_t *this, char c, struct timespec *ts);
extern int  ttyReadChar(tty_t *this, char *c, struct timespec *ts, double delay);

#endif
