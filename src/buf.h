#ifndef WARMC_BUF_H
#define WARMC_BUF_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t *data;
  size_t len, cap;

  uint32_t flags_epoch;

  /* Instructions emitted so far.  x86-64 instructions are variable length,
     so --profile cannot recover a block's instruction count from its byte
     count the way the a64 backend does; the encoder keeps the tally as it
     goes.  a64 leaves this at zero. */
  uint32_t ninstr;
} Buf;

void buf_reserve(Buf *b, size_t extra);
void buf_u8(Buf *b, uint8_t v);
void buf_u16(Buf *b, uint16_t v);
void buf_u32(Buf *b, uint32_t v);
void buf_u64(Buf *b, uint64_t v);
void buf_bytes(Buf *b, const void *p, size_t n);
void buf_zero(Buf *b, size_t n);
void buf_align(Buf *b, size_t align);
void buf_free(Buf *b);

#endif
