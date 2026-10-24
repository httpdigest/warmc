/* rt/winposix.c -- the POSIX surface rt/wasi.c and rt/wasifs.c are written
   against, implemented over Win32.  On Linux and macOS those two files call
   libc; Windows has no libc in this link, so this file is the other side of
   that whole interface.

   It adopts wasifs.h's non-Darwin arm wholesale, which is the AArch64-Linux
   layout: errno reached through __errno_location(), Linux errno numbers (so
   the shared wasifs_errno() table already knows them), Linux O_* bits and a
   128-byte struct stat.  Nothing outside this file reads those bytes except
   wasifs.c's accessors, so the layout only has to agree with itself.

   Win32 has no openat(), so a descriptor carries its own absolute path and
   every relative open is a join.  rt/wasifs.c walks paths one component at a
   time with O_NOFOLLOW and handles symlinks itself, and that carries over:
   FILE_FLAG_OPEN_REPARSE_POINT opens the link rather than the target, and a
   reparse point opened that way answers ELOOP, which is the errno the walk
   watches for.

   Win32 structures are hand-declared at fixed offsets, the way rt/win.inc
   declares CONTEXT; test/win_fixture.c checks every one of them against the
   real SDK headers at compile time. */

#include "wasifs.h"

/* ---- Win32 ---- */

#define INVALID_HANDLE ((void *)(uptr)-1)

extern u32 GetLastError(void);
extern void SetLastError(u32 e);
extern void *GetStdHandle(u32 which);
extern int CloseHandle(void *h);
extern void *GetCurrentProcess(void);
extern void *GetCurrentThread(void);
extern int DuplicateHandle(void *sp, void *sh, void *tp, void **th, u32 access,
                           int inherit, u32 options);

extern void *CreateFileW(const u16 *name, u32 access, u32 share, void *sa,
                         u32 disp, u32 flags, void *tmpl);
extern int ReadFile(void *h, void *buf, u32 n, u32 *got, void *ov);
extern int WriteFile(void *h, const void *buf, u32 n, u32 *put, void *ov);
extern int SetFilePointerEx(void *h, i64 dist, i64 *newpos, u32 method);
extern int SetEndOfFile(void *h);
extern int FlushFileBuffers(void *h);
extern u32 GetFileType(void *h);
extern int GetConsoleMode(void *h, u32 *mode);
extern int PeekNamedPipe(void *h, void *buf, u32 n, u32 *got, u32 *avail,
                         u32 *left);

extern int GetFileInformationByHandle(void *h, void *info);
extern int GetFileInformationByHandleEx(void *h, int cls, void *info, u32 n);
extern int SetFileTime(void *h, const u64 *cre, const u64 *acc, const u64 *wrt);

extern u32 GetFullPathNameW(const u16 *name, u32 n, u16 *out, u16 **part);
extern u32 GetFileAttributesW(const u16 *name);
extern int CreateDirectoryW(const u16 *name, void *sa);
extern int RemoveDirectoryW(const u16 *name);
extern int DeleteFileW(const u16 *name);
extern int MoveFileExW(const u16 *from, const u16 *to, u32 flags);
extern int CreateHardLinkW(const u16 *link, const u16 *target, void *sa);
/* BOOLEAN, not BOOL: this one returns a single byte, and the rest of the
   register is undefined -- read as an int, a failure tests true. */
extern u8 CreateSymbolicLinkW(const u16 *link, const u16 *target, u32 flags);
extern int DeviceIoControl(void *h, u32 code, void *in, u32 nin, void *out,
                           u32 nout, u32 *ret, void *ov);

extern void GetSystemTimePreciseAsFileTime(u64 *ft);
extern int QueryPerformanceCounter(u64 *c);
extern int QueryPerformanceFrequency(u64 *f);
extern int GetProcessTimes(void *h, u64 *cre, u64 *exi, u64 *ker, u64 *usr);
extern int GetThreadTimes(void *h, u64 *cre, u64 *exi, u64 *ker, u64 *usr);
extern int QueryProcessCycleTime(void *h, u64 *cycles);
extern int QueryThreadCycleTime(void *h, u64 *cycles);
extern void Sleep(u32 ms);
extern u32 WaitForSingleObject(void *h, u32 ms);

extern void *GetProcessHeap(void);
extern void *HeapAlloc(void *heap, u32 flags, usz n);
extern void *HeapReAlloc(void *heap, u32 flags, void *p, usz n);
extern int HeapFree(void *heap, u32 flags, void *p);

#define GENERIC_READ    0x80000000u
#define GENERIC_WRITE   0x40000000u
#define FILE_READ_ATTRS  0x00000080u
#define FILE_WRITE_ATTRS 0x00000100u
#define SHARE_ALL       0x00000007u        /* READ | WRITE | DELETE */

#define CREATE_NEW        1u
#define CREATE_ALWAYS     2u
#define OPEN_EXISTING     3u
#define OPEN_ALWAYS       4u
#define TRUNCATE_EXISTING 5u

#define FLAG_BACKUP_SEMANTICS   0x02000000u
#define FLAG_OPEN_REPARSE_POINT 0x00200000u

#define ATTR_READONLY      0x0001u
#define ATTR_DIRECTORY     0x0010u
#define ATTR_REPARSE_POINT 0x0400u
#define ATTR_INVALID       0xffffffffu

#define FTYPE_DISK 1u
#define FTYPE_CHAR 2u
#define FTYPE_PIPE 3u

#define MOVE_REPLACE_EXISTING 1u
#define DUPLICATE_SAME_ACCESS 2u

#define SYMLINK_FLAG_DIRECTORY   1u
#define SYMLINK_FLAG_UNPRIV      2u

#define FSCTL_GET_REPARSE_POINT 0x000900a8u
#define TAG_MOUNT_POINT 0xa0000003u
#define TAG_SYMLINK     0xa000000cu

/* GetFileInformationByHandleEx classes */
#define CLS_ATTRIBUTE_TAG_INFO      9
#define CLS_ID_BOTH_DIR_INFO        10
#define CLS_ID_BOTH_DIR_RESTART     11

/* ---- errno ---- */

#define E_PERM 1
#define E_NOENT 2
#define E_INTR 4
#define E_IO 5
#define E_BADF 9
#define E_AGAIN 11
#define E_NOMEM 12
#define E_ACCES 13
#define E_FAULT 14
#define E_BUSY 16
#define E_EXIST 17
#define E_XDEV 18
#define E_NODEV 19
#define E_NOTDIR 20
#define E_ISDIR 21
#define E_INVAL 22
#define E_MFILE 24
#define E_NOTTY 25
#define E_NOSPC 28
#define E_SPIPE 29
#define E_ROFS 30
#define E_PIPE 32
#define E_RANGE 34
#define E_NAMETOOLONG 36
#define E_NOSYS 38
#define E_NOTEMPTY 39
#define E_LOOP 40

