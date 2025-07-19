#include "yar.h"

/************************ YAR SPECIFIC FS OBJECTS *****************************
 * /cmds  : readonly file : contents is current command names
 * /lcmds : readonly file : contents is detailed long listing of current commands
 * /bcst  : readonly file : path of broadcast tty if enabled
 ******************************************************************************/
void
yarfsUsage(FILE *fp)
{
  fprintf(fp,
	  " /cmds  : readonly file : contents is current command names\n"
	  " /lcmds : readonly file : contents is detailed long listing of"
	  " current commands\n"
	  " /bcst  : readonly file : path of broadcast tty if enabled\n");
}

/*** /cmds ***/
static off_t cmdNamesSize()
{
  cmd_t *cmd, *tmp;
  off_t n = 0;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n+=strlen(cmd->name)+1;
  }
  return n;
}

static bool
fs_cmds_stat(fs_t *this, fs_file_t *file, struct stat *stbuf)
{
  VLPRINT(2, "%s %ld: ", file->name, file->ino);
  stbuf->st_ino = file->ino;
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = cmdNamesSize();
  VLPRINT(2, "%ld\n", stbuf->st_size);
  return true; 
}

static bool
fs_cmds_read(fs_t *this, fs_file_t *file, fuse_req_t req, size_t size,
			    off_t off)
{
  off_t  n = cmdNamesSize();
  char *next, *buf = malloc(n);
  cmd_t *cmd, *tmp;
  
  next=buf;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    next=stpcpy(next, cmd->name);
    *next='\n';
    next++;
  }

  int rc=fsFuseReplyBufLimited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
  return true;
}

fs_fileops_t fs_cmds_ops = {
  .stat    = fs_cmds_stat,
  .open    = NULL,
  .read    = fs_cmds_read,
  .write   = NULL,
  .readdir = NULL 
};

/*** /lcmds ***/
static off_t cmdsInfoSize()
{
  cmd_t *cmd, *tmp;
  off_t n = 0;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n+=strlen(cmd->name)+1;          // +1 for comma
    n+=strlen(cmd->clttty.link)+1;   // +1 for comma
    n+=strlen(cmd->cmdline)+1;       // +1 for newline
  }
  return n;
}

static bool
fs_lcmds_stat(fs_t *this, fs_file_t *file, struct stat *stbuf)
{
  VLPRINT(2, "%s %ld: ", file->name, file->ino);
  stbuf->st_ino = file->ino;
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = cmdsInfoSize();
  VLPRINT(2, "%ld\n", stbuf->st_size);
  return true; 
}

static bool
fs_lcmds_read(fs_t *this, fs_file_t *file, fuse_req_t req, size_t size,
			    off_t off)
{
  off_t  n = cmdsInfoSize();
  char *next, *buf = malloc(n);
  cmd_t *cmd, *tmp;
  
  next=buf;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    next=stpcpy(next, cmd->name);
    *next=',';
    next++;
    next=stpcpy(next, cmd->clttty.link);
    *next=',';
    next++;
    next=stpcpy(next, cmd->cmdline);
    *next='\n';
    next++;
  }

  int rc=fsFuseReplyBufLimited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
  return true;
}

fs_fileops_t fs_lcmds_ops = {
  .stat    = fs_lcmds_stat,
  .open    = NULL,
  .read    = fs_lcmds_read,
  .write   = NULL,
  .readdir = NULL 
};

/*** /bcst ***/
static off_t bcstSize()
{
  off_t n = 0;
  
  if (GBLS.bcsttty.link) n=strlen(GBLS.bcsttty.link)+1;
  return n;
}

static bool
fs_bcst_stat(fs_t *this, fs_file_t *file, struct stat *stbuf)
{
  VLPRINT(2, "%s %ld: ", file->name, file->ino);
  stbuf->st_ino = file->ino;
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = bcstSize();
  VLPRINT(2, "%ld\n", stbuf->st_size);
  return true; 
}

static bool
fs_bcst_read(fs_t *this, fs_file_t *file, fuse_req_t req, size_t size,
			    off_t off)
{
  off_t  n = bcstSize();
  char *next, *buf = malloc(n);

  next=buf;
  next=stpcpy(next, GBLS.bcsttty.link);

  int rc=fsFuseReplyBufLimited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
  return true;
}

fs_fileops_t fs_bcst_ops = {
  .stat    = fs_bcst_stat,
  .open    = NULL,
  .read    = fs_bcst_read,
  .write   = NULL,
  .readdir = NULL 
};


void
yarfsCreate(fs_t *fs, fs_ino_t rootino)
{
  fs_file_t *item;
  VLPRINT(2, "fs=%p rootino=%ld\n", fs, rootino);
  item = fsCreatefile(fs, rootino, "cmds", NULL, &fs_cmds_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "lcmds", NULL, &fs_lcmds_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "bcst", NULL, &fs_bcst_ops);
  assert(item);
}
