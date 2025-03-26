#include "yar.h"
#include <pty/pty_master_open.h>
#include <tty/tty_functions.h>

extern void
ttyDump(tty_t *this, FILE *f, char *prefix)
{
  fprintf(f, "%stty: this=%p path=%s link=%s \n"
             "       mfd=%d sfd=%d ifd=%d\n"
	  "       rbytes=%lu wbytes=%lu opens=%d readers=%d\n",
	  prefix, this,  this->path, this->link,
	  this->mfd, this->sfd, this->ifd, this->rbytes, this->wbytes,
	  this->opens, this->readers);
}

extern bool
ttyInit(tty_t *this, char *ttylink)
{
  this->path[0]   =  0;
  this->link      =  ttylink;
  this->rbytes    =  0;
  this->wbytes    =  0;
  this->mfd       = -1;
  this->sfd       = -1;
  this->ifd       = -1;
  this->opens     =  0;
  this->readers   =  0;
  return true;
}

extern bool
ttyCreate(tty_t *this, bool raw)
{
  int sfd;
  VLPRINT(1, "this=%p\n", this);

  assert(this);
  assert(this->mfd == -1);

  if (this->link != NULL && access(this->link, F_OK)==0) {
    EPRINT("%s already exists\n", this->link);
    return false;
  }
  
  this->mfd = ptyMasterOpen(this->path, TTY_MAX_PATH);
  if (this->mfd == -1) {
    perror("ptyMasterOpen failed:");
    return false;
  }
  fcntl(this->mfd, F_SETFD, FD_CLOEXEC);

  // we use non blocking reads and watch for EAGAIN on writes
  // to see when we have exhausted the kernel tty port buffer
  fdSetnonblocking(this->mfd);
  
  // we open the slave side and keep it open to ensure
  // that if out clients come and got via opens and close
  // of the slave side the slave tty and its persistent state
  // buffer and flags will survive.  
  sfd = open(this->path, O_RDWR);
  if (sfd == -1) {
    perror("client side of pty could not be opened");
    close(this->mfd);
    this->mfd = -1;
    return false;
  }
  fcntl(this->sfd, F_SETFD, FD_CLOEXEC);
  if (raw) {
    ttySetRaw(sfd, NULL);
  }
  
  if (this->link != NULL) {
    VLPRINT(2, "linking %s->%s\n", this->path, this->link);
    int rc =symlink(this->path, this->link);
    if (rc!=0) {
      perror("failed tty link create failed");
      return false;
    }
  }

  this->sfd     = sfd;
  this->rbytes  = 0;
  this->wbytes  = 0;
  this->opens   = 0;
  this->readers = 0;
  
  if (verbose(1)) ttyDump(this, stderr, "  Created ");
  return true;
}

extern int
ttyWriteChar(tty_t *this, char c, struct timespec *ts)
{
  int n=0;
 retry:
  n = write(this->mfd, &c, 1);
  if (n==-1) {
    if (errno==EAGAIN) {
      if (this->readers==0) {
	// FIXME: JA think about race conditions with a new reader
	//        starting when the port buffer is full
	// we have no readers
	int sqc = ttySlaveInQCnt(this);
	ttySlaveFlush(this);
	VLPRINT(1, "write to %s(%s) failed with %d opens %d readers:"
		" FLUSHED: %d bytes\n",
		this->link, this->path, this->opens, this->readers, sqc);
	n = write(this->mfd, &c, 1);
	goto retry;
      } else {
	EPRINT("failed to write data to %s(%s) with %d opens and %d readers\n"
	       "IMPLEMENT FLOW CONTROL: hang on to failed character & retry\n",
	       this->link, this->path, this->opens, this->readers);
	NYI;
      }
    } else {
      perror("ttyWriteChar write failed");
      NYI;
    }
  } else if (n==1) {
    if (ts) {
      if (clock_gettime(CLOCK_SOURCE, ts) == -1) {
	perror("clock_gettime");
	NYI;
      }
    }
    this->wbytes++;
    if (verbose(2)) {
      asciistr_t charstr;
      ascii_char2str((int)c, charstr);
      VPRINT("  %p:%s(%s): fd:%d c:%02x(%s) %s", this, this->link, this->path,
	     this->mfd, c, charstr,
	     (ascii_isprintable(c)) ? "" : "^^^^ NOT PRINTABLE ^^^^");
      if (ts) fprintf(stderr, "@%ld:%ld\n", ts->tv_sec, ts->tv_nsec);
      else fprintf(stderr, "\n");
    }
  } else {
    // n==0
    EPRINT("write returned unexpected value?? n=%d\n", n);
    NYI;
  }
  return n;
}

extern void
ttyPortSpace(tty_t *this, int *min, int *mout, int *sin, int *sout)
{
  assert(ioctl(this->mfd, TIOCINQ, min)==0);
  assert(ioctl(this->mfd, TIOCOUTQ, mout)==0);
  assert(ioctl(this->sfd, TIOCINQ, sin)==0);
  assert(ioctl(this->sfd, TIOCOUTQ, sout)==0);
}

extern int
ttyReadChar(tty_t *this, char *c, struct timespec *ts, double delay)
{
  struct timespec now;
  if (clock_gettime(CLOCK_SOURCE, &now) == -1) {
    perror("clock_gettime");
    NYI;
  }
  if (ts && (delay > 0.0)) {
    double diff;
    diff = (now.tv_sec - ts->tv_sec) + 
      (now.tv_nsec - ts->tv_nsec) / (double)NSEC_IN_SECOND;
    if (diff < delay) {
      this->delaycnt++;
      return 0;
    }
  }
  int n = read(this->mfd, c, 1);
  
  if (n==1) {
    if (verbose(3)) {
	asciistr_t charstr;
	ascii_char2str((int)(*c), charstr);
	VPRINT("  %p:%s(%s) fd:%d c:%02x(%s)\n", this, this->link,
	       this->path, this->mfd, *c, charstr);
      }
    this->rbytes++;
  } else {
    VLPRINT(2, "  read failed?? %d\n", n);
  }
  return n;
}

extern bool
ttyCleanup(tty_t *this)
{
  assert(this);
  VLPRINT(1, "closing: %p\n", this);
  if (verbose(1)) {
    ttyDump(this, stderr, NULL);
  }
  if (this->mfd == -1) return true;
  
  if (this->mfd  != -1 && close(this->mfd) != 0) perror("close tty->mfd");
  if (this->sfd  != -1 && close(this->sfd) != 0) perror("close tty->sfd"); 
  if (this->ifd  != -1 && close(this->ifd) != 0) perror("close tty->ifd");
  if (this->link != NULL && this->link[0] !=  0 && unlink(this->link) != 0) {
    perror("unlink tty->link");
  }
  
  this->mfd     = -1;
  this->sfd     = -1;
  this->ifd     = -1;
  this->path[0] =  0;
  this->rbytes  =  0;
  this->wbytes  =  0;
  this->readers =  0;
  this->opens   =  0;

  return true;
}
