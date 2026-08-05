#include "wasifs.h"

u32 wasifs_errno(int e) {
  switch (e) {
    case 1:  return 63;
    case 2:  return 44;
    case 4:  return 27;
    case 5:  return 29;
    case 6:  return 60;
    case 9:  return 8;
    case 12: return 48;
    case 13: return 2;
    case 14: return 21;
    case 16: return 10;
    case 17: return 20;
    case 18: return 75;
    case 20: return 54;
    case 21: return 31;
    case 22: return 28;
    case 23: return 41;
    case 24: return 33;
    case 25: return 59;
    case 27: return 22;
    case 28: return 51;
    case 29: return 70;
    case 30: return 69;
    case 31: return 34;
    case 32: return 64;
    case 34: return 68;
    case H_EAGAIN:       return 6;
    case H_ELOOP:        return 32;
    case H_ENAMETOOLONG: return 37;
    case H_ENOTEMPTY:    return 55;
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

void wasifs_str_copy(char *d, const char *s, unsigned long n) {
  unsigned long i = 0;
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

u32 wasifs_walk(int root, const char *path, int *out_dir, char *leaf) {
  int stack[MAXDEPTH];
  int sp = 0;
  char work[PATHMAX * 2];
  int links = 0;
  u32 rc = 0;

  if (path[0] == '/') return 63;
  wasifs_str_copy(work, path, PATHMAX * 2 - 1);
  stack[0] = root;
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

      if (sp == 0) { rc = 63; goto done; }
      close(stack[sp--]);
      *p = save;
      continue;
    }
    if (last) {
      wasifs_str_copy(leaf, c, PATHMAX - 1);
      *p = save;
      break;
    }

    int fd = openat(stack[sp], c, H_RDONLY | H_DIRECTORY | H_NOFOLLOW);
    if (fd < 0) {
      int e = h_errno;
      if (e != H_ELOOP && e != H_ENOTDIR) { rc = wasifs_errno(e); goto done; }
      char link[PATHMAX];
      long n = readlinkat(stack[sp], c, link, PATHMAX - 1);
      if (n < 0) { rc = wasifs_errno(e); goto done; }
      link[n] = 0;
      if (link[0] == '/') { rc = 63; goto done; }
      if (++links > MAXLINKS) { rc = 32; goto done; }

      char rest[PATHMAX * 2];
      *p = save;
      wasifs_str_copy(rest, p, PATHMAX * 2 - 1);
      char joined[PATHMAX * 2];
      unsigned long k = 0;
      for (long j = 0; link[j] && k < PATHMAX * 2 - 2; j++) joined[k++] = link[j];
      for (unsigned long j = 0; rest[j] && k < PATHMAX * 2 - 2; j++) joined[k++] = rest[j];
      joined[k] = 0;
      wasifs_str_copy(work, joined, PATHMAX * 2 - 1);
      p = work;
      continue;
    }
    if (sp + 1 >= MAXDEPTH) { close(fd); rc = 37; goto done; }
    stack[++sp] = fd;
    *p = save;
  }
  *out_dir = stack[sp];

  for (int i = 1; i < sp; i++) close(stack[i]);
  return 0;
done:
  for (int i = 1; i <= sp; i++) close(stack[i]);
  return rc;
}

void wasifs_walk_done(int root, int dir) { if (dir != root) close(dir); }

u32 wasifs_splice_leaf(char *path, const char *link) {
  unsigned long cut = 0;
  for (unsigned long i = 0; path[i]; i++) if (path[i] == '/') cut = i + 1;
  char next[PATHMAX];
  if (cut >= PATHMAX) return 37;
  for (unsigned long i = 0; i < cut; i++) next[i] = path[i];
  unsigned long j = cut;
  for (unsigned long i = 0; link[i]; i++) {
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