static int g_errno;

int *__errno_location(void) { return &g_errno; }

static int fail(int e) { g_errno = e; return -1; }

static int win_errno(u32 e) {
  switch (e) {
    case 0:    return 0;
    case 2:    return E_NOENT;          /* FILE_NOT_FOUND */
    case 3:    return E_NOENT;          /* PATH_NOT_FOUND */
    case 4:    return E_MFILE;          /* TOO_MANY_OPEN_FILES */
    case 5:    return E_ACCES;          /* ACCESS_DENIED */
    case 6:    return E_BADF;           /* INVALID_HANDLE */
    case 8:    return E_NOMEM;          /* NOT_ENOUGH_MEMORY */
    case 15:   return E_NODEV;          /* INVALID_DRIVE */
    case 17:   return E_XDEV;           /* NOT_SAME_DEVICE */
    case 18:   return E_NOENT;          /* NO_MORE_FILES */
    case 19:   return E_ROFS;           /* WRITE_PROTECT */
    case 21:   return E_BUSY;           /* NOT_READY */
    case 32:   return E_ACCES;          /* SHARING_VIOLATION */
    case 33:   return E_ACCES;          /* LOCK_VIOLATION */
    case 50:   return E_NOSYS;          /* NOT_SUPPORTED */
    case 55:   return E_NODEV;          /* DEV_NOT_EXIST */
    case 80:   return E_EXIST;          /* FILE_EXISTS */
    case 87:   return E_INVAL;          /* INVALID_PARAMETER */
    case 109:  return E_PIPE;           /* BROKEN_PIPE */
    case 112:  return E_NOSPC;          /* DISK_FULL */
    case 122:  return E_RANGE;          /* INSUFFICIENT_BUFFER */
    case 123:  return E_INVAL;          /* INVALID_NAME */
    case 145:  return E_NOTEMPTY;       /* DIR_NOT_EMPTY */
    case 161:  return E_NOENT;          /* BAD_PATHNAME */
    case 183:  return E_EXIST;          /* ALREADY_EXISTS */
    case 206:  return E_NAMETOOLONG;    /* FILENAME_EXCED_RANGE */
    case 232:  return E_PIPE;           /* NO_DATA (pipe with no reader) */
    case 267:  return E_NOTDIR;         /* DIRECTORY */
    case 998:  return E_FAULT;          /* NOACCESS */
    case 1314: return E_PERM;           /* PRIVILEGE_NOT_HELD */
    case 4390: return E_INVAL;          /* NOT_A_REPARSE_POINT */
    default:   return E_IO;
  }
}

static int fail_last(void) { return fail(win_errno(GetLastError())); }

/* ---- allocation ---- */

void *malloc(usz n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }

void free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

void *realloc(void *p, usz n) {
  if (!p) return malloc(n);
  return HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1);
}

/* ---- UTF-8 <-> UTF-16 ---- */

#define WPATH 4096

/* Decode one UTF-8 scalar; *p advances past it.  Malformed bytes become
   U+FFFD one byte at a time, so no input can run the reader off the end. */
static u32 utf8_next(const char **p) {
  const u8 *s = (const u8 *)*p;
  u32 c = *s++;
  int extra = c < 0x80 ? 0 : c < 0xc0 ? -1 : c < 0xe0 ? 1 : c < 0xf0 ? 2
            : c < 0xf8 ? 3 : -1;
  if (extra < 0) { *p = (const char *)s; return 0xfffd; }
  c &= (u32)(0x7f >> extra);
  for (int i = 0; i < extra; i++) {
    if ((*s & 0xc0) != 0x80) { *p = (const char *)s; return 0xfffd; }
    c = (c << 6) | (*s++ & 0x3f);
  }
  *p = (const char *)s;
  return c > 0x10ffff ? 0xfffd : c;
}

static void wput(u16 *w, usz *o, usz cap, u32 c) {
  if (c < 0x10000) {
    if (*o < cap) w[(*o)++] = (u16)c;
  } else {
    c -= 0x10000;
    if (*o + 1 < cap) {
      w[(*o)++] = (u16)(0xd800 + (c >> 10));
      w[(*o)++] = (u16)(0xdc00 + (c & 0x3ff));
    }
  }
}

/* UTF-8 to UTF-16, turning '/' into '\' on the way: every path this file
   hands Win32 is built here, and Win32 accepts either separator but the
   reparse buffers it writes back use backslashes only. */
static int wide_of(const char *s, u16 *w, usz cap) {
  usz o = 0;
  while (*s) {
    u32 c = utf8_next(&s);
    wput(w, &o, cap - 1, c == '/' ? '\\' : c);
  }
  if (o >= cap - 1) return fail(E_NAMETOOLONG);
  w[o] = 0;
  return 0;
}

/* UTF-16 to UTF-8, undoing the separator swap.  Returns the byte count, or
   -1 if it will not fit. */
static long utf8_of(const u16 *w, usz wn, char *d, usz cap) {
  usz o = 0;
  for (usz i = 0; i < wn; i++) {
    u32 c = w[i];
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < wn && w[i + 1] >= 0xdc00 &&
        w[i + 1] < 0xe000) {
      c = 0x10000 + ((c - 0xd800) << 10) + (w[i + 1] - 0xdc00);
      i++;
    } else if (c >= 0xd800 && c < 0xe000) {
      c = 0xfffd;
    }
    if (c == '\\') c = '/';
    u8 b[4];
    usz n;
    if (c < 0x80) { b[0] = (u8)c; n = 1; }
    else if (c < 0x800) { b[0] = (u8)(0xc0 | (c >> 6));
                          b[1] = (u8)(0x80 | (c & 63)); n = 2; }
    else if (c < 0x10000) { b[0] = (u8)(0xe0 | (c >> 12));
                            b[1] = (u8)(0x80 | ((c >> 6) & 63));
                            b[2] = (u8)(0x80 | (c & 63)); n = 3; }
    else { b[0] = (u8)(0xf0 | (c >> 18));
           b[1] = (u8)(0x80 | ((c >> 12) & 63));
           b[2] = (u8)(0x80 | ((c >> 6) & 63));
           b[3] = (u8)(0x80 | (c & 63)); n = 4; }
    if (o + n > cap) return -1;
    for (usz k = 0; k < n; k++) d[o++] = (char)b[k];
  }
  return (long)o;
}

static usz wlen(const u16 *w) {
  usz n = 0;
  while (w[n]) n++;
  return n;
}

