/* The host's errno as WASI's (rt/wasifs.c wasifs_errno), checked against
   this host's own <errno.h>: every name WASI has, by the number it has on
   this system, must come out as WASI's number for it.  The table is the
   WASI preview 1 errno enum; built and run by test/run_wasi.sh. */
#include <errno.h>
#include <stdio.h>

unsigned wasifs_errno(int e);

static const struct { int host; unsigned wasi; const char *name; } T[] = {
#ifdef EPERM
  { EPERM, 63, "EPERM" },
#endif
#ifdef ENOENT
  { ENOENT, 44, "ENOENT" },
#endif
#ifdef ESRCH
  { ESRCH, 71, "ESRCH" },
#endif
#ifdef EINTR
  { EINTR, 27, "EINTR" },
#endif
#ifdef EIO
  { EIO, 29, "EIO" },
#endif
#ifdef ENXIO
  { ENXIO, 60, "ENXIO" },
#endif
#ifdef E2BIG
  { E2BIG, 1, "E2BIG" },
#endif
#ifdef ENOEXEC
  { ENOEXEC, 45, "ENOEXEC" },
#endif
#ifdef EBADF
  { EBADF, 8, "EBADF" },
#endif
#ifdef ECHILD
  { ECHILD, 12, "ECHILD" },
#endif
#ifdef EAGAIN
  { EAGAIN, 6, "EAGAIN" },
#endif
#ifdef ENOMEM
  { ENOMEM, 48, "ENOMEM" },
#endif
#ifdef EACCES
  { EACCES, 2, "EACCES" },
#endif
#ifdef EFAULT
  { EFAULT, 21, "EFAULT" },
#endif
#ifdef EBUSY
  { EBUSY, 10, "EBUSY" },
#endif
#ifdef EEXIST
  { EEXIST, 20, "EEXIST" },
#endif
#ifdef EXDEV
  { EXDEV, 75, "EXDEV" },
#endif
#ifdef ENODEV
  { ENODEV, 43, "ENODEV" },
#endif
#ifdef ENOTDIR
  { ENOTDIR, 54, "ENOTDIR" },
#endif
#ifdef EISDIR
  { EISDIR, 31, "EISDIR" },
#endif
#ifdef EINVAL
  { EINVAL, 28, "EINVAL" },
#endif
#ifdef ENFILE
  { ENFILE, 41, "ENFILE" },
#endif
#ifdef EMFILE
  { EMFILE, 33, "EMFILE" },
#endif
#ifdef ENOTTY
  { ENOTTY, 59, "ENOTTY" },
#endif
#ifdef ETXTBSY
  { ETXTBSY, 74, "ETXTBSY" },
#endif
#ifdef EFBIG
  { EFBIG, 22, "EFBIG" },
#endif
#ifdef ENOSPC
  { ENOSPC, 51, "ENOSPC" },
#endif
#ifdef ESPIPE
  { ESPIPE, 70, "ESPIPE" },
#endif
#ifdef EROFS
  { EROFS, 69, "EROFS" },
#endif
#ifdef EMLINK
  { EMLINK, 34, "EMLINK" },
#endif
#ifdef EPIPE
  { EPIPE, 64, "EPIPE" },
#endif
#ifdef EDOM
  { EDOM, 18, "EDOM" },
#endif
#ifdef ERANGE
  { ERANGE, 68, "ERANGE" },
#endif
#ifdef EDEADLK
  { EDEADLK, 16, "EDEADLK" },
#endif
#ifdef ENAMETOOLONG
  { ENAMETOOLONG, 37, "ENAMETOOLONG" },
#endif
#ifdef ENOLCK
  { ENOLCK, 46, "ENOLCK" },
#endif
#ifdef ENOSYS
  { ENOSYS, 52, "ENOSYS" },
#endif
#ifdef ENOTEMPTY
  { ENOTEMPTY, 55, "ENOTEMPTY" },
#endif
#ifdef ELOOP
  { ELOOP, 32, "ELOOP" },
#endif
#ifdef ENOMSG
  { ENOMSG, 49, "ENOMSG" },
#endif
#ifdef EIDRM
  { EIDRM, 24, "EIDRM" },
#endif
#ifdef ENOLINK
  { ENOLINK, 47, "ENOLINK" },
#endif
#ifdef EPROTO
  { EPROTO, 65, "EPROTO" },
#endif
#ifdef EMULTIHOP
  { EMULTIHOP, 36, "EMULTIHOP" },
#endif
#ifdef EBADMSG
  { EBADMSG, 9, "EBADMSG" },
#endif
#ifdef EOVERFLOW
  { EOVERFLOW, 61, "EOVERFLOW" },
#endif
#ifdef EILSEQ
  { EILSEQ, 25, "EILSEQ" },
#endif
#ifdef ENOTSOCK
  { ENOTSOCK, 57, "ENOTSOCK" },
#endif
#ifdef EDESTADDRREQ
  { EDESTADDRREQ, 17, "EDESTADDRREQ" },
#endif
#ifdef EMSGSIZE
  { EMSGSIZE, 35, "EMSGSIZE" },
#endif
#ifdef EPROTOTYPE
  { EPROTOTYPE, 67, "EPROTOTYPE" },
#endif
#ifdef ENOPROTOOPT
  { ENOPROTOOPT, 50, "ENOPROTOOPT" },
#endif
#ifdef EPROTONOSUPPORT
  { EPROTONOSUPPORT, 66, "EPROTONOSUPPORT" },
#endif
#ifdef ENOTSUP
  { ENOTSUP, 58, "ENOTSUP" },
#endif
#ifdef EOPNOTSUPP
  { EOPNOTSUPP, 58, "EOPNOTSUPP" },
#endif
#ifdef EAFNOSUPPORT
  { EAFNOSUPPORT, 5, "EAFNOSUPPORT" },
#endif
#ifdef EADDRINUSE
  { EADDRINUSE, 3, "EADDRINUSE" },
#endif
#ifdef EADDRNOTAVAIL
  { EADDRNOTAVAIL, 4, "EADDRNOTAVAIL" },
#endif
#ifdef ENETDOWN
  { ENETDOWN, 38, "ENETDOWN" },
#endif
#ifdef ENETUNREACH
  { ENETUNREACH, 40, "ENETUNREACH" },
#endif
#ifdef ENETRESET
  { ENETRESET, 39, "ENETRESET" },
#endif
#ifdef ECONNABORTED
  { ECONNABORTED, 13, "ECONNABORTED" },
#endif
#ifdef ECONNRESET
  { ECONNRESET, 15, "ECONNRESET" },
#endif
#ifdef ENOBUFS
  { ENOBUFS, 42, "ENOBUFS" },
#endif
#ifdef EISCONN
  { EISCONN, 30, "EISCONN" },
#endif
#ifdef ENOTCONN
  { ENOTCONN, 53, "ENOTCONN" },
#endif
#ifdef ETIMEDOUT
  { ETIMEDOUT, 73, "ETIMEDOUT" },
#endif
#ifdef ECONNREFUSED
  { ECONNREFUSED, 14, "ECONNREFUSED" },
#endif
#ifdef EHOSTUNREACH
  { EHOSTUNREACH, 23, "EHOSTUNREACH" },
#endif
#ifdef EALREADY
  { EALREADY, 7, "EALREADY" },
#endif
#ifdef EINPROGRESS
  { EINPROGRESS, 26, "EINPROGRESS" },
#endif
#ifdef ESTALE
  { ESTALE, 72, "ESTALE" },
#endif
#ifdef EDQUOT
  { EDQUOT, 19, "EDQUOT" },
#endif
#ifdef ECANCELED
  { ECANCELED, 11, "ECANCELED" },
#endif
#ifdef EOWNERDEAD
  { EOWNERDEAD, 62, "EOWNERDEAD" },
#endif
#ifdef ENOTRECOVERABLE
  { ENOTRECOVERABLE, 56, "ENOTRECOVERABLE" },
#endif
};

int main(void) {
  int bad = 0;
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    unsigned got = wasifs_errno(T[i].host);
    if (got != T[i].wasi) {
      printf("FAIL[wasi/errno]: %s (%d here) maps to %u, not %u\n", T[i].name, T[i].host,
             got, T[i].wasi);
      bad = 1;
    }
  }
  /* one WASI has no name for */
  if (wasifs_errno(10000) != 29) { printf("FAIL[wasi/errno]: an unknown errno is not EIO\n"); bad = 1; }
  if (!bad) printf("%u errnos map to WASI's\n", (unsigned)(sizeof T / sizeof T[0]));
  return bad;
}
