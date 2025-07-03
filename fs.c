
#include <inttypes.h>

#include "yar.h"

// JA: FIXME the following needs to be used
//    in all fs operations right things are just
//    hard coded
fs_file_t fs_files[] = {
  [0] =         { .name =  NULL,   .ino = -1, .usage = NULL },
  #define DOT_INO 1
  [DOT_INO] =   { .name = ".",     .ino = DOT_INO, .usage = NULL },
  #define CMDS_INO 2
  [CMDS_INO] =  { .name = "cmds",  .ino = CMDS_INO,
  .usage = "reading this file returns the names of the current command lines."},
  #define LCMDS_INO 3
  [LCMDS_INO] = { .name = "lcmds", .ino = LCMDS_INO,
  .usage = NULL},
  { .name =  NULL, .usage = NULL, .ino = -1 },
};

extern void
fsusage(FILE *fp)
{
  for (int i=1;;i++) {
    if (fs_files[i].name == NULL) return;
    else {
      if (fs_files[i].usage) {
	if (fp == GBLS.mon.fileptr) {
	  monprintf("\t%s\t%s\n", fs_files[i].name, fs_files[i].usage);
	} else {
	  fprintf(fp, "\t%s\t%s\n", fs_files[i].name, fs_files[i].usage);
	}
      }
    }
  }
}

static off_t cmdNamesSize()
{
  cmd_t *cmd, *tmp;
  off_t n = 0;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n+=strlen(cmd->name)+1;
  }
  return n;
}

static int fs_stat(fuse_ino_t ino, struct stat *stbuf)
{
  stbuf->st_ino = ino;
  switch (ino) {
  case 1:
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    break;

  case CMDS_INO:
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = cmdNamesSize();
    break;

  default:
    return -1;
  }
  return 0;
}

static void fs_ll_init(void *userdata, struct fuse_conn_info *conn)
{
  (void)userdata;

  /* Disable the receiving and processing of FUSE_INTERRUPT requests */
  //	conn->no_interrupt = 1;

  /* Test setting flags the old way */
  conn->want = FUSE_CAP_ASYNC_READ;
  conn->want &= ~FUSE_CAP_ASYNC_READ;
}

static void fs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
  struct stat stbuf;

  (void) fi;

  memset(&stbuf, 0, sizeof(stbuf));
  if (fs_stat(ino, &stbuf) == -1)
    fuse_reply_err(req, ENOENT);
  else
    fuse_reply_attr(req, &stbuf, 1.0);
}

static void fs_ll_lookup(fuse_req_t req, fuse_ino_t parent,
			      const char *name)
{
  struct fuse_entry_param e;

  if (parent != 1 || strcmp(name, fs_files[CMDS_INO].name) != 0)
    fuse_reply_err(req, ENOENT);
  else {
    memset(&e, 0, sizeof(e));
    e.ino = 2;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    fs_stat(e.ino, &e.attr);

    fuse_reply_entry(req, &e);
  }
}

struct dirbuf {
  char *p;
  size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino)
{
  struct stat stbuf;
  size_t oldsize = b->size;
  b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
  b->p = (char *) realloc(b->p, b->size);
  memset(&stbuf, 0, sizeof(stbuf));
  stbuf.st_ino = ino;
  fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
		    b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off,
			  min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

static void fs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
  (void) fi;

  if (ino != 1)
    fuse_reply_err(req, ENOTDIR);
  else {
    struct dirbuf b;

    memset(&b, 0, sizeof(b));
    dirbuf_add(req, &b, ".", 1);
    dirbuf_add(req, &b, "..", 1);
    dirbuf_add(req, &b, fs_files[CMDS_INO].name, CMDS_INO);
    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
  }
}

static void fs_ll_open(fuse_req_t req, fuse_ino_t ino,
			    struct fuse_file_info *fi)
{
  if (ino != 2)
    fuse_reply_err(req, EISDIR);
  else if ((fi->flags & O_ACCMODE) != O_RDONLY)
    fuse_reply_err(req, EACCES);
  else
    fuse_reply_open(req, fi);
}


static void fs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			    off_t off, struct fuse_file_info *fi)
{
  (void) fi;

  assert(ino == 2);
  off_t  n = cmdNamesSize();
  char *next, *buf = malloc(n);
  cmd_t *cmd, *tmp;

  next=buf;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    next=stpcpy(next, cmd->name);
    *next='\n';
    next++;
  }

  int rc=reply_buf_limited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
}

static void fs_ll_getxattr(fuse_req_t req, fuse_ino_t ino,
			    const char *name, size_t size)
{
  (void)size;
  assert(ino == 1 || ino == 2);
  if (strcmp(name, "fs_ll_getxattr_name") == 0)
    {
      const char *buf = "fs_ll_getxattr_value";
      fuse_reply_buf(req, buf, strlen(buf));
    }
  else
    {
      fuse_reply_err(req, ENOTSUP);
    }
}

static void fs_ll_setxattr(fuse_req_t req, fuse_ino_t ino,
			   const char *name, const char *value,
			   size_t size, int flags)
{
  (void)flags;
  (void)size;
  assert(ino == 1 || ino == 2);
  const char* exp_val = "fs_ll_setxattr_value";
  if (strcmp(name, "fs_ll_setxattr_name") == 0 &&
      strlen(exp_val) == size &&
      strncmp(value, exp_val, size) == 0)
    {
      fuse_reply_err(req, 0);
    }
  else
    {
      fuse_reply_err(req, ENOTSUP);
    }
}

