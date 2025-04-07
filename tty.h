#ifndef __YAR_TTY_H__
#define __YAR_TTY_H__

#include <termios.h>
#include <unistd.h>

extern void ttyDump(tty_t *this, FILE *f, char *prefix);
extern bool ttyInit(tty_t *this, char *ttylink);
extern bool ttyCreate(tty_t *this, evntdesc_t ed, evntdesc_t ned, bool raw);
extern bool ttyRegisterEvents(tty_t *this, int epollfd);
extern bool ttyCleanup(tty_t *this);
extern int  ttyWriteBuf(tty_t *this, char *buf, int len, struct timespec *ts);
extern int  ttyReadChar(tty_t *this, char *c, struct timespec *ts, double delay);
extern void ttyPortSpace(tty_t *this, int *in, int *out, int *sin, int *sout);

// INLINES
__attribute__((unused)) static inline int
ttyWriteChar(tty_t *this, char c, struct timespec *ts)
{
  return ttyWriteBuf(this, &c, 1, ts);
}

__attribute__((unused)) static inline bool ttyIsClttty(tty_t *this)
{
  return (this && this->link != NULL);
}

__attribute__((unused)) static inline bool ttyIsCmdtty(tty_t *this)
{
  return !ttyIsClttty(this);
}

__attribute__((unused)) static inline int ttySubInQCnt(tty_t *this)
{
  int cnt; assert(ioctl(this->sfd, TIOCINQ, &cnt)==0); return cnt;
}
__attribute__((unused)) static inline void ttySubFlush(tty_t *this)
{
  tcflush(this->sfd, TCIFLUSH);
};
#endif

