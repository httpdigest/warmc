/* Driver for the data-into-an-imported-memory pair: instantiate A, whose
 * start fills its memory with 0xa5, register it, instantiate B, whose data
 * segments go into that memory, and compare the first 1 KB with what the
 * segments make of it: theirs where they are, zeros included, and 0xa5
 * between them. */
#include "../../rt/warm_rt.h"

extern const ModuleDesc dpa_module;
extern const ModuleDesc dpb_module;

/* The CLI harness in the runtime refers to this; a driver replaces the
 * harness but not the reference. */
const ModuleDesc warmc_module = { 0 };

static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(1, s, (usz)(p - s));
}

static const struct { u32 at, len; const char *bytes; } SEGS[] = {
  { 100, 9, "\x01\x02\0\0\0\0\0\0\x03" },
  { 140, 1, "\x04" },
  { 300, 2, "\x07\x08" },
  { 304, 1, "\x09" },
  { 400, 20, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" },
  { 500, 20, "\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d" },
  { 520, 20, "\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d" },
};

static int drive(int argc, char **argv);

int (*warmc_driver_hook)(int, char **) = drive;

static int drive(int argc, char **argv) {
  (void)argc; (void)argv;
  WarmInstance *a = warmc_instantiate(&dpa_module);
  warmc_register("A", a);
  warmc_instantiate(&dpb_module);
  u8 want[1024];
  for (u32 i = 0; i < 1024; i++) want[i] = 0xa5;
  for (u32 s = 0; s < sizeof SEGS / sizeof SEGS[0]; s++)
    for (u32 i = 0; i < SEGS[s].len; i++) want[SEGS[s].at + i] = (u8)SEGS[s].bytes[i];
  for (u32 i = 0; i < 1024; i++)
    if (a->mem_base[i] != want[i]) {
      char msg[] = "byte 0000 differs\n";
      u32 v = i;
      for (int k = 8; k >= 5; k--) { msg[k] = (char)('0' + v % 10); v /= 10; }
      out(msg);
      return 1;
    }
  out("ok\n");
  return 0;
}
