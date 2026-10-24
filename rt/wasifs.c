#include "wasifs.h"

/* The host's errno as WASI's, every one WASI has a name for: 1 to 34 are
   POSIX's on every host, above that each numbers its own (Windows answers
   with Linux's, rt/winposix.c).  One WASI has no name for is EIO. */
u32 wasifs_errno(int e) {
  switch (e) {
    case 1:  return 63;   /* EPERM */
    case 2:  return 44;   /* ENOENT */
    case 3:  return 71;   /* ESRCH */
    case 4:  return 27;   /* EINTR */
    case 5:  return 29;   /* EIO */
    case 6:  return 60;   /* ENXIO */
    case 7:  return 1;    /* E2BIG */
    case 8:  return 45;   /* ENOEXEC */
    case 9:  return 8;    /* EBADF */
    case 10: return 12;   /* ECHILD */
    case 12: return 48;   /* ENOMEM */
    case 13: return 2;    /* EACCES */
    case 14: return 21;   /* EFAULT */
    case 16: return 10;   /* EBUSY */
    case 17: return 20;   /* EEXIST */
    case 18: return 75;   /* EXDEV */
    case 19: return 43;   /* ENODEV */
    case 20: return 54;   /* ENOTDIR */
    case 21: return 31;   /* EISDIR */
    case 22: return 28;   /* EINVAL */
    case 23: return 41;   /* ENFILE */
    case 24: return 33;   /* EMFILE */
    case 25: return 59;   /* ENOTTY */
    case 26: return 74;   /* ETXTBSY */
    case 27: return 22;   /* EFBIG */
    case 28: return 51;   /* ENOSPC */
    case 29: return 70;   /* ESPIPE */
    case 30: return 69;   /* EROFS */
    case 31: return 34;   /* EMLINK */
    case 32: return 64;   /* EPIPE */
    case 33: return 18;   /* EDOM */
    case 34: return 68;   /* ERANGE */
#if defined(WARMC_DARWIN)
    case 11: return 16;   /* EDEADLK */
    case 35: return 6;    /* EAGAIN */
    case 36: return 26;   /* EINPROGRESS */
    case 37: return 7;    /* EALREADY */
    case 38: return 57;   /* ENOTSOCK */
    case 39: return 17;   /* EDESTADDRREQ */
    case 40: return 35;   /* EMSGSIZE */
    case 41: return 67;   /* EPROTOTYPE */
    case 42: return 50;   /* ENOPROTOOPT */
    case 43: return 66;   /* EPROTONOSUPPORT */
    case 45: return 58;   /* ENOTSUP */
    case 47: return 5;    /* EAFNOSUPPORT */
    case 48: return 3;    /* EADDRINUSE */
    case 49: return 4;    /* EADDRNOTAVAIL */
    case 50: return 38;   /* ENETDOWN */
    case 51: return 40;   /* ENETUNREACH */
    case 52: return 39;   /* ENETRESET */
    case 53: return 13;   /* ECONNABORTED */
    case 54: return 15;   /* ECONNRESET */
    case 55: return 42;   /* ENOBUFS */
    case 56: return 30;   /* EISCONN */
    case 57: return 53;   /* ENOTCONN */
    case 60: return 73;   /* ETIMEDOUT */
    case 61: return 14;   /* ECONNREFUSED */
    case 62: return 32;   /* ELOOP */
    case 63: return 37;   /* ENAMETOOLONG */
    case 65: return 23;   /* EHOSTUNREACH */
    case 66: return 55;   /* ENOTEMPTY */
    case 69: return 19;   /* EDQUOT */
    case 70: return 72;   /* ESTALE */
    case 77: return 46;   /* ENOLCK */
    case 78: return 52;   /* ENOSYS */
    case 84: return 61;   /* EOVERFLOW */
    case 89: return 11;   /* ECANCELED */
    case 90: return 24;   /* EIDRM */
    case 91: return 49;   /* ENOMSG */
    case 92: return 25;   /* EILSEQ */
    case 94: return 9;    /* EBADMSG */
    case 95: return 36;   /* EMULTIHOP */
    case 97: return 47;   /* ENOLINK */
    case 100: return 65;  /* EPROTO */
    case 102: return 58;  /* EOPNOTSUPP */
    case 104: return 56;  /* ENOTRECOVERABLE */
    case 105: return 62;  /* EOWNERDEAD */
#else
    case 11: return 6;    /* EAGAIN */
    case 35: return 16;   /* EDEADLK */
    case 36: return 37;   /* ENAMETOOLONG */
    case 37: return 46;   /* ENOLCK */
    case 38: return 52;   /* ENOSYS */
    case 39: return 55;   /* ENOTEMPTY */
    case 40: return 32;   /* ELOOP */
    case 42: return 49;   /* ENOMSG */
    case 43: return 24;   /* EIDRM */
    case 67: return 47;   /* ENOLINK */
    case 71: return 65;   /* EPROTO */
    case 72: return 36;   /* EMULTIHOP */
    case 74: return 9;    /* EBADMSG */
    case 75: return 61;   /* EOVERFLOW */
    case 84: return 25;   /* EILSEQ */
    case 88: return 57;   /* ENOTSOCK */
    case 89: return 17;   /* EDESTADDRREQ */
    case 90: return 35;   /* EMSGSIZE */
    case 91: return 67;   /* EPROTOTYPE */
    case 92: return 50;   /* ENOPROTOOPT */
    case 93: return 66;   /* EPROTONOSUPPORT */
    case 95: return 58;   /* ENOTSUP */
    case 97: return 5;    /* EAFNOSUPPORT */
    case 98: return 3;    /* EADDRINUSE */
    case 99: return 4;    /* EADDRNOTAVAIL */
    case 100: return 38;  /* ENETDOWN */
    case 101: return 40;  /* ENETUNREACH */
    case 102: return 39;  /* ENETRESET */
    case 103: return 13;  /* ECONNABORTED */
    case 104: return 15;  /* ECONNRESET */
    case 105: return 42;  /* ENOBUFS */
    case 106: return 30;  /* EISCONN */
    case 107: return 53;  /* ENOTCONN */
    case 110: return 73;  /* ETIMEDOUT */
    case 111: return 14;  /* ECONNREFUSED */
    case 113: return 23;  /* EHOSTUNREACH */
    case 114: return 7;   /* EALREADY */
    case 115: return 26;  /* EINPROGRESS */
    case 116: return 72;  /* ESTALE */
    case 122: return 19;  /* EDQUOT */
    case 125: return 11;  /* ECANCELED */
    case 130: return 62;  /* EOWNERDEAD */
    case 131: return 56;  /* ENOTRECOVERABLE */
#endif
    default: return 29;
  }
}

