#ifndef __FS_H__
#define __FS_H__

extern bool fsRegisterEvents(fs_t *this, int epollfd);
extern bool fsCreate(fs_t *this, char *name);
extern bool fsInit(fs_t *this, bool initmtpt, char *mntptdir, bool iszeroed);
extern bool fsCleanup(fs_t *this);
extern void fsusage(FILE *);

#endif
