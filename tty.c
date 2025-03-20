#include "yar.h"
#include <pty/pty_master_open.h>
#include <tty/tty_functions.h>

extern void
ttyDump(tty_t *this, FILE *f, char *prefix)
{
  fprintf(f, "%stty: this=0x%p path=%s link=%s \n"
             "       mfd=%d sfd=%d ifd=%d\n"
	     "       rbytes=%lu wbytes=%lu ccnt=%d\n",
	  prefix, this,  this->path, this->link,
	  this->mfd, this->sfd, this->ifd, this->rbytes, this->wbytes,
	  this->ccnt);
}

extern bool
ttyInit(tty_t *this, char *ttylink)
{
  this->path[0] =  0;
  this->link    =  ttylink;
  this->rbytes  =  0;
  this->wbytes  =  0;
  this->mfd     = -1;
  this->sfd     = -1;
  this->ifd     = -1;
  this->ccnt    =  0;
  return true;
}

extern bool
ttyCreate(tty_t *this, bool raw)
{
  int sfd;
  VLPRINT(1, "this=0x%p\n", this);

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

  this->sfd    = sfd;
  this->rbytes = 0;
  this->wbytes = 0;
  this->ccnt   = 0;
  
  if (verbose(1)) ttyDump(this, stderr, "  Created ");
  return true;
}

extern int
ttyWriteChar(tty_t *this, char c)
{
  VLPRINT(2, "0x%p: %02x(%c)\n", this, c, c);
  int n = write(this->mfd, &c, 1);
  if (n==1) this->wbytes++;  else VLPRINT(2, "write failed?? %d\n", n);
  return n;
}

extern int
ttyReadChar(tty_t *this, char *c)
{
  int n = read(this->mfd, c, 1);
  if (n==1) {
    VLPRINT(2, "0x%p: %02x(%c)\n", this, *c, *c);
    this->rbytes++;
  } else {
    VLPRINT(2, "read failed?? %d\n", n);
  }
  return n;
}

extern bool
ttyCleanup(tty_t *this)
{
  assert(this);
  VLPRINT(1, "closing: 0x%p\n", this);
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
  this->ccnt    =  0;

  return true;
}