static u16 *wdup(const u16 *w) {
  usz n = wlen(w);
  u16 *p = (u16 *)malloc((n + 1) * sizeof *p);
  if (!p) { g_errno = E_NOMEM; return 0; }
  for (usz i = 0; i <= n; i++) p[i] = w[i];
  return p;
}

/* ---- descriptor table ---- */

typedef struct {
  void *h;
  u16 *wpath;          /* absolute, for the joins that stand in for openat */
  u8 used, std, kind;  /* kind: FTYPE_* */
  u8 append, nonblock;
} WFd;

static WFd *g_fd;
static int g_nfd;
static int g_fd_ready;

static int fd_grow(int want) {
  if (want < g_nfd) return 1;
  int n = g_nfd ? g_nfd : 16;
  while (n <= want) n *= 2;
  WFd *p = (WFd *)realloc(g_fd, (usz)n * sizeof *p);
  if (!p) return 0;
  for (int i = g_nfd; i < n; i++) __builtin_memset(&p[i], 0, sizeof p[i]);
  g_fd = p;
  g_nfd = n;
  return 1;
}

static u8 handle_kind(void *h) {
  u32 t = GetFileType(h);
  return t == FTYPE_CHAR ? FTYPE_CHAR : t == FTYPE_PIPE ? FTYPE_PIPE : FTYPE_DISK;
}

static void fds_setup(void) {
  if (g_fd_ready) return;
  g_fd_ready = 1;
  if (!fd_grow(2)) return;
  for (int i = 0; i < 3; i++) {
    void *h = GetStdHandle((u32)(-10 - i));
    if (h == INVALID_HANDLE) h = 0;
    g_fd[i].h = h;
    g_fd[i].used = 1;
    g_fd[i].std = 1;
    g_fd[i].kind = h ? handle_kind(h) : FTYPE_CHAR;
  }
}

static WFd *fd_at(int fd) {
  fds_setup();
  if (fd < 0 || fd >= g_nfd || !g_fd[fd].used) { g_errno = E_BADF; return 0; }
  return &g_fd[fd];
}

/* Takes ownership of h and of a copy of wpath. */
static int fd_new(void *h, const u16 *wpath) {
  fds_setup();
  int slot = -1;
  for (int i = 0; i < g_nfd; i++) if (!g_fd[i].used) { slot = i; break; }
  if (slot < 0) {
    slot = g_nfd;
    if (!fd_grow(slot)) { CloseHandle(h); return fail(E_NOMEM); }
  }
  u16 *copy = 0;
  if (wpath && !(copy = wdup(wpath))) { CloseHandle(h); return -1; }
  __builtin_memset(&g_fd[slot], 0, sizeof g_fd[slot]);
  g_fd[slot].h = h;
  g_fd[slot].wpath = copy;
  g_fd[slot].used = 1;
  g_fd[slot].kind = handle_kind(h);
  return slot;
}

int close(int fd) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  if (!f->std && f->h) CloseHandle(f->h);
  free(f->wpath);
  __builtin_memset(f, 0, sizeof *f);
  return 0;
}

int dup(int fd) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  void *nh = 0;
  if (!DuplicateHandle(GetCurrentProcess(), f->h, GetCurrentProcess(), &nh, 0,
                       0, DUPLICATE_SAME_ACCESS))
    return fail_last();
  /* fd_new may grow the table, which moves it, so f is read out first. */
  u8 append = f->append, nonblock = f->nonblock;
  int nfd = fd_new(nh, f->wpath);
  if (nfd >= 0) {
    g_fd[nfd].append = append;
    g_fd[nfd].nonblock = nonblock;
  }
  return nfd;
}

/* ---- paths ---- */

/* The guest's path grammar has one separator, '/', and rt/wasifs.c has already
   split on it -- so a leaf is one component, and a backslash inside it is an
   ordinary character that happens to be there.  Win32 disagrees: it would read
   `..\..\x` as three components and walk straight out of the preopen.  A
   leaf carrying one is therefore simply not there, which is also what a POSIX
   host answers, since no such file exists.

   The reserved device names go the same way.  Win32 resolves CON, NUL, COM3
   and their kin to a device from any directory whatever, and no file can be
   named one -- so ENOENT is both the safe answer and the true one. */
