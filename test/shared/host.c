/* A C program linked against test/shared/basic.wat built with warmc
   --shared, through the header warmc writes beside it.  What ctypes cannot
   easily show (test/shared/basic.py covers the rest): several threads
   calling at once, the collector under them, a host import that calls back
   into the library, and the
   program's own SIGSEGV handler still getting its own faults once the
   library's runtime has put its handler in front of it: a signal handler,
   or on Windows a vectored exception handler. */

#include "basic.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL[shared/host] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* The import: twice, which on its way calls back into the library. */
static int64_t twice(void *ctx, int64_t v) {
  CHECK(ctx == (void *)&failures, "twice got ctx %p", ctx);
  CHECK(fib(10) == 55, "a call back into the library from an import");
  return 2 * v;
}

/* Each thread also allocates 8 x 32 MB, so the collector runs between and
   during the calls of other threads. */
#define THREADS 8
#define CALLS 20000
#define NODES 1000000
static void *hammer(void *arg) {
  int32_t t = (int32_t)(intptr_t)arg, bad = 0;
  for (int32_t i = 0; i < CALLS; i++) {
    if (add(t, i) != t + i) bad++;
    if (i % (CALLS / 8) == 0 &&
        list_sum(NODES + t) != (int64_t)(NODES + t) * (NODES + t - 1) / 2)
      bad++;
  }
  return (void *)(intptr_t)bad;
}

/* A page of the program's own that faults until its handler opens it. */
static volatile char *own_page;
static volatile int own_faults;
#if defined(_WIN32)
static LONG CALLBACK own_handler(EXCEPTION_POINTERS *ep) {
  EXCEPTION_RECORD *r = ep->ExceptionRecord;
  if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      (char *)r->ExceptionInformation[1] != own_page)
    return EXCEPTION_CONTINUE_SEARCH;
  own_faults++;
  DWORD old;
  VirtualProtect((void *)own_page, 4096, PAGE_READWRITE, &old);
  return EXCEPTION_CONTINUE_EXECUTION;
}
static void own_setup(void) {
  AddVectoredExceptionHandler(1, own_handler);
  own_page = VirtualAlloc(0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
}
#else
static void own_handler(int sig, siginfo_t *si, void *uc) {
  (void)sig; (void)uc;
  if ((char *)si->si_addr != own_page) _exit(97);
  own_faults++;
  mprotect((void *)own_page, 4096, PROT_READ | PROT_WRITE);
}
static void own_setup(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = own_handler;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, 0);
  sigaction(SIGBUS, &sa, 0);
  own_page = mmap(0, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
}
#endif

int main(void) {
  own_setup();

  CHECK(basic_bind_import("env", "twice", (void *)twice, &failures) == 0, "bind");

  /* The first call makes the instance, and with it the runtime's handler. */
  CHECK(twice_plus_one(20) == 41, "twice_plus_one(20) = %lld",
        (long long)twice_plus_one(20));
  CHECK(basic_bind_import("env", "twice", (void *)twice, 0) == 2,
        "a bind after the first call is refused");

  own_page[0] = 1;
  CHECK(own_faults == 1, "the program's own fault reached its handler %d times",
        own_faults);

  CHECK(quot(1, 0) == 0 && basic_last_trap() == 3, "quot(1, 0) traps");
  size_t size = 0;
  uint8_t *mem = basic_memory(&size);
  CHECK(mem && size == 65536, "memory %p, %zu bytes", (void *)mem, size);
  CHECK(sum(65530, 10) == 0 && basic_last_trap() == 2,
        "a read past memory traps out of bounds: %u", basic_last_trap());
  CHECK(quot(9, 3) == 3 && basic_last_trap() == 0, "and the next call clears it");

  int32_t before = calls();
  pthread_t th[THREADS];
  for (int i = 0; i < THREADS; i++)
    pthread_create(&th[i], 0, hammer, (void *)(intptr_t)i);
  for (int i = 0; i < THREADS; i++) {
    void *bad;
    pthread_join(th[i], &bad);
    CHECK(!bad, "thread %d saw %d wrong sums", i, (int)(intptr_t)bad);
  }
  CHECK(calls() - before == THREADS * CALLS,
        "%d threads made %d calls, the instance counted %d", THREADS,
        THREADS * CALLS, calls() - before);

  if (failures) return 1;
  printf("shared/host: imports, callbacks, traps, memory, %d threads with the "
         "collector, and the program's own fault handler\n", THREADS);
  return 0;
}