u64 wasifs_st_u64(const u8 *st, int off) {
  u64 v = 0;
  __builtin_memcpy(&v, st + off, 8);
  return v;
}

u32 wasifs_st_small(const u8 *st, int off) {
  u32 v = 0;
  __builtin_memcpy(&v, st + off, ST_SMALL);
  return v;
}

u64 wasifs_st_time(const u8 *st, int off) {
  return wasifs_st_u64(st, off) * 1000000000ull + wasifs_st_u64(st, off + 8);
}

u8 wasifs_filetype(const u8 *st) {
  switch (wasifs_st_small(st, ST_OFF_MODE) & 0170000u) {
    case 0040000u: return FT_DIR;
    case 0100000u: return FT_REGULAR;
    case 0120000u: return FT_SYMLINK;
    case 0020000u: return FT_CHAR;
    case 0060000u: return FT_BLOCK;
    default:       return FT_UNKNOWN;
  }
}

void wasifs_str_copy(char *d, const char *s, usz n) {
  usz i = 0;
  for (; i < n && s[i]; i++) d[i] = s[i];
  d[i] = 0;
}

int wasifs_str_eq(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

u32 wasifs_de_namlen(const u8 *de) {
#if defined(WARMC_DARWIN)
  unsigned short v;
  __builtin_memcpy(&v, de + DE_NAMLEN, 2);
  return v;
#else
  const char *p = (const char *)de + DE_NAME;
  const char *q = p;
  while (*q) q++;
  return (u32)(q - p);
#endif
}

u8 wasifs_de_filetype(u8 dt) {
  switch (dt) {
    case 4:  return FT_DIR;
    case 8:  return FT_REGULAR;
    case 10: return FT_SYMLINK;
    case 2:  return FT_CHAR;
    case 6:  return FT_BLOCK;
    default: return FT_UNKNOWN;
  }
}

static usz str_len(const char *s) {
  usz n = 0;
  while (s[n]) n++;
  return n;
}

/* A directory below the walk's current one, for walking through: opened
   for search, or for reading on a system without that; never through a
   link. */
static int open_through(int at, const char *name) {
  int fd = openat(at, name, H_SEARCH | H_DIRECTORY | H_NOFOLLOW);
  if (fd < 0 && H_SEARCH != 0 && h_errno == 22 /* EINVAL: no search open */)
    fd = openat(at, name, H_RDONLY | H_DIRECTORY | H_NOFOLLOW);
  return fd;
}

/* The directory `depth` names down from root, opened again: what ".." goes
   back to, since the walk keeps only its current directory open.  Each was
   a directory when the walk came through it; one that is now a link is not
   gone through. */
static int reopen(int root, const char *names, const u16 *at, u32 depth, u32 *rc) {
  int cur = root;
  for (u32 i = 0; i < depth; i++) {
    int fd = open_through(cur, names + at[i]);
    int e = h_errno;
    if (cur != root) close(cur);
    if (fd < 0) { *rc = wasifs_errno(e); return -1; }
    cur = fd;
  }
  return cur;
}

u32 wasifs_walk(int root, const char *path, int *out_dir, char *leaf) {
  /* the names gone down through, for ".." to go back by */
  char names[PATHMAX * 2];
  u16 at[MAXDEPTH];
  usz nlen = 0;
  u32 depth = 0;
  int cur = root;
  char work[PATHMAX * 2];
  int links = 0;
  u32 rc = 0;

  if (path[0] == '/') return 63;
  wasifs_str_copy(work, path, PATHMAX * 2 - 1);
  leaf[0] = 0;

  char *p = work;
  for (;;) {
    while (*p == '/') p++;
    if (!*p) break;
    char *c = p;
    while (*p && *p != '/') p++;
    int last = (*p == 0);
    char save = *p;
    *p = 0;

    if (wasifs_str_eq(c, ".")) { *p = save; continue; }
    if (wasifs_str_eq(c, "..")) {

      if (depth == 0) { rc = 63; goto done; }
      depth--;
      nlen = at[depth];
      if (cur != root) close(cur);
      cur = reopen(root, names, at, depth, &rc);
      if (cur < 0) { cur = root; goto done; }
      *p = save;
      continue;
    }
    if (last) {
      wasifs_str_copy(leaf, c, PATHMAX - 1);
      *p = save;
      break;
    }

    int fd = open_through(cur, c);
    if (fd < 0) {
      int e = h_errno;
      if (e != H_ELOOP && e != H_ENOTDIR) { rc = wasifs_errno(e); goto done; }
      char link[PATHMAX];
      long n = readlinkat(cur, c, link, PATHMAX - 1);
      if (n < 0) { rc = wasifs_errno(e); goto done; }
      link[n] = 0;
      if (link[0] == '/') { rc = 63; goto done; }
      if (++links > MAXLINKS) { rc = 32; goto done; }

      /* the link's text in place of its name, then the rest of the path,
         which together must fit */
      *p = save;
      usz rest = str_len(p);
      if ((usz)n + rest > sizeof work - 1) { rc = 37; goto done; }
      usz from = (usz)(p - work);
      if ((usz)n > from) for (usz j = rest + 1; j-- > 0;) work[n + j] = work[from + j];
      else for (usz j = 0; j <= rest; j++) work[n + j] = work[from + j];
      for (usz j = 0; j < (usz)n; j++) work[j] = link[j];
      p = work;
      continue;
    }
    usz cl = str_len(c);
    if (depth >= MAXDEPTH || nlen + cl + 1 > sizeof names) { close(fd); rc = 37; goto done; }
    at[depth++] = (u16)nlen;
    for (usz i = 0; i <= cl; i++) names[nlen + i] = c[i];
    nlen += cl + 1;
    if (cur != root) close(cur);
    cur = fd;
    *p = save;
  }
  *out_dir = cur;
  return 0;
done:
  if (cur != root) close(cur);
  return rc;
}

void wasifs_walk_done(int root, int dir) { if (dir != root) close(dir); }

int wasifs_trim_slash(char *path) {
  usz n = 0;
  while (path[n]) n++;
  int any = 0;
  while (n > 1 && path[n - 1] == '/') { path[--n] = 0; any = 1; }
  return any;
}

u32 wasifs_splice_leaf(char *path, const char *link) {
  usz cut = 0;
  for (usz i = 0; path[i]; i++) if (path[i] == '/') cut = i + 1;
  char next[PATHMAX];
  if (cut >= PATHMAX) return 37;
  for (usz i = 0; i < cut; i++) next[i] = path[i];
  usz j = cut;
  for (usz i = 0; link[i]; i++) {
    if (j >= PATHMAX - 1) return 37;
    next[j++] = link[i];
  }
  next[j] = 0;
  wasifs_str_copy(path, next, PATHMAX - 1);
  return 0;
}

u32 wasifs_walk_leaf(int root, char *path, int *out_dir, char *leaf,
                     int follow) {
  int links = 0;
  for (;;) {
    u32 rc = wasifs_walk(root, path, out_dir, leaf);
    if (rc) return rc;
    if (!follow) return 0;

    const char *name = leaf[0] ? leaf : ".";
    u8 st[ST_SIZE];
    if (fstatat(*out_dir, name, st, H_AT_NOFOLLOW) < 0) return 0;
    if (wasifs_filetype(st) != FT_SYMLINK) return 0;

    char link[PATHMAX];
    long n = readlinkat(*out_dir, name, link, PATHMAX - 1);
    wasifs_walk_done(root, *out_dir);
    if (n < 0) return wasifs_errno(h_errno);
    link[n] = 0;
    if (link[0] == '/') return 63;
    if (++links > MAXLINKS) return 32;
    rc = wasifs_splice_leaf(path, link);
    if (rc) return rc;
  }
}