static int leaf_reachable(const char *leaf) {
  usz n = 0;
  for (const char *p = leaf; *p; p++, n++)
    if (*p == '\\') return 0;

  /* The device name is the stem, whatever follows it: `con.txt` is CON. */
  usz stem = 0;
  while (stem < n && leaf[stem] != '.' && leaf[stem] != ':') stem++;
  if (stem < 3 || stem > 4) return 1;
  char up[4];
  for (usz i = 0; i < stem; i++) {
    char c = leaf[i];
    up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  if (stem == 3) {
    static const char dev[4][4] = { "CON", "PRN", "AUX", "NUL" };
    for (int i = 0; i < 4; i++)
      if (up[0] == dev[i][0] && up[1] == dev[i][1] && up[2] == dev[i][2])
        return 0;
    return 1;
  }
  return !(up[3] >= '1' && up[3] <= '9' &&
           ((up[0] == 'C' && up[1] == 'O' && up[2] == 'M') ||
            (up[0] == 'L' && up[1] == 'P' && up[2] == 'T')));
}

/* dir's path with leaf appended.  A "." leaf is dropped rather than appended,
   so a descriptor's stored path stays the shortest spelling of itself. */
static int wjoin(const u16 *dir, const char *leaf, u16 *out) {
  usz n = wlen(dir);
  if (!leaf_reachable(leaf)) return fail(E_NOENT);
  if (n + 2 >= WPATH) return fail(E_NAMETOOLONG);
  for (usz i = 0; i < n; i++) out[i] = dir[i];
  if (leaf[0] == '.' && !leaf[1]) { out[n] = 0; return 0; }
  if (n && out[n - 1] != '\\' && out[n - 1] != '/') out[n++] = '\\';
  out[n] = 0;
  u16 tail[WPATH];
  if (wide_of(leaf, tail, WPATH)) return -1;
  usz m = wlen(tail);
  if (n + m >= WPATH) return fail(E_NAMETOOLONG);
  for (usz i = 0; i <= m; i++) out[n + i] = tail[i];
  return 0;
}

/* The absolute Win32 path a (dirfd, name) pair names. */
static int resolve(int dirfd, const char *name, u16 *out) {
  if (dirfd == H_AT_FDCWD) {
    u16 rel[WPATH];
    if (wide_of(name, rel, WPATH)) return -1;
    u32 n = GetFullPathNameW(rel, WPATH, out, 0);
    if (!n) return fail_last();
    if (n >= WPATH) return fail(E_NAMETOOLONG);
    return 0;
  }
  WFd *d = fd_at(dirfd);
  if (!d) return -1;
  if (!d->wpath) return fail(E_NOTDIR);
  return wjoin(d->wpath, name, out);
}

/* ---- file information ---- */

/* FILE_ATTRIBUTE_TAG_INFO */
typedef struct { u32 attr, tag; } TagInfo;

/* BY_HANDLE_FILE_INFORMATION */
typedef struct {
  u32 attr;
  u64 ctime, atime, wtime;      /* FILETIME pairs, 4-aligned in the SDK */
  u32 volser, size_hi, size_lo, nlinks, idx_hi, idx_lo;
} __attribute__((packed, aligned(4))) HandleInfo;

/* FILE_ID_BOTH_DIR_INFO */
typedef struct {
  u32 next, index;
  u64 ctime, atime, wtime, chtime;
  i64 eof, alloc;
  u32 attr, namelen, easize;
  u8 shortlen;
  u16 shortname[12];
  u64 fileid;
  u16 name[1];
} __attribute__((aligned(8))) DirInfo;

static int tag_of(void *h, u32 *attr, u32 *tag) {
  TagInfo ti;
  if (GetFileInformationByHandleEx(h, CLS_ATTRIBUTE_TAG_INFO, &ti, sizeof ti)) {
    *attr = ti.attr;
    *tag = ti.tag;
    return 1;
  }
  HandleInfo hi;
  if (GetFileInformationByHandle(h, &hi)) {
    *attr = hi.attr;
    *tag = 0;
    return 1;
  }
  *attr = 0;
  *tag = 0;
  return 0;
}

static int is_link_tag(u32 tag) {
  return tag == TAG_SYMLINK || tag == TAG_MOUNT_POINT;
}

/* FILETIME (100ns since 1601) to a POSIX second/nanosecond pair. */
static void ft_split(u64 ft, u64 *sec, u64 *nsec) {
  if (ft < 116444736000000000ull) { *sec = 0; *nsec = 0; return; }
  u64 t = ft - 116444736000000000ull;
  *sec = t / 10000000ull;
  *nsec = (t % 10000000ull) * 100ull;
}

static u64 ft_now(void) {
  u64 ft;
  GetSystemTimePreciseAsFileTime(&ft);
  return ft;
}

static void st_put64(u8 *st, int off, u64 v) { __builtin_memcpy(st + off, &v, 8); }
static void st_put32(u8 *st, int off, u32 v) { __builtin_memcpy(st + off, &v, 4); }

static void st_time(u8 *st, int off, u64 ft) {
  u64 sec, nsec;
  ft_split(ft, &sec, &nsec);
  st_put64(st, off, sec);
  st_put64(st, off + 8, nsec);
}

static int stat_of_handle(void *h, u8 *st) {
  HandleInfo hi;
  if (!GetFileInformationByHandle(h, &hi)) {
    /* Pipes and consoles have no file information; report a character
       device with no size, which is what fstat on one gives on Linux. */
    u32 e = GetLastError();
    u8 kind = handle_kind(h);
    if (kind == FTYPE_DISK) return fail(win_errno(e));
    for (int i = 0; i < ST_SIZE; i++) st[i] = 0;
    st_put32(st, ST_OFF_MODE, kind == FTYPE_CHAR ? 0020666u : 0010666u);
    st_put32(st, ST_OFF_NLINK, 1);
    return 0;
  }
  u32 attr, tag;
  tag_of(h, &attr, &tag);
  if (!attr) attr = hi.attr;

  for (int i = 0; i < ST_SIZE; i++) st[i] = 0;
  u32 mode = (attr & ATTR_READONLY) ? 0444u : 0644u;
  if (is_link_tag(tag) && (attr & ATTR_REPARSE_POINT)) mode |= 0120000u;
  else if (attr & ATTR_DIRECTORY) mode = 0040000u | (mode | 0111u);
  else mode |= 0100000u;

  st_put64(st, ST_OFF_INO, ((u64)hi.idx_hi << 32) | hi.idx_lo);
  st_put32(st, ST_OFF_MODE, mode);
  st_put32(st, ST_OFF_NLINK, hi.nlinks ? hi.nlinks : 1);
  st_put64(st, ST_OFF_FSIZE, ((u64)hi.size_hi << 32) | hi.size_lo);
  st_time(st, ST_OFF_ATIME, hi.atime);
  st_time(st, ST_OFF_MTIME, hi.wtime);
  st_time(st, ST_OFF_CTIME, hi.wtime);
  return 0;
}

/* ---- open ---- */

/* link_is_target: opening a reparse point is the point of the call (readlink,
   lstat), so it does not answer ELOOP the way an O_NOFOLLOW open does. */
static void *win_open_handle(const u16 *wp, int flags, int link_is_target) {
  u32 access;
  switch (flags & 3) {
    case H_WRONLY: access = GENERIC_WRITE; break;
    case H_RDWR:   access = GENERIC_READ | GENERIC_WRITE; break;
    default:       access = GENERIC_READ; break;
  }
  if (link_is_target) access = FILE_READ_ATTRS;

  u32 disp;
  if (flags & H_CREAT)
    disp = (flags & H_EXCL) ? CREATE_NEW
         : (flags & H_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS;
  else
    disp = (flags & H_TRUNC) ? TRUNCATE_EXISTING : OPEN_EXISTING;

  u32 extra = FLAG_BACKUP_SEMANTICS;
  if ((flags & H_NOFOLLOW) || link_is_target) extra |= FLAG_OPEN_REPARSE_POINT;

  void *h = CreateFileW(wp, access, SHARE_ALL, 0, disp, extra, 0);
  if (h != INVALID_HANDLE) return h;

  u32 e = GetLastError();
  /* Win32 answers ACCESS_DENIED both for a directory opened for writing and
     for a create whose name is taken by a directory; POSIX distinguishes. */
  if (e == 5 && (access & GENERIC_WRITE)) {
    u32 attr = GetFileAttributesW(wp);
    if (attr != ATTR_INVALID && (attr & ATTR_DIRECTORY)) {
      g_errno = E_ISDIR;
      return INVALID_HANDLE;
    }
  }
  g_errno = win_errno(e);
  return INVALID_HANDLE;
}

static int win_open(const u16 *wp, int flags) {
  void *h = win_open_handle(wp, flags, 0);
  if (h == INVALID_HANDLE) return -1;

  u32 attr, tag;
  tag_of(h, &attr, &tag);
  if ((flags & H_NOFOLLOW) && (attr & ATTR_REPARSE_POINT) && is_link_tag(tag)) {
    CloseHandle(h);
    return fail(E_LOOP);
  }
  if ((flags & H_DIRECTORY) && !(attr & ATTR_DIRECTORY)) {
    CloseHandle(h);
    return fail(E_NOTDIR);
  }
  int fd = fd_new(h, wp);
  if (fd >= 0 && (flags & H_APPEND)) g_fd[fd].append = 1;
  if (fd >= 0 && (flags & H_NONBLOCK)) g_fd[fd].nonblock = 1;
  return fd;
}

int openat(int dirfd, const char *name, int flags, ...) {
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  return win_open(wp, flags);
}

/* ---- read, write and position ---- */

#define IO_CHUNK (1u << 24)

/* These two answer -errno, not -1, because that is what rt/wasi.c's iovec
   walk expects of warmc_rt_read/write -- on Linux those are raw syscalls. */
long warmc_win_read(int fd, void *buf, usz n) {
  WFd *f = fd_at(fd);
  if (!f) return -E_BADF;
  u32 want = n > IO_CHUNK ? IO_CHUNK : (u32)n, got = 0;
  if (!ReadFile(f->h, buf, want, &got, 0)) {
    u32 e = GetLastError();
    if (e == 109) return 0;             /* a pipe whose writer is gone is EOF */
    return -win_errno(e);
  }
  return (long)got;
}

long warmc_win_write(int fd, const void *buf, usz n) {
  WFd *f = fd_at(fd);
  if (!f) return -E_BADF;
  u32 want = n > IO_CHUNK ? IO_CHUNK : (u32)n, put = 0;
  if (!WriteFile(f->h, buf, want, &put, 0)) return -win_errno(GetLastError());
  return (long)put;
}

i64 lseek(int fd, i64 off, int whence) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  i64 pos = 0;
  /* SEEK_SET/CUR/END are 0/1/2, and so are FILE_BEGIN/CURRENT/END. */
  if (whence < 0 || whence > 2) return fail(E_INVAL);
  if (!SetFilePointerEx(f->h, off, &pos, (u32)whence)) {
    u32 e = GetLastError();
    return fail(e == 1 || e == 6 ? E_SPIPE : win_errno(e));
  }
  return pos;
}

/* Positioned I/O leaves the descriptor's own offset alone, so it is bracketed
   by a save and restore rather than handed an OVERLAPPED -- on a synchronous
   handle Win32 moves the file pointer either way. */
static long pio(int fd, void *buf, usz n, i64 off, int writing) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  i64 save = 0;
  if (!SetFilePointerEx(f->h, 0, &save, 1)) return fail(E_SPIPE);
  if (!SetFilePointerEx(f->h, off, 0, 0)) return fail_last();
  u32 want = n > IO_CHUNK ? IO_CHUNK : (u32)n, moved = 0;
  int ok = writing ? WriteFile(f->h, buf, want, &moved, 0)
                   : ReadFile(f->h, buf, want, &moved, 0);
  u32 e = ok ? 0 : GetLastError();
  SetFilePointerEx(f->h, save, 0, 0);
  if (!ok) return fail(win_errno(e));
  return (long)moved;
}

