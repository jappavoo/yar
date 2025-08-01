#include "yar.h"
#include <pty/pty_master_open.h>
#include <pty/pty_fork.h>
#include <tty/tty_functions.h>
#include <fcntl.h>

extern void
ttyDump(tty_t *this, FILE *f, char *prefix)
{
  int din, dout, sin, sout;
  ttyPortSpace(this, &din, &dout, &sin, &sout);
  fprintf(f, "%stty: this=%p path=%s link=%s extra1.src=%s extra1.dst=%s"
	  "extra2.src=%s extra2.dst=%s\n"
             "       dfd=%d sfd=%d ifd=%d iwd=%d\n"
	  "       rbytes=%lu wbytes=%lu wdbytes=%lu opens=%d domInQ=%d domout=%d"
	  " subInQ=%d subOut=%d\n",
	  prefix, this,  this->path, this->link,
	  this->extralink1.src, this->extralink1.dst,
	  this->extralink2.src, this->extralink2.dst,
	  this->dfd, this->sfd, this->ifd, this->iwd,
	  this->rbytes, this->wbytes, this->wdbytes, this->opens, din, dout,
	  sin, sout);
  if (this->wdbytes) {
    hexdump(f, (uint8_t*)(this->discards),
	    (this->wdbytes<sizeof(this->discards)) ? this->wdbytes :
	    sizeof(this->discards));
  }
	    
}

static evnthdlrrc_t
ttyNotifyEvent(void *obj, uint32_t evnts, int epollfd)
{
  tty_t * this = obj;
  
  if (verbose(2)) {
    ttyDump(this, stderr, "ttyNotifyEvent on:\n  ");
  }
  if (evnts & EPOLLIN) {
    VLPRINT(2, "EPOLLIN(%x)\n", EPOLLIN);
    struct inotify_event iev;
    uint32_t ievents;
    ssize_t len = read(this->ifd, &iev, sizeof(iev));
    if (len != sizeof(iev)) {
      perror("read");
      NYI;
    }
    ASSERT(iev.wd == this->iwd);
    ievents = iev.mask;
    switch (ievents) {
    case IN_OPEN:
      this->opens++;
      VLPRINT(1, "%s: %s(%s): OPENED: opens=%d\n",
	      (ttyIsCmdtty(this)) ? "cmdtty" : "clttty", this->link, this->path,
	      this->opens);
      ievents = ievents & ~IN_OPEN;
      break;
    case IN_CLOSE_WRITE:
    case IN_CLOSE_NOWRITE:
      // seem to be getting a close without having seen an open ...
      // lets assume we lost the open but the close is real so go ahead
      // and pass it along to down stream handlers and assume they can deal
      if (this->opens>0) this->opens--;
      else {
	EPRINT(stderr, "ERROR: %s: %s(%s): got a close when opens=0\n",
	       (ttyIsCmdtty(this)) ? "cmdtty" : "clttty", this->link, this->path
	       );
      }
      VLPRINT(1, "%s: %s(%s): CLOSED: opens=%d\n",
	      (ttyIsCmdtty(this)) ? "cmdtty" : "clttty", this->link, this->path,
	      this->opens);
      assert(this->opens >= 0);
      ievents = ievents & ~IN_CLOSE;
      break;
    default:
      EPRINT(stderr, "Unexpected notify case: iev.mask:0x%x ievents:0x%x\n",
	     iev.mask, ievents);
      NYI;
    }
    assert(ievents == 0);
    // we have handled the notify event interally call external notify handler
    // if we have one registered
    if (this->ned.hdlr) {
      this->ned.hdlr(this->ned.obj, iev.mask, epollfd); 
    }
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    VLPRINT(2, "EPOLLHUP(%x)\n", EPOLLHUP);
    assert(0);
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2, "EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    assert(0);
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2, "EPOLLERR(%x)\n", EPOLLERR);
    assert(0);
  }
  if (evnts != 0) {
    VLPRINT(2, "unknown events evnts:%x", evnts);
    assert(0);
  }
 done:
  return EVNT_HDLR_SUCCESS;
}

extern bool
ttyInit(tty_t *this, char *ttylink, char *extra1src, char *extra1dest,
	char *extra2src, char *extra2dest,
	bool iszeroed)
{
  if (!iszeroed) bzero(this, sizeof(tty_t));
  // all other values are now zeroed 
  // a non null tty link means the tty is a client tty see ttyIsClient in .h
  assert(this->link == NULL); // there has been a lot of churn so for my sanity
  this->link     = (ttylink) ? strdup(ttylink) : NULL;
  this->extralink1.src = (extra1src) ? strdup(extra1src) : NULL;  
  this->extralink1.dst = (extra1dest) ? strdup(extra1dest) : NULL;
  this->extralink2.src = (extra2src) ? strdup(extra2src) : NULL;  
  this->extralink2.dst = (extra2dest) ? strdup(extra2dest) : NULL;
  this->dfd      = -1;
  this->sfd      = -1;
  this->ifd      = -1;
  this->iwd      = -1;   // iwatch descriptor is not and fd
  return true;
}

