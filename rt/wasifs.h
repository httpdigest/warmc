#ifndef WARMC_WASIFS_H
#define WARMC_WASIFS_H

#include "warm_rt.h"

#if !defined(WARMC_DARWIN) && (!defined(WARMC_WINDOWS) || defined(WARMC_JIT))
/* A Linux program from warmc links no libc: rt/linuxposix.c answers these
   over raw system calls, under names of its own, so that a program which
   does link a libc -- an embedder's driver -- keeps that libc whole rather
   than having half of its allocator replaced.  Every declaration and call in
   the WASI hosts goes through these, and so do linuxposix.c's definitions.
   A JIT's runtime on Windows is in a DLL beside the C library, whose
   functions of these names the compiler there calls, and rt/winposix.c's
   go by names of their own as well. */
#if defined(WARMC_WINDOWS)
#define WARMC_POSIX(n) warmc_wp_##n
#else
#define WARMC_POSIX(n) warmc_lx_##n
#endif
#define openat(...)        WARMC_POSIX(openat)(__VA_ARGS__)
#define close(...)         WARMC_POSIX(close)(__VA_ARGS__)
#define dup(...)           WARMC_POSIX(dup)(__VA_ARGS__)
#define pread(...)         WARMC_POSIX(pread)(__VA_ARGS__)
#define pwrite(...)        WARMC_POSIX(pwrite)(__VA_ARGS__)
#define lseek(...)         WARMC_POSIX(lseek)(__VA_ARGS__)
#define fstat(...)         WARMC_POSIX(fstat)(__VA_ARGS__)
#define fstatat(...)       WARMC_POSIX(fstatat)(__VA_ARGS__)
#define mkdirat(...)       WARMC_POSIX(mkdirat)(__VA_ARGS__)
#define unlinkat(...)      WARMC_POSIX(unlinkat)(__VA_ARGS__)
#define renameat(...)      WARMC_POSIX(renameat)(__VA_ARGS__)
#define symlinkat(...)     WARMC_POSIX(symlinkat)(__VA_ARGS__)
#define readlinkat(...)    WARMC_POSIX(readlinkat)(__VA_ARGS__)
#define linkat(...)        WARMC_POSIX(linkat)(__VA_ARGS__)
#define fsync(...)         WARMC_POSIX(fsync)(__VA_ARGS__)
#define fdatasync(...)     WARMC_POSIX(fdatasync)(__VA_ARGS__)
#define ftruncate(...)     WARMC_POSIX(ftruncate)(__VA_ARGS__)
#define fcntl(...)         WARMC_POSIX(fcntl)(__VA_ARGS__)
#define utimensat(...)     WARMC_POSIX(utimensat)(__VA_ARGS__)
#define futimens(...)      WARMC_POSIX(futimens)(__VA_ARGS__)
#define isatty(...)        WARMC_POSIX(isatty)(__VA_ARGS__)
#define opendir(...)       WARMC_POSIX(opendir)(__VA_ARGS__)
#define fdopendir(...)     WARMC_POSIX(fdopendir)(__VA_ARGS__)
#define readdir(...)       WARMC_POSIX(readdir)(__VA_ARGS__)
#define telldir(...)       WARMC_POSIX(telldir)(__VA_ARGS__)
#define seekdir(...)       WARMC_POSIX(seekdir)(__VA_ARGS__)
#define rewinddir(...)     WARMC_POSIX(rewinddir)(__VA_ARGS__)
#define closedir(...)      WARMC_POSIX(closedir)(__VA_ARGS__)
#define clock_gettime(...) WARMC_POSIX(clock_gettime)(__VA_ARGS__)
#define clock_getres(...)  WARMC_POSIX(clock_getres)(__VA_ARGS__)
#define nanosleep(...)     WARMC_POSIX(nanosleep)(__VA_ARGS__)
#define poll(...)          WARMC_POSIX(poll)(__VA_ARGS__)
#define signal(...)        WARMC_POSIX(signal)(__VA_ARGS__)
#define socket(...)        WARMC_POSIX(socket)(__VA_ARGS__)
#define bind(...)          WARMC_POSIX(bind)(__VA_ARGS__)
#define listen(...)        WARMC_POSIX(listen)(__VA_ARGS__)
#define accept(...)        WARMC_POSIX(accept)(__VA_ARGS__)
#define setsockopt(...)    WARMC_POSIX(setsockopt)(__VA_ARGS__)
#define shutdown(...)      WARMC_POSIX(shutdown)(__VA_ARGS__)
#define send(...)          WARMC_POSIX(send)(__VA_ARGS__)
#define recv(...)          WARMC_POSIX(recv)(__VA_ARGS__)
#define malloc(...)        WARMC_POSIX(malloc)(__VA_ARGS__)
#define free(...)          WARMC_POSIX(free)(__VA_ARGS__)
#define realloc(...)       WARMC_POSIX(realloc)(__VA_ARGS__)
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