long pread(int fd, void *buf, usz n, i64 off) { return pio(fd, buf, n, off, 0); }

long pwrite(int fd, const void *buf, usz n, i64 off) {
  return pio(fd, (void *)buf, n, off, 1);
}

int ftruncate(int fd, i64 size) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  i64 save = 0;
  if (!SetFilePointerEx(f->h, 0, &save, 1)) return fail(E_SPIPE);
  int ok = SetFilePointerEx(f->h, size, 0, 0) && SetEndOfFile(f->h);
  u32 e = ok ? 0 : GetLastError();
  SetFilePointerEx(f->h, save, 0, 0);
  return ok ? 0 : fail(win_errno(e));
}

static int flush_fd(int fd) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  if (FlushFileBuffers(f->h)) return 0;
  u32 e = GetLastError();
  /* Nothing buffered to flush on a console or a pipe is not an error. */
  return (e == 1 || e == 6) ? 0 : fail(win_errno(e));
}

int fsync(int fd) { return flush_fd(fd); }
int fdatasync(int fd) { return flush_fd(fd); }

int fcntl(int fd, int cmd, ...) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  __builtin_va_list ap;
  __builtin_va_start(ap, cmd);
  int arg = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  if (cmd == 3) {                       /* F_GETFL */
    int fl = 0;
    if (f->append) fl |= H_APPEND;
    if (f->nonblock) fl |= H_NONBLOCK;
    return fl;
  }
  if (cmd == 4) {                       /* F_SETFL */
    f->append = (arg & H_APPEND) != 0;
    f->nonblock = (arg & H_NONBLOCK) != 0;
    return 0;
  }
  return fail(E_INVAL);
}

int isatty(int fd) {
  WFd *f = fd_at(fd);
  if (!f) { g_errno = E_BADF; return 0; }
  u32 mode;
  if (f->kind != FTYPE_CHAR || !GetConsoleMode(f->h, &mode)) {
    g_errno = E_NOTTY;
    return 0;
  }
  return 1;
}

/* ---- stat ---- */

int fstat(int fd, void *st) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  return stat_of_handle(f->h, (u8 *)st);
}

int fstatat(int dirfd, const char *name, void *st, int flags) {
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  int nofollow = (flags & H_AT_NOFOLLOW) != 0;
  void *h = win_open_handle(wp, H_RDONLY, nofollow);
  if (h == INVALID_HANDLE) return -1;
  int rc = stat_of_handle(h, (u8 *)st);
  CloseHandle(h);
  return rc;
}

/* ---- directory entries ---- */

int mkdirat(int dirfd, const char *name, unsigned int mode) {
  (void)mode;
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  return CreateDirectoryW(wp, 0) ? 0 : fail_last();
}

int unlinkat(int dirfd, const char *name, int flags) {
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  u32 attr = GetFileAttributesW(wp);
  if (attr == ATTR_INVALID) return fail_last();
  int isdir = (attr & ATTR_DIRECTORY) != 0;
  int islink = (attr & ATTR_REPARSE_POINT) != 0;

  if (flags & H_AT_REMOVEDIR) {
    if (!isdir) return fail(E_NOTDIR);
    return RemoveDirectoryW(wp) ? 0 : fail_last();
  }
  /* A symlink to a directory is itself a directory entry on Win32, so it is
     unlinked with RemoveDirectoryW even though POSIX calls it a file. */
  if (isdir && islink) return RemoveDirectoryW(wp) ? 0 : fail_last();
  if (isdir) return fail(E_ISDIR);
  if (attr & ATTR_READONLY) return fail(E_ACCES);
  return DeleteFileW(wp) ? 0 : fail_last();
}

