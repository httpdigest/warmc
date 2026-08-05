#ifndef WARMC_WAT_H
#define WARMC_WAT_H

#include "warmc.h"

uint8_t *wat_assemble(const char *src, size_t len, const char *path,
                      size_t *len_out);

bool wat_is_text(const uint8_t *bin, size_t len);

#endif
