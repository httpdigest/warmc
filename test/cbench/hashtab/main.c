// An open-addressing hash table keyed by strings: FNV-1a over the key, linear
// probing, and a memcmp to confirm each candidate -- the symbol table of every
// compiler, interpreter and configuration parser written in C.
//
// Unlike rsbench/mapint and gobench/maphash, the keys are byte strings, so the
// row mixes a hash loop, a probe that is a dependent load, and a short compare
// with an early exit.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

enum { CAP = 1 << 15, KEYS = 20000 };
typedef struct { char key[24]; uint32_t len, value; } Slot;
static Slot table[CAP];

static uint32_t fnv(const char *s, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
  return h;
}

static Slot *find(const char *k, uint32_t n, int insert) {
  uint32_t i = fnv(k, n) & (CAP - 1);
  for (;;) {
    Slot *s = &table[i];
    if (!s->len) {
      if (!insert) return 0;
      memcpy(s->key, k, n);
      s->len = n;
      return s;
    }
    if (s->len == n && !memcmp(s->key, k, n)) return s;
    i = (i + 1) & (CAP - 1);
  }
}

static uint32_t make_key(char *out, uint32_t i) {
  return (uint32_t)snprintf(out, 24, "k%u_%x", i * 2654435761u % 1000003, i);
}

int main(void) {
  char k[24];
  for (uint32_t i = 0; i < KEYS; i++) {
    uint32_t n = make_key(k, i);
    find(k, n, 1)->value = i;
  }
  uint64_t sum = 0;
  for (int r = 0; r < 60; r++)
    for (uint32_t i = 0; i < KEYS; i++) {
      uint32_t n = make_key(k, (i * 7 + (uint32_t)r) % (KEYS + 500));
      Slot *s = find(k, n, 0);
      sum += s ? s->value : 1;
    }
  printf("sum = %llu\n", (unsigned long long)sum);
  return 0;
}
