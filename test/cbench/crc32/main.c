// Table-driven CRC-32 over a buffer: a byte load, an xor, a table lookup and a
// shift per byte, all on one dependency chain.
//
// The row is bounded by load-to-use latency -- the table index depends on the
// previous CRC -- so it measures how tightly the loop's address arithmetic is
// folded into the loads, not throughput.  zlib, PNG and every archive format
// have this loop.
#include <stdio.h>
#include <stdint.h>

static uint32_t table[256];

static uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

int main(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  static uint8_t buf[65536];
  uint32_t x = 1;
  for (size_t i = 0; i < sizeof buf; i++) {
    x = x * 1664525u + 1013904223u;
    buf[i] = (uint8_t)(x >> 24);
  }
  uint32_t crc = 0;
  for (int r = 0; r < 400; r++) {
    crc = crc32(buf, sizeof buf, crc);
    buf[r] ^= (uint8_t)crc;
  }
  printf("crc = %08x\n", crc);
  return 0;
}
