#ifndef __YAR_TTY_H__
#define __YAR_TTY_H__

#include <termios.h>
#include <unistd.h>

extern void ttyDump(tty_t *this, FILE *f, char *prefix);
extern bool ttyInit(tty_t *this, char *ttylink);
extern bool ttyCreate(tty_t *this, bool raw);
extern bool ttyCleanup(tty_t *this);
extern int  ttyWriteChar(tty_t *this, char c, struct timespec *ts);
extern int  ttyReadChar(tty_t *this, char *c, struct timespec *ts, double delay);
extern void ttyPortSpace(tty_t *this, int *in, int *out, int *sin, int *sout);
__attribute__((unused)) static int ttySlaveInQCnt(tty_t *this)
{
  int cnt; assert(ioctl(this->sfd, TIOCINQ, &cnt)==0); return cnt;
}
__attribute__((unused)) static void ttySlaveFlush(tty_t *this)
{
  tcflush(this->sfd, TCIFLUSH);
};
#endif
