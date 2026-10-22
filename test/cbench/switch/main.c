// Two switches on data: one whose forty case values are scattered over the
// whole 32-bit range, as in a switch on a keyword's hash or on a protocol's
// magic numbers, and one on HTTP status codes, dense runs with gaps between.
//
// clang lowers the scattered one to a binary search: four levels of signed
// compares, then up to three equality tests at a leaf, every branch on the
// data.  The clustered one becomes a single br_table spanning 200 to 511, most
// of its 312 entries the default, with a four-entry table for the 1xx codes
// behind its default.  interp's opcode switch is the dense case alone; this is
// the compare tree no other kernel here has, and a jump table that is mostly
// holes.
#include <stdio.h>
#include <stdint.h>

static uint32_t s = 1, t = 0;

// Forty case values with nothing in common, as in a switch on a hash of a
// keyword or on a protocol's magic numbers.
static const uint32_t keys[40] = {
  0x099950d8u, 0x0c5c7fd0u, 0x0cb1e29cu, 0x0ed90475u, 0x0f21ddb6u, 0x0fd630f1u, 0x11e20b8fu, 0x128b2f33u,
  0x1600a35au, 0x1738f7d9u, 0x1818e811u, 0x1fb17c23u, 0x269e0d37u, 0x36f675ccu, 0x39263059u, 0x3d9c1724u,
  0x52e6b438u, 0x5d9dc9f8u, 0x6513270eu, 0x658cda14u, 0x6b0d549bu, 0x6cad4a26u, 0x6f03675au, 0x81e74ef5u,
  0x892f902bu, 0x8d116eceu, 0x90c192cfu, 0x93bd04cfu, 0x9531985du, 0x953f48f1u, 0x95e60af5u, 0xa09f76b5u,
  0xa170b338u, 0xa6a3a450u, 0xd23f0824u, 0xd3ac94afu, 0xe8e25d94u, 0xf28c105du, 0xf29d0da9u, 0xf2a74de4u,
};

__attribute__((noinline)) static void sparse(uint32_t k) {
  switch (k) {
    case 0x099950d8u: s += 0xf9ebu; break;
    case 0x0c5c7fd0u: s ^= 0x3899u; break;
    case 0x0cb1e29cu: s *= 0x0bedu; break;
    case 0x0ed90475u: t += s >> 18; break;
    case 0x0f21ddb6u: s = s << 28 | s >> 4; break;
    case 0x0fd630f1u: s -= t & 0x2217u; break;
    case 0x11e20b8fu: t ^= s + 0x4a23u; break;
    case 0x128b2f33u: s = (s ^ t) + 14u; break;
    case 0x1600a35au: s += 0x24edu; break;
    case 0x1738f7d9u: s ^= 0x8a6bu; break;
    case 0x1818e811u: s *= 0x1e27u; break;
    case 0x1fb17c23u: t += s >> 19; break;
    case 0x269e0d37u: s = s << 10 | s >> 22; break;
    case 0x36f675ccu: s -= t & 0x8f6du; break;
    case 0x39263059u: t ^= s + 0xd0edu; break;
    case 0x3d9c1724u: s = (s ^ t) + 22u; break;
    case 0x52e6b438u: s += 0x2e45u; break;
    case 0x5d9dc9f8u: s ^= 0x1a61u; break;
    case 0x6513270eu: s *= 0x94e3u; break;
    case 0x658cda14u: t += s >> 9; break;
    case 0x6b0d549bu: s = s << 21 | s >> 11; break;
    case 0x6cad4a26u: s -= t & 0x3019u; break;
    case 0x6f03675au: t ^= s + 0x5f55u; break;
    case 0x81e74ef5u: s = (s ^ t) + 4u; break;
    case 0x892f902bu: s += 0x8c39u; break;
    case 0x8d116eceu: s ^= 0xb64du; break;
    case 0x90c192cfu: s *= 0x1013u; break;
    case 0x93bd04cfu: t += s >> 3; break;
    case 0x9531985du: s = s << 2 | s >> 30; break;
    case 0x953f48f1u: s -= t & 0x9e77u; break;
    case 0x95e60af5u: t ^= s + 0x34b9u; break;
    case 0xa09f76b5u: s = (s ^ t) + 16u; break;
    case 0xa170b338u: s += 0xae2fu; break;
    case 0xa6a3a450u: s ^= 0x881fu; break;
    case 0xd23f0824u: s *= 0x6d77u; break;
    case 0xd3ac94afu: t += s >> 25; break;
    case 0xe8e25d94u: s = s << 11 | s >> 21; break;
    case 0xf28c105du: s -= t & 0x7731u; break;
    case 0xf29d0da9u: t ^= s + 0x95e7u; break;
    case 0xf2a74de4u: s = (s ^ t) + 30u; break;
    default: t++; break;
  }
}

// Case values in a few dense runs with gaps between them: HTTP status codes.
static const uint16_t codes[] = {
  100, 101, 102, 103, 200, 201, 202, 203, 204, 205, 206, 226, 300, 301, 302, 303,
  304, 307, 308, 400, 401, 403, 404, 405, 408, 409, 410, 413, 414, 418, 429, 451,
  500, 501, 502, 503, 504, 505, 511, 999,
};

__attribute__((noinline)) static void clustered(uint32_t c) {
  switch (c) {
    case 100: case 102: s += 3; break;
    case 101: case 103: t ^= s; break;
    case 200: s = s * 5 + 1; break;
    case 201: case 202: t += s >> 4; break;
    case 203: s ^= t << 3; break;
    case 204: s -= 17; break;
    case 205: case 206: t = t * 9 + s; break;
    case 226: s = s >> 1 | s << 31; break;
    case 300: case 307: s += t; break;
    case 301: case 308: s ^= 0x5bd1e995u; break;
    case 302: t -= s & 255; break;
    case 303: case 304: s *= 0x2545u; break;
    case 400: t += 400; break;
    case 401: case 403: s = (s ^ t) * 3; break;
    case 404: t ^= s >> 7; break;
    case 405: case 408: s += s >> 5; break;
    case 409: case 410: t = t << 1 | t >> 31; break;
    case 413: case 414: s -= t; break;
    case 418: s ^= 418; break;
    case 429: t += s * 7; break;
    case 451: s = ~s; break;
    case 500: s += 0x9e3779b9u; break;
    case 501: case 502: t ^= t >> 3; break;
    case 503: s = s * 33 ^ t; break;
    case 504: case 505: t -= s >> 11; break;
    case 511: s ^= s << 13; break;
    default: t += 2; break;
  }
}

enum { M = 4096, ROUNDS = 2200 };
static uint32_t in_keys[M];
static uint16_t in_codes[M];

int main(void) {
  uint32_t x = 2463534242u;
  for (int i = 0; i < M; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    // One selector in eight misses every case.
    in_keys[i] = (x & 7) ? keys[(x >> 3) % 40] : x;
    in_codes[i] = codes[(x >> 9) % (sizeof codes / sizeof codes[0])];
  }
  uint64_t h = 0;
  for (int r = 0; r < ROUNDS; r++) {
    for (int i = 0; i < M; i++) {
      sparse(in_keys[i]);
      clustered(in_codes[i]);
    }
    h = h * 1000003u + (s ^ t);
    in_keys[r & (M - 1)] ^= s & 0x100u;
  }
  printf("s = %08x t = %08x h = %016llx\n", s, t, (unsigned long long)h);
  return 0;
}
