#ifndef __YAR_TTY_H__
#define __YAR_TTY_H__

#include <termios.h>
#include <unistd.h>

#define TTY_MAX_PATH 256

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

extern void ttyDump(tty_t *this, FILE *f, char *prefix);
extern bool ttyInit(tty_t *this, char *ttylink, bool iszeroed);
extern bool ttySetlink(tty_t *this, char *ttylink);
extern bool ttyCreate(tty_t *this, evntdesc_t ed, evntdesc_t ned, bool raw);
extern bool ttyRegisterEvents(tty_t *this, int epollfd);
extern bool ttyCleanup(tty_t *this);
extern int  ttyWriteBuf(tty_t *this, char *buf, int len, struct timespec *ts);
extern int  ttyReadChar(tty_t *this, char *c, struct timespec *ts,
			double delay);
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

