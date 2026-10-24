/* The host of test/shared/tramp.wat built with warmc --shared: its import
   runs another trampoline in the library while one of the library's
   trampolined functions is inside it. */

#include "tramp.h"

#include <stdio.h>

static int calls;

static void back(void *ctx, int32_t from) {
  (void)ctx;
  calls++;
  tr_run(from);
}

int main(void) {
  tramp_bind_import("env", "back", (void *)back, 0);
  int32_t r = tr_go();
  if (r != 3090 || calls != 2) {
    fprintf(stderr, "FAIL[shared/tramp] tr_go() = %d with %d calls back, not 3090 with 2\n",
            (int)r, calls);
    return 1;
  }
  return 0;
}