int renameat(int fromfd, const char *from, int tofd, const char *to) {
  u16 wf[WPATH], wt[WPATH];
  if (resolve(fromfd, from, wf) || resolve(tofd, to, wt)) return -1;
  if (MoveFileExW(wf, wt, MOVE_REPLACE_EXISTING)) return 0;
  u32 e = GetLastError();
  /* MOVEFILE_REPLACE_EXISTING will not replace a directory, but POSIX
     rename does when the target is an empty one. */
  if (e == 5 || e == 183) {
    u32 attr = GetFileAttributesW(wt);
    if (attr != ATTR_INVALID && (attr & ATTR_DIRECTORY) &&
        RemoveDirectoryW(wt) && MoveFileExW(wf, wt, MOVE_REPLACE_EXISTING))
      return 0;
  }
  return fail(win_errno(e));
}

int linkat(int fromfd, const char *from, int tofd, const char *to, int flags) {
  (void)flags;
  u16 wf[WPATH], wt[WPATH];
  if (resolve(fromfd, from, wf) || resolve(tofd, to, wt)) return -1;
  return CreateHardLinkW(wt, wf, 0) ? 0 : fail_last();
}

int symlinkat(const char *target, int dirfd, const char *name) {
  u16 wp[WPATH], wt[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  if (wide_of(target, wt, WPATH)) return -1;

  /* Win32 needs to be told at creation time whether the target is a
     directory, so a relative target is resolved against the link's own
     directory first.  A dangling target is assumed to be a file, which is
     what a guest that creates one and never follows it cannot tell apart. */
  u32 flags = SYMLINK_FLAG_UNPRIV;
  u16 probe[WPATH];
  usz cut = 0, n = wlen(wp);
  for (usz i = 0; i < n; i++) if (wp[i] == '\\') cut = i + 1;
  usz tn = wlen(wt);
  if (tn && (wt[1] == ':' || wt[0] == '\\')) {
    for (usz i = 0; i <= tn; i++) probe[i] = wt[i];
  } else if (cut + tn < WPATH) {
    for (usz i = 0; i < cut; i++) probe[i] = wp[i];
    for (usz i = 0; i <= tn; i++) probe[cut + i] = wt[i];
  } else {
    probe[0] = 0;
  }
  u32 attr = probe[0] ? GetFileAttributesW(probe) : ATTR_INVALID;
  if (attr != ATTR_INVALID && (attr & ATTR_DIRECTORY))
    flags |= SYMLINK_FLAG_DIRECTORY;

  if (CreateSymbolicLinkW(wp, wt, flags)) return 0;
  u32 e = GetLastError();
  /* Before Windows 10 1703 the unprivileged-create bit is not a valid flag. */
  if (e == 87 && CreateSymbolicLinkW(wp, wt, flags & ~SYMLINK_FLAG_UNPRIV))
    return 0;
  return fail(win_errno(e));
}

/* REPARSE_DATA_BUFFER, symlink and mount-point shapes.  The two differ by
   the 4-byte Flags field the symlink form carries. */
typedef struct {
  u32 tag;
  u16 datalen, reserved;
  u16 subst_off, subst_len, print_off, print_len;
  u32 flags;
  u16 path[1];
} ReparseLink;

long readlinkat(int dirfd, const char *name, char *buf, usz cap) {
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  void *h = win_open_handle(wp, H_RDONLY, 1);
  if (h == INVALID_HANDLE) return -1;

  u8 raw[16384];
  u32 got = 0;
  int ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, 0, 0, raw, sizeof raw,
                           &got, 0);
  u32 e = ok ? 0 : GetLastError();
  CloseHandle(h);
  if (!ok) return fail(win_errno(e));
  if (got < 16) return fail(E_INVAL);

  ReparseLink *r = (ReparseLink *)raw;
  const u16 *path;
  u16 off, len;
  if (r->tag == TAG_SYMLINK) {
    path = r->path;
    off = r->print_len ? r->print_off : r->subst_off;
    len = r->print_len ? r->print_len : r->subst_len;
  } else if (r->tag == TAG_MOUNT_POINT) {
    path = (const u16 *)(raw + 16);      /* no Flags field in this shape */
    off = r->print_len ? r->print_off : r->subst_off;
    len = r->print_len ? r->print_len : r->subst_len;
  } else {
    return fail(E_INVAL);
  }
  /* the name the buffer says it holds must lie in what came back of it */
  usz base = (usz)((const u8 *)path - raw);
  if (base > got || (usz)off + len > got - base) return fail(E_INVAL);
  const u16 *s = path + off / 2;
  usz n = len / 2;
  /* A substitute name carries the \??\ device prefix; the guest wants the
     path a lookup would take. */
  if (n > 4 && s[0] == '\\' && s[1] == '?' && s[2] == '?' && s[3] == '\\') {
    s += 4;
    n -= 4;
  }
  /* rt/wasifs.c refuses to follow a link whose text is absolute, and it asks
     the POSIX question: does it start with '/'?  A drive-qualified Win32
     target is just as absolute and answers no, so it is given the leading
     slash that makes the sandbox check see it -- a junction, which always
     stores an absolute target, would otherwise be walked component by
     component with a "C:" among them.  (A rooted or UNC target already
     begins with a separator, which utf8_of turns into '/'.) */
  usz at = 0;
  if (n >= 2 && s[1] == ':' &&
      ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) {
    if (!cap) return fail(E_RANGE);
    buf[at++] = '/';
  }
  long w = utf8_of(s, n, buf + at, cap - at);
  if (w < 0) return fail(E_RANGE);
  return w + (long)at;
}

/* ---- directory streams ---- */

typedef struct {
  u64 ino;
  u8 type;
  char *name;
} DEnt;

typedef struct {
  int fd;
  DEnt *ents;
  u32 n, cap, at;
  u8 de[DE_NAME + 1024];
} WDir;

static int dir_push(WDir *d, const DirInfo *e) {
  if (d->n == d->cap) {
    u32 cap = d->cap ? d->cap * 2 : 32;
    DEnt *p = (DEnt *)realloc(d->ents, (usz)cap * sizeof *p);
    if (!p) return fail(E_NOMEM);
    d->ents = p;
    d->cap = cap;
  }
  usz wn = e->namelen / 2;
  char utf8[1024];
  long n = utf8_of(e->name, wn, utf8, sizeof utf8 - 1);
  if (n < 0) return 0;                  /* a name that long cannot be named */
  utf8[n] = 0;
  char *copy = (char *)malloc((usz)n + 1);
  if (!copy) return fail(E_NOMEM);
  for (long i = 0; i <= n; i++) copy[i] = utf8[i];

  d->ents[d->n].ino = e->fileid;
  d->ents[d->n].name = copy;
  d->ents[d->n].type = (e->attr & ATTR_REPARSE_POINT) ? 10
                     : (e->attr & ATTR_DIRECTORY) ? 4 : 8;
  d->n++;
  return 0;
}

