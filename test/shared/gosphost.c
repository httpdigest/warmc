/* The host of test/shared/gosp.wat built with warmc --shared: its import
   reads the library's stack pointer back through an export while the Go
   function that called it is inside, and moves it. */

#include "gosp.h"

#include <stdio.h>

static int32_t passed, seen;

static int64_t back(void *ctx, int32_t p) {
  (void)ctx;
  passed = p;
  seen = go_sp();
  go_move(16);
  return 1000;
}

int main(void) {
  gosp_bind_import("env", "back", (void *)back, 0);
  int64_t r = go_run();
  /* $g's frame at 8192 - 8 - 32; it pops 40 from 16 above that, and reads
     the 5 it stored from where its pointer then is: 16 bytes above, zero */
  int64_t want = ((int64_t)1000 << 32) | (8192 - 8 - 32 + 16 + 40);
  int bad = 0;
  if (passed != 8152 || seen != 8152) {
    fprintf(stderr, "FAIL[shared/gosp] the import saw the pointer at %d, the global at %d, not 8152\n",
            (int)passed, (int)seen);
    bad = 1;
  }
  if (r != want) {
    fprintf(stderr, "FAIL[shared/gosp] go_run() = %#llx, not %#llx\n", (long long)r, (long long)want);
    bad = 1;
  }
  return bad;
}
