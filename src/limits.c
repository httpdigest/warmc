/* How many processors the process may use: the threads a compile takes
   (compile.c) and the stores' collectors the JIT runs at once (capi.c).
   The processors online, fewer where the process's affinity mask allows
   fewer, and fewer again where its cgroup's CPU quota does (a container's
   --cpus): a quota of 1.5 processors is two threads, of 0.5 one.  The
   cgroup's files are read as rt/cgroup.inc does for the runtime's heap.
   WARMC_LIMITS_DEBUG prints what was found; WARMC_CGROUP_ROOT puts a tree
   laid out by hand in front of every path, there and in the runtime. */
#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif
#include "warmc.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../rt/cgroup.inc"

#ifdef _WIN32
unsigned long __stdcall GetActiveProcessorCount(unsigned short group);
#endif

static long read_whole(const char *path, char *buf, long cap) {
#ifdef _WIN32
  (void)path;
  (void)buf;
  (void)cap;
  return -1;
#else
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  long n = 0;
  for (;;) {
    ssize_t r = read(fd, buf + n, (size_t)(cap - 1 - n));
    if (r <= 0) break;
    n += r;
    if (n == cap - 1) break;
  }
  close(fd);
  buf[n] = 0;
  return n;
#endif
}

static pthread_once_t g_cpus_once = PTHREAD_ONCE_INIT;
static long g_cpus;

static void cpus_init(void) {
#ifdef _WIN32
  unsigned long w = GetActiveProcessorCount((unsigned short)0xffff);
  long online = w ? (long)w : 1;
#else
  long online = sysconf(_SC_NPROCESSORS_ONLN);
#endif
  if (online < 1) online = 1;
  long n = online, affinity = 0, quota = 0;
#if defined(__linux__)
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof set, &set) == 0) {
    affinity = CPU_COUNT(&set);
    if (affinity > 0 && affinity < n) n = affinity;
  }
#endif
  const char *root = warmc_getenv("WARMC_CGROUP_ROOT");
  CgLimits lim = { 0, 0 };
#if defined(__linux__)
  int look = 1;
#else
  int look = root != NULL;
#endif
  if (look) {
    long cap = 1l << 20;
    char *buf = malloc((size_t)cap);
    if (buf) {
      cg_limits(read_whole, root ? root : "", buf, cap, &lim);
      free(buf);
    }
  }
  if (lim.cpu_milli) {
    quota = (long)((lim.cpu_milli + 999) / 1000);
    if (quota < n) n = quota;
  }
  if (n < 1) n = 1;
  g_cpus = n;
  if (warmc_getenv("WARMC_LIMITS_DEBUG"))
    fprintf(stderr, "warmc: %ld processors (online %ld, affinity %ld, cgroup quota %llu.%03llu)\n",
            n, online, affinity, lim.cpu_milli / 1000, lim.cpu_milli % 1000);
}

long warmc_host_cpus(void) {
  pthread_once(&g_cpus_once, cpus_init);
  return g_cpus;
}