/* The whole directory is read up front.  telldir/seekdir then index a list
   that cannot shift underneath a partial fd_readdir, which is what preview1's
   cookie protocol assumes. */
void *fdopendir(int fd) {
  WFd *f = fd_at(fd);
  if (!f) return 0;
  WDir *d = (WDir *)malloc(sizeof *d);
  if (!d) { g_errno = E_NOMEM; return 0; }
  __builtin_memset(d, 0, sizeof *d);
  d->fd = fd;

  static u8 buf[65536];
  int cls = CLS_ID_BOTH_DIR_RESTART;
  for (;;) {
    if (!GetFileInformationByHandleEx(f->h, cls, buf, sizeof buf)) {
      u32 e = GetLastError();
      if (e == 18) break;               /* NO_MORE_FILES */
      for (u32 i = 0; i < d->n; i++) free(d->ents[i].name);
      free(d->ents);
      free(d);
      g_errno = win_errno(e);
      return 0;
    }
    cls = CLS_ID_BOTH_DIR_INFO;
    u8 *p = buf;
    for (;;) {
      const DirInfo *e = (const DirInfo *)p;
      if (dir_push(d, e)) {
        for (u32 i = 0; i < d->n; i++) free(d->ents[i].name);
        free(d->ents);
        free(d);
        return 0;
      }
      if (!e->next) break;
      p += e->next;
    }
  }
  return d;
}

void *opendir(const char *path) {
  int fd = openat(H_AT_FDCWD, path, H_RDONLY | H_DIRECTORY);
  if (fd < 0) return 0;
  void *d = fdopendir(fd);
  if (!d) close(fd);
  return d;
}

int closedir(void *dp) {
  WDir *d = (WDir *)dp;
  if (!d) return fail(E_INVAL);
  for (u32 i = 0; i < d->n; i++) free(d->ents[i].name);
  free(d->ents);
  int fd = d->fd;
  free(d);
  return close(fd);
}

void *readdir(void *dp) {
  WDir *d = (WDir *)dp;
  if (!d || d->at >= d->n) return 0;
  DEnt *e = &d->ents[d->at++];
  __builtin_memset(d->de, 0, sizeof d->de);
  __builtin_memcpy(d->de + DE_INO, &e->ino, 8);
  d->de[DE_TYPE] = e->type;
  usz i = 0;
  while (e->name[i] && i < sizeof d->de - DE_NAME - 1) {
    d->de[DE_NAME + i] = (u8)e->name[i];
    i++;
  }
  return d->de;
}

void rewinddir(void *dp) { if (dp) ((WDir *)dp)->at = 0; }

long telldir(void *dp) { return dp ? (long)((WDir *)dp)->at : -1; }

void seekdir(void *dp, long at) {
  WDir *d = (WDir *)dp;
  if (d && at >= 0 && (u32)at <= d->n) d->at = (u32)at;
}

/* ---- timestamps ---- */

/* The shared code passes {sec, nsec} pairs with UTIME_NOW/UTIME_OMIT in the
   nanosecond slot; Win32 takes a null FILETIME pointer for "leave alone". */
static int set_times(void *h, const i64 *ts) {
  u64 at, mt;
  const u64 *pa = 0, *pm = 0;
  if (ts[1] != (i64)((1l << 30) - 2l)) {
    at = ts[1] == (i64)((1l << 30) - 1l)
           ? ft_now()
           : (u64)ts[0] * 10000000ull + (u64)ts[1] / 100ull +
             116444736000000000ull;
    pa = &at;
  }
  if (ts[3] != (i64)((1l << 30) - 2l)) {
    mt = ts[3] == (i64)((1l << 30) - 1l)
           ? ft_now()
           : (u64)ts[2] * 10000000ull + (u64)ts[3] / 100ull +
             116444736000000000ull;
    pm = &mt;
  }
  if (!pa && !pm) return 0;
  return SetFileTime(h, 0, pa, pm) ? 0 : fail_last();
}

int futimens(int fd, const void *ts) {
  WFd *f = fd_at(fd);
  if (!f) return -1;
  if (!set_times(f->h, (const i64 *)ts)) return 0;
  /* preview1 lets a descriptor opened for reading set times; Win32 wants
     FILE_WRITE_ATTRIBUTES on the handle, so reopen by name for it. */
  if (g_errno != E_ACCES || !f->wpath) return -1;
  void *h = CreateFileW(f->wpath, FILE_WRITE_ATTRS, SHARE_ALL, 0,
                        OPEN_EXISTING, FLAG_BACKUP_SEMANTICS, 0);
  if (h == INVALID_HANDLE) return fail_last();
  int rc = set_times(h, (const i64 *)ts);
  CloseHandle(h);
  return rc;
}

int utimensat(int dirfd, const char *name, const void *ts, int flags) {
  u16 wp[WPATH];
  if (resolve(dirfd, name, wp)) return -1;
  /* FILE_WRITE_ATTRIBUTES rather than GENERIC_WRITE: it is exactly what
     SetFileTime asks for, and it is the only one of the two a directory can
     be opened with. */
  void *h = CreateFileW(wp, FILE_WRITE_ATTRS, SHARE_ALL, 0, OPEN_EXISTING,
                        FLAG_BACKUP_SEMANTICS |
                        ((flags & H_AT_NOFOLLOW) ? FLAG_OPEN_REPARSE_POINT : 0),
                        0);
  if (h == INVALID_HANDLE) return fail_last();
  int rc = set_times(h, (const i64 *)ts);
  CloseHandle(h);
  return rc;
}

/* ---- clocks ---- */

static u64 qpc_freq;

static u64 mono_nanos(void) {
  u64 f = qpc_freq, c;
  if (!f) {
    QueryPerformanceFrequency(&f);
    qpc_freq = f ? f : 1;
    f = qpc_freq;
  }
  QueryPerformanceCounter(&c);
  return (c / f) * 1000000000ull + (c % f) * 1000000000ull / f;
}

/* GetProcessTimes reports in 100ns units but only moves on the scheduler
   tick -- 15.6ms -- so a guest that burns a few milliseconds and asks again
   sees no change at all.  Query*CycleTime counts the cycles actually run,
   which is the same quantity at far finer grain; converting it to
   nanoseconds needs the TSC rate, which nothing reports, so it is measured
   once against the monotonic clock.  A hundred microseconds of spin buys
   about a tenth of a percent, and only a guest that asks for a CPU clock
   ever pays it. */
static u64 cyc_per_us;

static u64 rdtsc(void) {
  u32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((u64)hi << 32) | lo;
}