extern bool
ttyCreate(tty_t *this, evntdesc_t ed, evntdesc_t ned, bool raw)
{
  int sfd;
  VLPRINT(1, "this=%p\n", this);

  ASSERT(this && this->dfd == -1);

  if (this->link != NULL && access(this->link, F_OK)==0) {
    EPRINT(stderr, "%s already exists\n", this->link);
    goto cleanup;
  }

  if (this->extralink1.src != NULL && access(this->extralink1.src, F_OK)==0) {
    EPRINT(stderr, "%s already exists\n", this->extralink1.src);
    goto cleanup;
  }

  if (this->extralink2.src != NULL && access(this->extralink2.src, F_OK)==0) {
    EPRINT(stderr, "%s already exists\n", this->extralink2.src);
    goto cleanup;
  }

  this->dfd = ptyMasterOpen(this->path, TTY_MAX_PATH);
  if (this->dfd == -1) {
    perror("ptyMasterOpen failed:");
    goto cleanup;
  }
  fcntl(this->dfd, F_SETFD, FD_CLOEXEC);

  // we use non blocking reads and watch for EAGAIN on writes
  // to see when we have exhausted the kernel tty port buffer
  fdSetnonblocking(this->dfd);
  
  // we open the sub side and keep it open to ensure
  // that if our clients or commands come and got via opens and close
  // of the sub side the su-tty and its persistent state
  // buffer and flags will survive.
  // NOTE: We do this before we start tracking opens ... so this open is not 
  //       reflected in opens count! Normal state for ttys.opens is 0
  sfd = open(this->path, O_RDWR);
  if (sfd == -1) {
    perror("client side of pty could not be opened");
    goto cleanup;
  }
  assert(fcntl(sfd, F_SETFD, FD_CLOEXEC)!=-1);
  
  if (raw) {
    ttySetRaw(sfd, NULL);
  }

  // we use non blocking reads and watch for EAGAIN 
  // to see when we have drained the clinet tty
  fdSetnonblocking(sfd);

  // create file system link to the sub-tty (it is a client tty)
  if (this->link != NULL) {
    VLPRINT(2, "linking %s->%s\n", this->path, this->link);
    int rc =symlink(this->path, this->link);
    if (rc!=0) {
      perror("failed tty link create failed");
      goto cleanup;
    }
  }

  // create extra link if specified
  if (this->extralink1.src != NULL) {
    assert(this->extralink2.dst != NULL);
    VLPRINT(2, "linking %s->%s\n", this->extralink1.src, this->extralink1.dst);
    int rc =symlink(this->extralink1.dst, this->extralink1.src);
    if (rc!=0) {
      perror("failed tty extra link 1 create failed");
      goto cleanup;
    }    
  }

  // create extra link if specified
  if (this->extralink2.src != NULL) {
    assert(this->extralink2.dst != NULL);
    VLPRINT(2, "linking %s->%s\n", this->extralink2.src, this->extralink2.dst);
    int rc =symlink(this->extralink2.dst, this->extralink2.src);
    if (rc!=0) {
      perror("failed tty extra link 2 create failed");
      goto cleanup;
    }    
  }

  // setup register for inotify events on the pts to track opens and closes 
  this->ifd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
  if (this->ifd == -1) {
    perror("inotify_init1");
    goto cleanup;
  }
  this->iwd = inotify_add_watch(this->ifd, this->path, IN_OPEN | IN_CLOSE);
  if (this->iwd == -1) {
    perror("inotify_add_watch");
    goto cleanup;
  }
  
  this->sfd     = sfd;
  this->rbytes  = 0;
  this->wbytes  = 0;
  this->opens   = 0;

  // setup event descriptors to so events to this tty will be handled
  // correctly
  this->dfded = ed;
  this->ifded = (evntdesc_t){ .hdlr = ttyNotifyEvent, .obj = this };
  this->ned   = ned;
  
  if (verbose(1)) ttyDump(this, stderr, "  Created ");
  return true;
 cleanup:
  ttyCleanup(this);
  return false;
}