static void fs_ll_removexattr(fuse_req_t req, fuse_ino_t ino,
			      const char *name)
{
  assert(ino == 1 || ino == 2);
  if (strcmp(name, "fs_ll_removexattr_name") == 0)
    {
      fuse_reply_err(req, 0);
    }
  else
    {
      fuse_reply_err(req, ENOTSUP);
    }
}

static const struct fuse_lowlevel_ops fs_ll_oper = {
  .init        = fs_ll_init,
  .lookup      = fs_ll_lookup,
  .getattr     = fs_ll_getattr,
  .readdir     = fs_ll_readdir,
  .open        = fs_ll_open,
  .read        = fs_ll_read,
  .setxattr    = fs_ll_setxattr,
  .getxattr    = fs_ll_getxattr,
  .removexattr = fs_ll_removexattr,
};


static evnthdlrrc_t
fsEvent(void *obj, uint32_t evnts, int epollfd)
{
  fs_t *this = obj;
  assert(this == &GBLS.fs);
  int     fd = this->fuse_fd;

  VLPRINT(3,"START: fs: fd:%d evnts:0x%08x\n", fd, evnts);
  if (evnts & EPOLLIN) {
    int n = fuse_session_receive_buf(this->fuse_se, &(this->fuse_buf));
    if (n>0) fuse_session_process_buf(this->fuse_se, &(this->fuse_buf));
    evnts = evnts & ~EPOLLIN;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLHUP) {
    VLPRINT(2,"EPOLLHUP(%x)\n", EPOLLHUP);
    evnts = evnts & ~EPOLLHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLRDHUP) {
    VLPRINT(2,"EPOLLRDHUP(%x)\n", EPOLLRDHUP);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts & EPOLLERR) {
    VLPRINT(2,"EPOLLERR(%x)\n", EPOLLERR);
    evnts = evnts & ~EPOLLRDHUP;
    if (evnts==0) goto done;
  }
  if (evnts != 0) {
    VLPRINT(2,"unknown events evnts:%x", evnts);
  }
 done:
  VLPRINT(3, "END: fs: fd:%d evnts:0x%08x\n", fd, evnts);
  return EVNT_HDLR_SUCCESS;
}

extern bool
fsRegisterEvents(fs_t *this, int epollfd)
{
  int fd;
  struct epoll_event ev;

  ASSERT(this && this->fuse_se && this->fuse_fd != -1);
  fd = this->fuse_fd;
  ev.events   = EPOLLIN |  EPOLLHUP | EPOLLRDHUP | EPOLLERR; // Level 
  ev.data.ptr = &this->ed;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
    perror("epoll_ctl: fs->fuse_fd");
    return false;
  }
  return true;
}

extern bool
fsCreate(fs_t *this, char *name)
{
  ASSERT(this);
  // mkdir
  if (mkdir(this->mntpt, 0700)<0) {
    perror("fsCreate: mkdir");
    return false;
  }
  this->mkdir = true;
  fuse_opt_add_arg(&this->fuse_args, name);
	
  this->fuse_se = fuse_session_new(&this->fuse_args, &fs_ll_oper,
				   sizeof(fs_ll_oper), NULL);
  assert(this->fuse_se);

  if (fuse_session_mount(this->fuse_se, this->mntpt) != 0) {
    fprintf(stderr, "Error: fuse_session_mount() failed\n");
    return false;
  }
  
  this->fuse_fd = fuse_session_fd(this->fuse_se);
  assert(this->fuse_fd != -1);
  fcntl(this->fuse_fd, F_SETFD, FD_CLOEXEC);

  assert(this->ed.obj == this && this->ed.hdlr == fsEvent);
  return true;
}

static bool
fsMountPoint(char *mntpt, int n, char *dir)
{
  int rc;
  if (dir) {
    rc = snprintf(mntpt, n, "%s/%" PRIdMAX ".fs", dir, (intmax_t)GBLS.pid);
  } else {
    rc = snprintf(mntpt, n, "%" PRIdMAX ".fs",  (intmax_t)GBLS.pid);    
  }
  if (rc < 0 || rc >= 1024) {
    perror("snprintf"); 
    return false;
  }
  return true;
}

static bool
fsSetMntPt(fs_t *this, char *mntpt)
{
  assert(this);
  assert(this->mntpt==NULL);
  if (mntpt==NULL) { this->mntpt = NULL; return true; }
  // WE ASSUME mntpt is a properly null terminated string
  this->mntpt = strdup(mntpt);
  assert(this->mntpt);
  return true;
}

extern bool
fsInit(fs_t *this, char *mntptrdir)
{
  char tmp[1024];
  bzero(this, sizeof(fs_t));
  *this = (fs_t){ .fuse_args = FUSE_ARGS_INIT(0, NULL),
		  .fuse_fd = -1,
		  .mkdir = false };
  if (!fsMountPoint(tmp, 1024, NULL)) EEXIT();
  if (!fsSetMntPt(this, tmp)) EEXIT();
  this->ed = (evntdesc_t){ .obj = this, .hdlr = fsEvent };
  return true;
}

extern bool
fsCleanup(fs_t *this)
{
  if (this->mntpt && this->mkdir) {  
    if (this->fuse_se) {
      fuse_session_unmount(this->fuse_se);
      fuse_session_destroy(this->fuse_se);
      this->fuse_se = NULL;
      fuse_opt_free_args(&this->fuse_args);
      this->fuse_args = (struct fuse_args)FUSE_ARGS_INIT(0, NULL);
    }
    if (this->fuse_buf.mem) free(this->fuse_buf.mem);
    rmdir(this->mntpt);
    if (this->mntpt) free(this->mntpt);
    this->mntpt = NULL;
  }
  return true;
}