static void calibrate_tsc(void) {
  if (cyc_per_us) return;
  u64 t0 = mono_nanos(), c0 = rdtsc(), dn;
  do {
    __asm__ __volatile__("pause");
    dn = mono_nanos() - t0;
  } while (dn < 100000ull);
  u64 v = (rdtsc() - c0) * 1000ull / dn;
  cyc_per_us = v ? v : 1000;
}

static u64 cpu_nanos(int thread) {
  u64 cycles = 0;
  int ok = thread ? QueryThreadCycleTime(GetCurrentThread(), &cycles)
                  : QueryProcessCycleTime(GetCurrentProcess(), &cycles);
  if (ok) {
    calibrate_tsc();
    return cycles * 1000ull / cyc_per_us;
  }
  u64 cre, exi, ker, usr;
  ok = thread ? GetThreadTimes(GetCurrentThread(), &cre, &exi, &ker, &usr)
              : GetProcessTimes(GetCurrentProcess(), &cre, &exi, &ker, &usr);
  return ok ? (ker + usr) * 100ull : 0;
}

/* rt/wasi.c's clock_query takes this arm on Windows: clock_gettime's timespec
   is two longs, which are 32-bit here, and the CPU clocks come from a
   different API rather than a different clock id. */
int warmc_win_clock(int want_res, u32 id, u64 *out) {
  switch (id) {
    case 0:                             /* realtime */
      if (want_res) { *out = 100; return 0; }
      *out = (ft_now() - 116444736000000000ull) * 100ull;
      return 0;
    case 1:                             /* monotonic */
      if (want_res) {
        u64 f = qpc_freq;
        if (!f) { QueryPerformanceFrequency(&f); qpc_freq = f ? f : 1; f = qpc_freq; }
        *out = 1000000000ull / f ? 1000000000ull / f : 1;
        return 0;
      }
      *out = mono_nanos();
      return 0;
    case 2:                             /* process cputime */
    case 3:                             /* thread cputime */
      if (want_res) { *out = 100; return 0; }
      *out = cpu_nanos(id == 3);
      return 0;
    default:
      return fail(E_INVAL);
  }
}

/* ---- sleeping and readiness ---- */

/* Sleep()'s resolution is the scheduler tick -- about 15ms by default, and
   never better than 1ms -- so a guest asking for 100us would get ten times
   what it asked for, which is the very thing rt/wasi.c stopped routing
   short waits through poll() to avoid.  The last two milliseconds are spun
   against the monotonic clock instead; the deadline, not the call, is what
   says when the wait is over. */
#define SPIN_NS 2000000ull

static void sleep_until(u64 deadline) {
  for (;;) {
    u64 now = mono_nanos();
    if (now >= deadline) return;
    u64 left = deadline - now;
    if (left > SPIN_NS) {
      u64 ms = (left - SPIN_NS) / 1000000ull;
      Sleep(ms > 4000000ull ? 4000000u : (u32)ms);
    } else {
      __asm__ __volatile__("pause");
    }
  }
}

int nanosleep(const void *req, void *rem) {
  const struct { long sec, nsec; } *r = req;
  if (rem) { struct { long sec, nsec; } *o = rem; o->sec = 0; o->nsec = 0; }
  if (r->sec < 0 || r->nsec < 0 || r->nsec >= 1000000000l) return fail(E_INVAL);
  sleep_until(mono_nanos() + (u64)r->sec * 1000000000ull + (u64)r->nsec);
  return 0;
}

#define POLL_IN  1
#define POLL_OUT 4
#define POLL_HUP 0x10

typedef struct { int fd; short ev, rev; } PollFd;

static short ready_bits(WFd *f, short want) {
  short r = 0;
  if (want & POLL_OUT) r |= POLL_OUT;   /* files, pipes and consoles take it */
  if (want & POLL_IN) {
    if (f->kind == FTYPE_PIPE) {
      u32 avail = 0;
      if (!PeekNamedPipe(f->h, 0, 0, 0, &avail, 0)) r |= POLL_HUP | POLL_IN;
      else if (avail) r |= POLL_IN;
    } else if (f->kind == FTYPE_CHAR) {
      if (WaitForSingleObject(f->h, 0) == 0) r |= POLL_IN;
    } else {
      r |= POLL_IN;                     /* a regular file never blocks */
    }
  }
  return r;
}

/* Win32 has no one call that waits on a mixed set of files, pipes and
   consoles, so readiness is sampled and the wait is a sleep between
   samples.  Every subscription preview1 can raise is level-triggered, so a
   1ms sample period only costs latency. */
int poll(void *fds_, unsigned nfds, int timeout) {
  PollFd *fds = (PollFd *)fds_;
  u64 deadline = timeout >= 0
                   ? mono_nanos() + (u64)(u32)timeout * 1000000ull : 0;
  for (;;) {
    int ready = 0;
    for (unsigned i = 0; i < nfds; i++) {
      WFd *f = fd_at(fds[i].fd);
      fds[i].rev = f ? ready_bits(f, fds[i].ev) : POLL_HUP;
      if (fds[i].rev) ready++;
    }
    if (ready) return ready;
    if (timeout >= 0 && mono_nanos() >= deadline) return 0;
    Sleep(1);
  }
}

/* ---- what this host does not have ---- */

void (*signal(int sig, void (*h)(int)))(int) {
  (void)sig;
  (void)h;
  /* Windows has no SIGPIPE: a write to a pipe with no reader simply fails,
     which is the behaviour rt/wasi.c installs this handler to get. */
  return 0;
}

/* Sockets are the one part of preview1 this host leaves out on Windows;
   Winsock needs its own descriptor kind and its own poll.  A guest only
   reaches these through --tcplisten, which fails first. */
int socket(int d, int t, int p) { (void)d; (void)t; (void)p; return fail(E_NOSYS); }
int setsockopt(int s, int l, int o, const void *v, unsigned int n) {
  (void)s; (void)l; (void)o; (void)v; (void)n;
  return fail(E_NOSYS);
}
int bind(int s, const void *a, unsigned int n) {
  (void)s; (void)a; (void)n;
  return fail(E_NOSYS);
}
int listen(int s, int n) { (void)s; (void)n; return fail(E_NOSYS); }
int accept(int s, void *a, void *n) { (void)s; (void)a; (void)n; return fail(E_NOSYS); }
long recv(int s, void *b, usz n, int f) {
  (void)s; (void)b; (void)n; (void)f;
  return fail(E_NOSYS);
}
long send(int s, const void *b, usz n, int f) {
  (void)s; (void)b; (void)n; (void)f;
  return fail(E_NOSYS);
}
int shutdown(int s, int how) { (void)s; (void)how; return fail(E_NOSYS); }
