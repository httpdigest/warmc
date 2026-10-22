// memcpy, memmove, memset, memcmp, memchr, memrchr and memmem: every length
// from 0 to 70 at every source and destination misalignment within a word,
// memmove with forward and backward overlap by every distance, memset with
// values whose high bit is set, memcmp's sign on bytes above 0x7f, and struct
// assignment and array zeroing that clang lowers into these calls itself.
//
// With bulk memory enabled clang emits memory.copy and memory.fill for some
// of these and calls musl's word-at-a-time C versions for others; memmove's
// overlap direction and the head/tail handling of an unaligned copy are where
// such code goes wrong.  Each operation is checked against a byte-at-a-time
// reference and the result digest printed, along with the first mismatch.
#define _GNU_SOURCE  // memrchr, memmem
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char a[256], b[256], ref[256];

static void fill_pattern(unsigned char *p, int n, int seed) {
  for (int i = 0; i < n; i++) p[i] = (unsigned char)(i * 7 + seed * 13 + (i >> 3));
}
static uint32_t digest(const unsigned char *p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static int first_diff(const unsigned char *x, const unsigned char *y, int n) {
  for (int i = 0; i < n; i++)
    if (x[i] != y[i]) return i;
  return -1;
}

static void copies(void) {
  printf("-- memcpy at all alignments\n");
  int bad = 0;
  for (int so = 0; so < 8; so++) {
    uint32_t h = 0;
    for (int dof = 0; dof < 8; dof++) {
      for (int len = 0; len <= 70; len++) {
        fill_pattern(a, 256, so + len);
        memset(b, 0xee, 256);
        memset(ref, 0xee, 256);
        memcpy(b + 64 + dof, a + 16 + so, (size_t)len);
        for (int i = 0; i < len; i++) ref[64 + dof + i] = a[16 + so + i];
        int d = first_diff(b, ref, 256);
        if (d >= 0) {
          bad++;
          if (bad < 10) printf("mismatch so=%d do=%d len=%d at %d\n", so, dof, len, d);
        }
        h = h * 31 + digest(b, 256);
      }
    }
    printf("src off %d: digest %08x\n", so, h);
  }
  printf("memcpy mismatches: %d\n", bad);
}

static void moves(void) {
  printf("-- memmove overlaps\n");
  int bad = 0;
  for (int dist = -20; dist <= 20; dist++) {
    uint32_t h = 0;
    for (int len = 0; len <= 64; len += 1) {
      for (int base = 40; base < 44; base++) {
        fill_pattern(a, 256, len + dist);
        memcpy(ref, a, 256);
        // reference: copy through a temporary
        unsigned char tmp[128];
        for (int i = 0; i < len; i++) tmp[i] = ref[base + i];
        for (int i = 0; i < len; i++) ref[base + dist + i] = tmp[i];
        memmove(a + base + dist, a + base, (size_t)len);
        int d = first_diff(a, ref, 256);
        if (d >= 0) {
          bad++;
          if (bad < 10) printf("mismatch dist=%d len=%d base=%d at %d\n", dist, len, base, d);
        }
        h = h * 31 + digest(a, 160);
      }
    }
    printf("dist %3d: %08x\n", dist, h);
  }
  printf("memmove mismatches: %d\n", bad);
  // shift a string left and right in place
  char s[64] = "0123456789abcdefghij";
  memmove(s + 5, s, 21);
  printf("right: %s\n", s);
  memmove(s, s + 7, strlen(s + 7) + 1);
  printf("left:  %s\n", s);
}

static void sets(void) {
  printf("-- memset\n");
  int vals[] = {0, 1, 0x7f, 0x80, 0xff, 0x1234, -1, -128};
  for (int vi = 0; vi < 8; vi++) {
    uint32_t h = 0;
    for (int off = 0; off < 8; off++)
      for (int len = 0; len <= 70; len += 3) {
        memset(a, 0x55, 256);
        memset(a + 32 + off, vals[vi], (size_t)len);
        h = h * 31 + digest(a, 128);
      }
    printf("memset %d: %08x first byte %02x\n", vals[vi], h, a[32]);
  }
}

static void compares(void) {
  printf("-- memcmp memchr memrchr memmem\n");
  unsigned char x[64], y[64];
  for (int i = 0; i < 64; i++) x[i] = y[i] = (unsigned char)(i * 5);
  for (int pos = 0; pos < 64; pos += 7) {
    for (int delta = -2; delta <= 2; delta += 4) {
      memcpy(y, x, 64);
      y[pos] = (unsigned char)(y[pos] + delta);
      int r = memcmp(x, y, 64);
      int r2 = memcmp(x, y, (size_t)pos);
      int r3 = memcmp(x + pos, y + pos, 1);
      printf("pos %2d delta %+d: %d %d %d\n", pos, delta, (r > 0) - (r < 0), r2, (r3 > 0) - (r3 < 0));
    }
  }
  unsigned char hi[4] = {0x80, 0x00, 0xff, 0x7f}, lo[4] = {0x7f, 0x00, 0xff, 0x80};
  printf("high bytes unsigned: %d %d\n", memcmp(hi, lo, 4) > 0, memcmp(hi + 3, lo + 3, 1) < 0);
  const char *text = "find the needle in the haystack, needle!";
  size_t n = strlen(text);
  for (const char *c = "nedlz!"; *c; c++) {
    const char *f = memchr(text, *c, n);
    const char *l = memrchr(text, *c, n);
    printf("'%c' memchr=%td memrchr=%td\n", *c, f ? f - text : -1, l ? l - text : -1);
  }
  const char *pats[] = {"needle", "hay", "!", "find", "needle!", "neeedle", ""};
  for (int i = 0; i < 7; i++) {
    const char *f = memmem(text, n, pats[i], strlen(pats[i]));
    printf("memmem '%s' -> %td\n", pats[i], f ? f - text : -1);
  }
  // memchr limited by length
  printf("bounded memchr: %d %d\n", memchr(text, 'h', 5) != NULL, memchr(text, 'h', 7) != NULL);
}

typedef struct { int id; char name[20]; double vals[5]; short flags[3]; } Rec;

static void implicit(void) {
  printf("-- struct copies and zeroing\n");
  Rec r[6];
  memset(r, 0, sizeof r);
  for (int i = 0; i < 6; i++) {
    r[i].id = i * 11;
    snprintf(r[i].name, sizeof r[i].name, "record-%d", i);
    for (int k = 0; k < 5; k++) r[i].vals[k] = i + k / 4.0;
    r[i].flags[i % 3] = (short)(i + 100);
  }
  // rotate the array by struct assignment
  Rec t = r[0];
  for (int i = 0; i < 5; i++) r[i] = r[i + 1];
  r[5] = t;
  for (int i = 0; i < 6; i++)
    printf("%d %s %.2f %.2f %d %d %d\n", r[i].id, r[i].name, r[i].vals[0], r[i].vals[4],
           r[i].flags[0], r[i].flags[1], r[i].flags[2]);
  int big[300] = {0};
  big[299] = 5;
  int z = 0;
  for (int i = 0; i < 300; i++) z += big[i] == 0;
  printf("zeros=%d\n", z);
  char str[50] = "abc";  // remainder zero-filled
  int zeros = 0;
  for (int i = 0; i < 50; i++) zeros += str[i] == 0;
  printf("string zero tail=%d\n", zeros);
}

int main(void) {
  copies();
  moves();
  sets();
  compares();
  implicit();
  return 0;
}