extern bool
ttyRegisterEvents(tty_t *this, int epollfd)
{
  int fd;
  struct epoll_event ev;

  // 1) Register IO events from the tty's dom-tty
  //    (eg. allows us to detect data arrival and errors from the sub-tty
  ASSERT(this && this->dfd != -1);
  fd = this->dfd;
  ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // Level 
  ev.data.ptr = &this->dfded;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
    perror("epoll_ctl: cmd->cmdtty.dfd");
    return false;
  }

  // 2) Register for inotify events (open and closes) for  
  //    tty's sub tty file path (eg. /dev/pts/XX)
  // we do this for both cmd and clt ttys -- for cmd tty's we typically
  // expect one open and close but really depends on what the cmd does
  ASSERT(this->ifd != -1 && this->iwd != -1);
  fd = this->ifd;
  ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // Level 
  ev.data.ptr = &this->ifded;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
    perror("epoll_ctl: cmd->cmdtty.dfd");
    return false;
  }

  return true;
}

extern int
ttyWriteBuf(tty_t *this, char *buf, int len,  struct timespec *ts)
{
  int n=0;
  
  if (this->opens != 0) {
    n = write(this->dfd, buf, len);
    if (n==-1) {
      if (errno==EAGAIN) {
	// write out of space??? let -1 return to caller to handle
	VLPRINT(2, "%s(%s) client is a slow child be kind\n", this->link,
		this->path);
      }  else {
	perror("ttyWriteChar write failed");
	NYI;
      }
    } else if (n==len) {
      // success mark the time of the write if needed
      if (ts) {
	if (clock_gettime(CLOCK_SOURCE, ts) == -1) {
	  perror("clock_gettime");
	  NYI;
	}
      }
      // book keeping
      this->wbytes+=n;
      if (verbose(2)) {
	asciistr_t charstr;
	ascii_char2str((int)buf[0], charstr);
	VPRINT("  %p:%s(%s): fd:%d n=%d buf[0]:%02x(%s) %s", this, this->link,
	       this->path, this->dfd, n, buf[0], charstr,
	       (ascii_isprintable(buf[0])) ? "" : "^^^^ NOT PRINTABLE ^^^^");
	if (ts) fprintf(stderr, "@%ld:%ld\n", ts->tv_sec, ts->tv_nsec);
	else fprintf(stderr, "\n");
      }
    } else {
      // n==0
      EPRINT(stderr, "write returned unexpected value?? n=%d\n", n);
      NYI;
    }
  } else {
    int used = this->wdbytes;
    int free = sizeof(this->discards)-used;
    if (free<0) free=0;
    int nb = (len<=free) ? len : free;
    
    // no one is listening so pretend the write succeeded
    memcpy(&(this->discards[used]), buf, nb);
    this->wdbytes += len;		   
    n=len;
  }
  return n;
}

extern void
ttyPortSpace(tty_t *this, int *din, int *dout, int *sin, int *sout)
{
  if (this->dfd != -1) assert(ioctl(this->dfd, TIOCINQ, din)==0);
  if (this->dfd != -1) assert(ioctl(this->dfd, TIOCOUTQ, dout)==0);
  if (this->sfd != -1) assert(ioctl(this->sfd, TIOCINQ, sin)==0);
  if (this->sfd != -1) assert(ioctl(this->sfd, TIOCOUTQ, sout)==0);
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
  int n = read(this->dfd, c, 1);
  
  if (n==1) {
    if (verbose(3)) {
	asciistr_t charstr;
	ascii_char2str((int)(*c), charstr);
	VPRINT("  %p:%s(%s) fd:%d c:%02x(%s)\n", this, this->link,
	       this->path, this->dfd, *c, charstr);
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
  VPRINT("%p: %s %s\n", this, this->path, this->link);
  if (verbose(2)) {
    ttyDump(this, stderr, NULL);
  }
  
  if (this->ifd  != -1 && close(this->ifd) != 0) perror("close tty->ifd");
  if (this->dfd  != -1 && close(this->dfd) != 0) perror("close tty->dfd");
  if (this->sfd  != -1 && close(this->sfd) != 0) perror("close tty->sfd"); 
  if (this->link != NULL) {
    if (this->link[0] != '\0') {
      if (unlink(this->link) != 0) {
	perror("unlink tty->link");
      }
    }
    free(this->link);
  }
  
  if (this->extralink1.src != NULL) {
    if (this->extralink1.src[0] != '\0') {
      if (unlink(this->extralink1.src) != 0) {
	perror("unlink tty->extralink1.src");
      }
    }
    free(this->extralink1.src);
  }
  if (this->extralink1.dst != NULL) free(this->extralink1.dst);

  if (this->extralink2.src != NULL) {
    if (this->extralink2.src[0] != '\0') {
      if (unlink(this->extralink2.src) != 0) {
	perror("unlink tty->extralink2.src");
      }
    }
    free(this->extralink2.src);
  }
  if (this->extralink2.dst != NULL) free(this->extralink2.dst);
  
  // reset values
  bzero(this, sizeof(tty_t));
  this->dfd     = -1;
  this->sfd     = -1;
  this->iwd     = -1;   // iwatch descriptor is not an FD
  this->ifd     = -1;
  return true;
}
