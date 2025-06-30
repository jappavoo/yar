/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file GPL2.txt.
*/

/** @file
 *
 * minimal example filesystem using low-level API
 *
 * Compile with:
 *
 *     gcc -Wall cmds_ll.c `pkg-config fuse3 --cflags --libs` -o cmds_ll
 *
 * Note: If the pkg-config command fails due to the absence of the fuse3.pc
 *     file, you should configure the path to the fuse3.pc file in the
 *     PKG_CONFIG_PATH variable.
 *
 * ## Source code ##
 * \include cmds_ll.c
 */

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <time.h>

static const char *cmds_str = "cmd0\ncmd1\ncmd2\n";
static const char *cmds_name = "cmds";

static int cmds_stat(fuse_ino_t ino, struct stat *stbuf)
{
	stbuf->st_ino = ino;
	switch (ino) {
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;

	case 2:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(cmds_str);
		break;

	default:
		return -1;
	}
	return 0;
}

static void cmds_ll_init(void *userdata, struct fuse_conn_info *conn)
{
	(void)userdata;

	/* Disable the receiving and processing of FUSE_INTERRUPT requests */
	//	conn->no_interrupt = 1;

	/* Test setting flags the old way */
	conn->want = FUSE_CAP_ASYNC_READ;
	conn->want &= ~FUSE_CAP_ASYNC_READ;
}

static void cmds_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (cmds_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void cmds_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	if (parent != 1 || strcmp(name, cmds_name) != 0)
		fuse_reply_err(req, ENOENT);
	else {
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;
		cmds_stat(e.ino, &e.attr);

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

static void cmds_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
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
		dirbuf_add(req, &b, cmds_name, 2);
		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}
}

static void cmds_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & O_ACCMODE) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void cmds_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void) fi;

	assert(ino == 2);
	reply_buf_limited(req, cmds_str, strlen(cmds_str), off, size);
}

static void cmds_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  size_t size)
{
	(void)size;
	assert(ino == 1 || ino == 2);
	if (strcmp(name, "cmds_ll_getxattr_name") == 0)
	{
		const char *buf = "cmds_ll_getxattr_value";
		fuse_reply_buf(req, buf, strlen(buf));
	}
	else
	{
		fuse_reply_err(req, ENOTSUP);
	}
}

static void cmds_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  const char *value, size_t size, int flags)
{
	(void)flags;
	(void)size;
	assert(ino == 1 || ino == 2);
	const char* exp_val = "cmds_ll_setxattr_value";
	if (strcmp(name, "cmds_ll_setxattr_name") == 0 &&
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

static void cmds_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	assert(ino == 1 || ino == 2);
	if (strcmp(name, "cmds_ll_removexattr_name") == 0)
	{
		fuse_reply_err(req, 0);
	}
	else
	{
		fuse_reply_err(req, ENOTSUP);
	}
}

static const struct fuse_lowlevel_ops cmds_ll_oper = {
	.init = cmds_ll_init,
	.lookup = cmds_ll_lookup,
	.getattr = cmds_ll_getattr,
	.readdir = cmds_ll_readdir,
	.open = cmds_ll_open,
	.read = cmds_ll_read,
	.setxattr = cmds_ll_setxattr,
	.getxattr = cmds_ll_getxattr,
	.removexattr = cmds_ll_removexattr,
};

#define EPOLL_MAX_EVENTS 10
#define MOUNTPOINT "mt"

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(1, argv);
	struct fuse_session *se;
	int ret = -1;
	int epoll_fd = -1;
	
	se = fuse_session_new(&args, &cmds_ll_oper,
			      sizeof(cmds_ll_oper), NULL);
	if (se == NULL) {
	  fprintf(stderr, "Error: Unable to create FUSE session\n");
	  goto cleanup_args;
	}

	if (fuse_session_mount(se, MOUNTPOINT) != 0) {
	  fprintf(stderr, "Error: fuse_session_mount() failed\n");
	  goto cleanup_mount;
	}

	/* Create an epoll instance */
	epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
	  perror("epoll_create1");
	  goto cleanup_mount;
	}
	
	/* Retrieve the FUSE file descriptor and add it to the epoll instance */
	int fuse_fd = fuse_session_fd(se);
	{
	  struct epoll_event ev;
	  memset(&ev, 0, sizeof(ev));
	  ev.events = EPOLLIN;
	  ev.data.fd = fuse_fd;
	  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fuse_fd, &ev) == -1) {
	    perror("epoll_ctl");
	    goto cleanup_epoll;
	  }
	}

	
	printf("FUSE filesystem mounted on %s.\n", MOUNTPOINT);
	printf("Press Ctrl+C or fusermount3 -u %s to unmount.\n", MOUNTPOINT);

	struct fuse_buf buf = { .mem = NULL };
	/* Main event loop: Wait on epoll for events and process FUSE requests */
	while (!fuse_session_exited(se)) {
	  struct epoll_event events[EPOLL_MAX_EVENTS];
	  int nfds = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
	  if (nfds == -1) {
	    if (errno == EINTR)
	      continue;
	    perror("epoll_wait");
	    break;
	  }
	  
	  for (int i = 0; i < nfds; i++) {
	    if (events[i].data.fd == fuse_fd) {
	      int n = fuse_session_receive_buf(se, &buf);
	      if (n == -EINTR) continue;
	      if (n <=0)  break;
	      fuse_session_process_buf(se, &buf);
	    }
	  }
	}
	free(buf.mem);
	ret = 0;
 cleanup_epoll:
	if (epoll_fd != -1)
	  close(epoll_fd);
 cleanup_mount:
	fuse_session_unmount(se);
	fuse_session_destroy(se);
 cleanup_args:
	fuse_opt_free_args(&args);
	return ret;
}
