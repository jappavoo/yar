#include "yar.h"
#include <pty/pty_master_open.h>

extern void
ttyDump(tty_t *this, FILE *f, char *prefix)
{
  fprintf(f, "%stty: this=0x%p path=%s link=%s fd=%d ccnt=%d\n", prefix, this,
	  this->path, this->link, this->fd, this->ccnt);
}

extern bool
ttyInit(tty_t *this, char *ttylink)
{
  this->path[0] =  0;
  this->link    =  ttylink;
  this->rbytes  =  0;
  this->wbytes  =  0;
  this->fd      = -1;
  this->ifd     = -1;
  this->ccnt    =  0;
  return true;
}

extern bool
ttyCreate(tty_t *this)
{
  VLPRINT(1, "this=0x%p\n", this);

  assert(this);
  assert(this->fd == -1);

  if (this->link != NULL && access(this->link, F_OK)==0) {
    EPRINT("%s already exists\n", this->link);
    return false;
  }
  
  this->fd = ptyMasterOpen(this->path, TTY_MAX_PATH);

  if (this->fd == -1) {
    perror("ptyMasterOpen failed:");
    return false;
  }

  if (this->link != NULL) {
    VLPRINT(2, "linking %s->%s\n", this->path, this->link);
    int rc =symlink(this->path, this->link);
    if (rc!=0) {
      perror("failed tty link create failed");
      return false;
    }
  }
  
  this->rbytes = 0;
  this->wbytes = 0;
  this->ccnt   = 0;
  
  if (verbose(1)) ttyDump(this, stderr, "  Created ");
  return true;
}

extern bool
ttyCleanup(tty_t *this)
{
  assert(this);
  VLPRINT(1, "closing: 0x%p\n", this);
  if (verbose(1)) {
    ttyDump(this, stderr, NULL);
  }
  if (this->fd == -1) return true;
  
  if (this->fd      != -1 && close(this->fd) != 0) perror("close tty->fd"); 
  if (this->ifd     != -1 && close(this->ifd) != 0) perror("close tty->ifd");
  if (this->link[0] !=  0 && unlink(this->link) != 0) perror("unlink tty->link");
  
  this->fd      = -1;
  this->ifd     = -1;
  this->path[0] =  0;
  this->rbytes  =  0;
  this->wbytes  =  0;
  this->ccnt    =  0;

  return true;
}
