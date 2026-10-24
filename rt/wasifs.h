#ifndef WARMC_WASIFS_H
#define WARMC_WASIFS_H

#include "warm_rt.h"

#if !defined(WARMC_DARWIN) && !defined(WARMC_WINDOWS)
/* A Linux program from warmc links no libc: rt/linuxposix.c answers these
   over raw system calls, under names of its own, so that a program which
   does link a libc -- an embedder's driver -- keeps that libc whole rather
   than having half of its allocator replaced.  Every declaration and call in
   the WASI hosts goes through these, and so do linuxposix.c's definitions. */
#define openat(...)        warmc_lx_openat(__VA_ARGS__)
#define close(...)         warmc_lx_close(__VA_ARGS__)
#define dup(...)           warmc_lx_dup(__VA_ARGS__)
#define pread(...)         warmc_lx_pread(__VA_ARGS__)
#define pwrite(...)        warmc_lx_pwrite(__VA_ARGS__)
#define lseek(...)         warmc_lx_lseek(__VA_ARGS__)
#define fstat(...)         warmc_lx_fstat(__VA_ARGS__)
#define fstatat(...)       warmc_lx_fstatat(__VA_ARGS__)
#define mkdirat(...)       warmc_lx_mkdirat(__VA_ARGS__)
#define unlinkat(...)      warmc_lx_unlinkat(__VA_ARGS__)
#define renameat(...)      warmc_lx_renameat(__VA_ARGS__)
#define symlinkat(...)     warmc_lx_symlinkat(__VA_ARGS__)
#define readlinkat(...)    warmc_lx_readlinkat(__VA_ARGS__)
#define linkat(...)        warmc_lx_linkat(__VA_ARGS__)
#define fsync(...)         warmc_lx_fsync(__VA_ARGS__)
#define fdatasync(...)     warmc_lx_fdatasync(__VA_ARGS__)
#define ftruncate(...)     warmc_lx_ftruncate(__VA_ARGS__)
#define fcntl(...)         warmc_lx_fcntl(__VA_ARGS__)
#define utimensat(...)     warmc_lx_utimensat(__VA_ARGS__)
#define futimens(...)      warmc_lx_futimens(__VA_ARGS__)
#define isatty(...)        warmc_lx_isatty(__VA_ARGS__)
#define fdopendir(...)     warmc_lx_fdopendir(__VA_ARGS__)
#define readdir(...)       warmc_lx_readdir(__VA_ARGS__)
#define telldir(...)       warmc_lx_telldir(__VA_ARGS__)
#define seekdir(...)       warmc_lx_seekdir(__VA_ARGS__)
#define rewinddir(...)     warmc_lx_rewinddir(__VA_ARGS__)
#define closedir(...)      warmc_lx_closedir(__VA_ARGS__)
#define clock_gettime(...) warmc_lx_clock_gettime(__VA_ARGS__)
#define clock_getres(...)  warmc_lx_clock_getres(__VA_ARGS__)
#define nanosleep(...)     warmc_lx_nanosleep(__VA_ARGS__)
#define poll(...)          warmc_lx_poll(__VA_ARGS__)
#define signal(...)        warmc_lx_signal(__VA_ARGS__)
#define socket(...)        warmc_lx_socket(__VA_ARGS__)
#define bind(...)          warmc_lx_bind(__VA_ARGS__)
#define listen(...)        warmc_lx_listen(__VA_ARGS__)
#define accept(...)        warmc_lx_accept(__VA_ARGS__)
#define setsockopt(...)    warmc_lx_setsockopt(__VA_ARGS__)
#define shutdown(...)      warmc_lx_shutdown(__VA_ARGS__)
#define send(...)          warmc_lx_send(__VA_ARGS__)
#define recv(...)          warmc_lx_recv(__VA_ARGS__)
#define malloc(...)        warmc_lx_malloc(__VA_ARGS__)
#define free(...)          warmc_lx_free(__VA_ARGS__)
#define realloc(...)       warmc_lx_realloc(__VA_ARGS__)
#endif

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
#elif defined(WARMC_WINDOWS)
extern int *__errno_location(void);
#define h_errno (*__errno_location())
#else
extern int *warmc_lx_errno(void);
#define h_errno (*warmc_lx_errno())
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

/* A guest path, as Linux takes one; how deep the walk goes below a preopen;
   how many links it follows. */
#define PATHMAX  4096
#define MAXDEPTH 1024
#define MAXLINKS 8

/* to open a directory to walk through: for search, which a directory of
   --x lets, where there is such an open; for reading where not */
#if defined(WARMC_DARWIN)
#define H_SEARCH 0x40000000
#elif defined(WARMC_WINDOWS)
#define H_SEARCH 0
#else
#define H_SEARCH 0x200000
#endif

u32 wasifs_errno(int e);
u64 wasifs_st_u64(const u8 *st, int off);
u32 wasifs_st_small(const u8 *st, int off);
u64 wasifs_st_time(const u8 *st, int off);
u8 wasifs_filetype(const u8 *st);
void wasifs_str_copy(char *d, const char *s, usz n);
int wasifs_str_eq(const char *a, const char *b);
u32 wasifs_walk(int root, const char *path, int *out_dir, char *leaf);
/* A path that ends in a slash walks into its last component, so that what
   is done is done to "." in it: as wasmtime has it, that is what opening,
   stating, unlinking or removing such a path means.  Making a directory or
   renaming by one takes the slashes off and acts on the entry itself, which
   must then be a directory; this takes them off, all but a first
   character, and says whether there were any. */
int wasifs_trim_slash(char *path);
void wasifs_walk_done(int root, int dir);
u32 wasifs_walk_leaf(int root, char *path, int *out_dir, char *leaf, int follow);
u32 wasifs_splice_leaf(char *path, const char *link);
u32 wasifs_de_namlen(const u8 *de);
u8 wasifs_de_filetype(u8 dt);

#endif
