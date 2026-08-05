#ifndef WARMC_OPCODES_H
#define WARMC_OPCODES_H

#include "reader.h"

typedef enum {
  IMM_NONE,
  IMM_BLOCKTYPE,
  IMM_LABEL,
  IMM_BRTABLE,
  IMM_FUNC,
  IMM_TYPE,
  IMM_TYPE_TABLE,

  IMM_TYPE_U32,
  IMM_LOCAL,
  IMM_GLOBAL,
  IMM_TABLE,
  IMM_MEMARG,
  IMM_MEMIDX,
  IMM_DATA,
  IMM_DATA_MEM,
  IMM_MEM_MEM,
  IMM_ELEM,
  IMM_ELEM_TABLE,
  IMM_TABLE_TABLE,
  IMM_I32,
  IMM_I64,
  IMM_F32,
  IMM_F64,
  IMM_V128,
  IMM_HEAPTYPE,
  IMM_VALTYPES,
  IMM_TAG,
  IMM_TRY_TABLE,

  IMM_BR_CAST,
  IMM_LANE,
  IMM_MEMARG_LANE,
  IMM_LANES16
} ImmKind;

typedef struct {
  uint32_t op;
  const char *name;
  uint8_t imm;

  uint8_t align;

  const char *sig;
} OpInfo;

#define OP_PREFIXED(prefix, sub) (((uint32_t)(prefix) << 16) | (uint32_t)(sub))

#define OP_SUB_UNKNOWN 0xffffu

uint32_t wasm_read_op(Rd *r);

const OpInfo *wasm_op_info(uint32_t op);
const char *wasm_op_name(uint32_t op);

bool wasm_op_by_name(const char *name, size_t n, uint32_t *op_out);

bool wasm_skip_imm(Rd *r, uint32_t op);

typedef void (*WasmTypeFn)(uint32_t type_index, void *ctx);
bool wasm_walk_imm(Rd *r, uint32_t op, WasmTypeFn on_type, void *ctx);

bool wasm_op_wants_gctype(uint32_t op);

int wasm_op_data_seg(uint32_t op);

#endif
