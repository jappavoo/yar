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
	  " /pid   : readonly file : contents is pid of yar\n"
	  " /cmds  : readonly file : contents is list of current command names\n"
	  " /lcmds : readonly file : contents is long list of current commands\n"
	  "         (name,tty,yarpid,command line)\n"
	  " /ttys  : readonly file : contents is list of current ttys\n"
	  "          (tty,yarpid,type,v1,v2)\n"
	  "          where type is one of 'cmd'|'bcst'|'mon' and v1 and v2 are\n"
	  "          type dependent values:\n"
	  "              type='cmd'  v1=name v2=cmdline\n"
	  "              type='bcst' v1=num of broadcast clients v2=empty\n"
	  "              type='mon'  v1=empty v2=empty\n"
	  " /bcst  : readonly file : path of broadcast tty if enabled\n");
}

/*** /pid ***/
static off_t pidSize()
{
  char pidstr[24];
  int pidstrlen;
  off_t n = 0;
  pidstrlen = snprintf(pidstr, sizeof(pidstr), "%" PRIdMAX, (intmax_t)GBLS.pid);
  n = pidstrlen;
  return n;
}

static bool
fs_pid_stat(fs_t *this, fs_file_t *file, struct stat *stbuf)
{
  VLPRINT(2, "%s %ld: ", file->name, file->ino);
  stbuf->st_ino = file->ino;
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = pidSize();
  VLPRINT(2, "%ld\n", stbuf->st_size);
  return true; 
}

static bool
fs_pid_read(fs_t *this, fs_file_t *file, fuse_req_t req, size_t size,
			    off_t off)
{
  off_t  n = pidSize();
  char *buf = malloc(n+1);
  snprintf(buf, n+1, "%" PRIdMAX, (intmax_t)GBLS.pid);

  int rc=fsFuseReplyBufLimited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
  return true;
}

fs_fileops_t fs_pid_ops = {
  .stat    = fs_pid_stat,
  .open    = NULL,
  .read    = fs_pid_read,
  .write   = NULL,
  .readdir = NULL 
};

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
  char pidstr[24];
  int pidstrlen;
  off_t n = 0;

  pidstrlen = snprintf(pidstr, sizeof(pidstr), "%" PRIdMAX, (intmax_t)GBLS.pid);
  assert(pidstrlen > 0);
  
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n+=strlen(cmd->name)+1;          // +1 for comma
    n+=strlen(cmd->clttty.link)+1;   // +1 for comma
    n+=(pidstrlen+1);                // +1 for comma
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
  char pidstr[24];
  int pidstrlen;

  pidstrlen = snprintf(pidstr, sizeof(pidstr), "%" PRIdMAX, (intmax_t)GBLS.pid);
  assert(pidstrlen > 0);
  
  next=buf;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    next=stpcpy(next, cmd->name);
    *next=',';
    next++;
    next=stpcpy(next, cmd->clttty.link);
    *next=',';
    next++;
    next=stpcpy(next, pidstr);
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


/*** /ttys ***/
static off_t
ttysInfoSize()
{
  cmd_t *cmd, *tmp;
  char pidstr[24];
  int pidstrlen;
  char cmdcntstr[24];
  int cmdcntstrlen;
  off_t n = 0;

  pidstrlen = snprintf(pidstr, sizeof(pidstr), "%" PRIdMAX, (intmax_t)GBLS.pid);
  assert(pidstrlen > 0);

  cmdcntstrlen = snprintf(cmdcntstr, sizeof(cmdcntstr), "%" PRIdMAX,
		       (intmax_t)HASH_COUNT(GBLS.cmds));
  assert(cmdcntstrlen > 0);
  
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    n+=strlen(cmd->clttty.link)+1;   // +1 for comma
    n+=(pidstrlen+1);                // +1 for comma
    n+=strlen("cmd")+1;              // +1 for comma
    n+=strlen(cmd->name)+1;          // +1 for comma
    n+=strlen(cmd->cmdline)+1;   // +1 for newline
  }
  n+=strlen(GBLS.bcsttty.link)+1;    // +1 for comma
  n+=(pidstrlen+1);                  // +1 for comma
  n+=strlen("bcst")+1;               // +1 for comma
  n+=cmdcntstrlen+2;                // +2 for comma newline
  
  n+=strlen(GBLS.mon.tty.link)+1;    // +1 for comma
  n+=(pidstrlen+1);                  // +1 for comma
  n+=strlen("mon")+3;                // +3 for comma comma newline
  
  return n;
}

static bool
fs_ttys_stat(fs_t *this, fs_file_t *file, struct stat *stbuf)
{
  VLPRINT(2, "%s %ld: ", file->name, file->ino);
  stbuf->st_ino = file->ino;
  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;
  stbuf->st_size = ttysInfoSize();
  VLPRINT(2, "%ld\n", stbuf->st_size);
  return true; 
}

static bool
fs_ttys_read(fs_t *this, fs_file_t *file, fuse_req_t req, size_t size,
			    off_t off)
{
  off_t  n = ttysInfoSize();
  char *next, *buf = malloc(n);
  cmd_t *cmd, *tmp;
  char pidstr[24];
  int pidstrlen;
  char cmdcntstr[24];
  int cmdcntstrlen;

  pidstrlen = snprintf(pidstr, sizeof(pidstr), "%" PRIdMAX, (intmax_t)GBLS.pid);
  assert(pidstrlen > 0);

  cmdcntstrlen = snprintf(cmdcntstr, sizeof(cmdcntstr), "%" PRIdMAX,
			  (intmax_t)HASH_COUNT(GBLS.cmds));
  assert(cmdcntstrlen > 0);

  next=buf;
  HASH_ITER(hh, GBLS.cmds, cmd, tmp) {
    next=stpcpy(next, cmd->clttty.link);
    *next=',';
    next++;
    next=stpcpy(next, pidstr);
    *next=',';
    next++;
    next=stpcpy(next, "cmd");
    *next=',';
    next++;
    next=stpcpy(next, cmd->name);
    *next=',';
    next++;
    next=stpcpy(next, cmd->cmdline);
    *next='\n';
    next++;
  }

  next=stpcpy(next, GBLS.bcsttty.link);
  *next=',';
  next++;
  next=stpcpy(next, pidstr);
  *next=',';
  next++;
  next=stpcpy(next, "bcst");
  *next=',';
  next++;
  next=stpcpy(next, cmdcntstr);
  *next=',';
  next++;
  *next='\n';
  next++;
    
  next=stpcpy(next, GBLS.mon.tty.link);
  *next=',';
  next++;
  next=stpcpy(next, pidstr);
  *next=',';
  next++;
  next=stpcpy(next, "mon");
  *next=',';
  next++;
  *next=',';
  next++;
  *next='\n';

  int rc=fsFuseReplyBufLimited(req, buf, n, off, size);
  if (rc!=0) fprintf(stderr, "fuse_reply_buf failed: %d", rc);
    
  free(buf);
  return true;
}

fs_fileops_t fs_ttys_ops = {
  .stat    = fs_ttys_stat,
  .open    = NULL,
  .read    = fs_ttys_read,
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
  item = fsCreatefile(fs, rootino, "pid", NULL, &fs_pid_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "cmds", NULL, &fs_cmds_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "lcmds", NULL, &fs_lcmds_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "ttys", NULL, &fs_ttys_ops);
  assert(item);
  item = fsCreatefile(fs, rootino, "bcst", NULL, &fs_bcst_ops);
  assert(item);
}
