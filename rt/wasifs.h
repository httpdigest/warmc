#ifndef WARMC_WASIFS_H
#define WARMC_WASIFS_H

#include "warm_rt.h"

extern void *opendir(const char *);
extern void *fdopendir(int);
extern void *readdir(void *);
extern int closedir(void *);
extern void rewinddir(void *);
extern long telldir(void *);
extern void seekdir(void *, long);

extern int openat(int, const char *, int, ...);
extern int close(int);
extern long pread(int, void *, usz, long long);
extern long pwrite(int, const void *, usz, long long);
extern long long lseek(int, long long, int);
extern int fstat(int, void *);
extern int fstatat(int, const char *, void *, int);
extern int mkdirat(int, const char *, unsigned int);
extern int unlinkat(int, const char *, int);
extern int renameat(int, const char *, int, const char *);
extern int symlinkat(const char *, int, const char *);
extern long readlinkat(int, const char *, char *, usz);
extern int linkat(int, const char *, int, const char *, int);
extern int fsync(int);
extern int fcntl(int, int, ...);
extern int ftruncate(int, long long);
extern int futimens(int, const void *);
extern int utimensat(int, const char *, const void *, int);

#if defined(WARMC_DARWIN)
extern int *__error(void);
#define h_errno (*__error())
#else
extern int *__errno_location(void);
#define h_errno (*__errno_location())
#endif

#if defined(WARMC_DARWIN)
#define H_RDONLY      0x0000
#define H_WRONLY      0x0001
#define H_RDWR        0x0002
#define H_NONBLOCK    0x0004
#define H_APPEND      0x0008
#define H_SYNC        0x0080
#define H_NOFOLLOW    0x0100
#define H_CREAT       0x0200
#define H_TRUNC       0x0400
#define H_EXCL        0x0800
#define H_DIRECTORY   0x100000
#define H_AT_NOFOLLOW 0x0020
#define H_AT_REMOVEDIR 0x0080
#define H_AT_SYMLINK_FOLLOW 0x0040
#define H_AT_FDCWD       (-2)
#define H_EAGAIN         35
#define H_ELOOP          62
#define H_ENAMETOOLONG   63
#define H_ENOTEMPTY      66
#else
#define H_RDONLY      0x0000
#define H_WRONLY      0x0001
#define H_RDWR        0x0002
#define H_NONBLOCK    0x0800
#define H_APPEND      0x0400
#define H_SYNC        0x101000
#define H_NOFOLLOW    0x8000
#define H_CREAT       0x0040
#define H_TRUNC       0x0200
#define H_EXCL        0x0080
#define H_DIRECTORY   0x4000
#define H_AT_NOFOLLOW 0x0100
#define H_AT_REMOVEDIR 0x0200
#define H_AT_SYMLINK_FOLLOW 0x0400
#define H_AT_FDCWD       (-100)
#define H_EAGAIN         11
#define H_ENAMETOOLONG   36
#define H_ENOTEMPTY      39
#define H_ELOOP          40
#endif
#define H_ENOTDIR        20

#if defined(WARMC_DARWIN)
#define ST_SIZE   144
#define ST_OFF_MODE   4
#define ST_OFF_NLINK  6
#define ST_OFF_INO    8
#define ST_OFF_ATIME  32
#define ST_OFF_MTIME  48
#define ST_OFF_CTIME  64
#define ST_OFF_FSIZE  96
#define ST_SMALL      2
#else
#define ST_SIZE   128
#define ST_OFF_MODE   16
#define ST_OFF_NLINK  20
#define ST_OFF_INO    8
#define ST_OFF_ATIME  72
#define ST_OFF_MTIME  88
#define ST_OFF_CTIME  104
#define ST_OFF_FSIZE  48
#define ST_SMALL      4
#endif

#define FT_UNKNOWN 0
#define FT_BLOCK   1
#define FT_CHAR    2
#define FT_DIR     3
#define FT_REGULAR 4
#define FT_SYMLINK 7

#if defined(WARMC_DARWIN)
#define DE_INO    0
#define DE_NAMLEN 18
#define DE_TYPE   20
#define DE_NAME   21
#else
#define DE_INO    0
#define DE_TYPE   18
#define DE_NAME   19
#endif

extern int dup(int);

#define PATHMAX  1024
#define MAXDEPTH 64
#define MAXLINKS 8

u32 wasifs_errno(int e);
u64 wasifs_st_u64(const u8 *st, int off);
u32 wasifs_st_small(const u8 *st, int off);
u64 wasifs_st_time(const u8 *st, int off);
u8 wasifs_filetype(const u8 *st);
void wasifs_str_copy(char *d, const char *s, usz n);
int wasifs_str_eq(const char *a, const char *b);
u32 wasifs_walk(int root, const char *path, int *out_dir, char *leaf);
void wasifs_walk_done(int root, int dir);
u32 wasifs_walk_leaf(int root, char *path, int *out_dir, char *leaf, int follow);
u32 wasifs_splice_leaf(char *path, const char *link);
u32 wasifs_de_namlen(const u8 *de);
u8 wasifs_de_filetype(u8 dt);

#endif
