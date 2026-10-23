#include "opcodes.h"

#include <pthread.h>
#include <stdatomic.h>

static const OpInfo OPCODES[] = {

  { 0x00, "unreachable", IMM_NONE, 0, NULL },
  { 0x01, "nop", IMM_NONE, 0, NULL },
  { 0x02, "block", IMM_BLOCKTYPE, 0, NULL },
  { 0x03, "loop", IMM_BLOCKTYPE, 0, NULL },
  { 0x04, "if", IMM_BLOCKTYPE, 0, NULL },
  { 0x05, "else", IMM_NONE, 0, NULL },

  { 0x08, "throw", IMM_TAG, 0, NULL },
  { 0x0a, "throw_ref", IMM_NONE, 0, NULL },
  { 0x1f, "try_table", IMM_TRY_TABLE, 0, NULL },
  { 0x0b, "end", IMM_NONE, 0, NULL },
  { 0x0c, "br", IMM_LABEL, 0, NULL },
  { 0x0d, "br_if", IMM_LABEL, 0, NULL },
  { 0x0e, "br_table", IMM_BRTABLE, 0, NULL },
  { 0x0f, "return", IMM_NONE, 0, NULL },
  { 0x10, "call", IMM_FUNC, 0, NULL },
  { 0x11, "call_indirect", IMM_TYPE_TABLE, 0, NULL },
  { 0x12, "return_call", IMM_FUNC, 0, NULL },
  { 0x13, "return_call_indirect", IMM_TYPE_TABLE, 0, NULL },
  { 0x14, "call_ref", IMM_TYPE, 0, NULL },
  { 0x15, "return_call_ref", IMM_TYPE, 0, NULL },

  { 0x1a, "drop", IMM_NONE, 0, NULL },
  { 0x1b, "select", IMM_NONE, 0, NULL },
  { 0x1c, "select t", IMM_VALTYPES, 0, NULL },

  { 0x20, "local.get", IMM_LOCAL, 0, NULL },
  { 0x21, "local.set", IMM_LOCAL, 0, NULL },
  { 0x22, "local.tee", IMM_LOCAL, 0, NULL },
  { 0x23, "global.get", IMM_GLOBAL, 0, NULL },
  { 0x24, "global.set", IMM_GLOBAL, 0, NULL },

  { 0x25, "table.get", IMM_TABLE, 0, NULL },
  { 0x26, "table.set", IMM_TABLE, 0, NULL },

  { 0x28, "i32.load", IMM_MEMARG, 2, "i:i" },
  { 0x29, "i64.load", IMM_MEMARG, 3, "i:l" },
  { 0x2a, "f32.load", IMM_MEMARG, 2, "i:f" },
  { 0x2b, "f64.load", IMM_MEMARG, 3, "i:d" },
  { 0x2c, "i32.load8_s", IMM_MEMARG, 0, "i:i" },
  { 0x2d, "i32.load8_u", IMM_MEMARG, 0, "i:i" },
  { 0x2e, "i32.load16_s", IMM_MEMARG, 1, "i:i" },
  { 0x2f, "i32.load16_u", IMM_MEMARG, 1, "i:i" },
  { 0x30, "i64.load8_s", IMM_MEMARG, 0, "i:l" },
  { 0x31, "i64.load8_u", IMM_MEMARG, 0, "i:l" },
  { 0x32, "i64.load16_s", IMM_MEMARG, 1, "i:l" },
  { 0x33, "i64.load16_u", IMM_MEMARG, 1, "i:l" },
  { 0x34, "i64.load32_s", IMM_MEMARG, 2, "i:l" },
  { 0x35, "i64.load32_u", IMM_MEMARG, 2, "i:l" },
  { 0x36, "i32.store", IMM_MEMARG, 2, "ii:" },
  { 0x37, "i64.store", IMM_MEMARG, 3, "il:" },
  { 0x38, "f32.store", IMM_MEMARG, 2, "if:" },
  { 0x39, "f64.store", IMM_MEMARG, 3, "id:" },
  { 0x3a, "i32.store8", IMM_MEMARG, 0, "ii:" },
  { 0x3b, "i32.store16", IMM_MEMARG, 1, "ii:" },
  { 0x3c, "i64.store8", IMM_MEMARG, 0, "il:" },
  { 0x3d, "i64.store16", IMM_MEMARG, 1, "il:" },
  { 0x3e, "i64.store32", IMM_MEMARG, 2, "il:" },
  { 0x3f, "memory.size", IMM_MEMIDX, 0, NULL },
  { 0x40, "memory.grow", IMM_MEMIDX, 0, NULL },

  { 0x41, "i32.const", IMM_I32, 0, NULL },
  { 0x42, "i64.const", IMM_I64, 0, NULL },
  { 0x43, "f32.const", IMM_F32, 0, NULL },
  { 0x44, "f64.const", IMM_F64, 0, NULL },

  { 0x45, "i32.eqz", IMM_NONE, 0, "i:i" },
  { 0x46, "i32.eq", IMM_NONE, 0, "ii:i" },
  { 0x47, "i32.ne", IMM_NONE, 0, "ii:i" },
  { 0x48, "i32.lt_s", IMM_NONE, 0, "ii:i" },
  { 0x49, "i32.lt_u", IMM_NONE, 0, "ii:i" },
  { 0x4a, "i32.gt_s", IMM_NONE, 0, "ii:i" },
  { 0x4b, "i32.gt_u", IMM_NONE, 0, "ii:i" },
  { 0x4c, "i32.le_s", IMM_NONE, 0, "ii:i" },
  { 0x4d, "i32.le_u", IMM_NONE, 0, "ii:i" },
  { 0x4e, "i32.ge_s", IMM_NONE, 0, "ii:i" },
  { 0x4f, "i32.ge_u", IMM_NONE, 0, "ii:i" },

  { 0x50, "i64.eqz", IMM_NONE, 0, "l:i" },
  { 0x51, "i64.eq", IMM_NONE, 0, "ll:i" },
  { 0x52, "i64.ne", IMM_NONE, 0, "ll:i" },
  { 0x53, "i64.lt_s", IMM_NONE, 0, "ll:i" },
  { 0x54, "i64.lt_u", IMM_NONE, 0, "ll:i" },
  { 0x55, "i64.gt_s", IMM_NONE, 0, "ll:i" },
  { 0x56, "i64.gt_u", IMM_NONE, 0, "ll:i" },
  { 0x57, "i64.le_s", IMM_NONE, 0, "ll:i" },
  { 0x58, "i64.le_u", IMM_NONE, 0, "ll:i" },
  { 0x59, "i64.ge_s", IMM_NONE, 0, "ll:i" },
  { 0x5a, "i64.ge_u", IMM_NONE, 0, "ll:i" },

  { 0x5b, "f32.eq", IMM_NONE, 0, "ff:i" },
  { 0x5c, "f32.ne", IMM_NONE, 0, "ff:i" },
  { 0x5d, "f32.lt", IMM_NONE, 0, "ff:i" },
  { 0x5e, "f32.gt", IMM_NONE, 0, "ff:i" },
  { 0x5f, "f32.le", IMM_NONE, 0, "ff:i" },
  { 0x60, "f32.ge", IMM_NONE, 0, "ff:i" },

  { 0x61, "f64.eq", IMM_NONE, 0, "dd:i" },
  { 0x62, "f64.ne", IMM_NONE, 0, "dd:i" },
  { 0x63, "f64.lt", IMM_NONE, 0, "dd:i" },
  { 0x64, "f64.gt", IMM_NONE, 0, "dd:i" },
  { 0x65, "f64.le", IMM_NONE, 0, "dd:i" },
  { 0x66, "f64.ge", IMM_NONE, 0, "dd:i" },

  { 0x67, "i32.clz", IMM_NONE, 0, "i:i" },
  { 0x68, "i32.ctz", IMM_NONE, 0, "i:i" },
  { 0x69, "i32.popcnt", IMM_NONE, 0, "i:i" },
  { 0x6a, "i32.add", IMM_NONE, 0, "ii:i" },
  { 0x6b, "i32.sub", IMM_NONE, 0, "ii:i" },
  { 0x6c, "i32.mul", IMM_NONE, 0, "ii:i" },
  { 0x6d, "i32.div_s", IMM_NONE, 0, "ii:i" },
  { 0x6e, "i32.div_u", IMM_NONE, 0, "ii:i" },
  { 0x6f, "i32.rem_s", IMM_NONE, 0, "ii:i" },
  { 0x70, "i32.rem_u", IMM_NONE, 0, "ii:i" },
  { 0x71, "i32.and", IMM_NONE, 0, "ii:i" },
  { 0x72, "i32.or", IMM_NONE, 0, "ii:i" },
  { 0x73, "i32.xor", IMM_NONE, 0, "ii:i" },
  { 0x74, "i32.shl", IMM_NONE, 0, "ii:i" },
  { 0x75, "i32.shr_s", IMM_NONE, 0, "ii:i" },
  { 0x76, "i32.shr_u", IMM_NONE, 0, "ii:i" },
  { 0x77, "i32.rotl", IMM_NONE, 0, "ii:i" },
  { 0x78, "i32.rotr", IMM_NONE, 0, "ii:i" },

  { 0x79, "i64.clz", IMM_NONE, 0, "l:l" },
  { 0x7a, "i64.ctz", IMM_NONE, 0, "l:l" },
  { 0x7b, "i64.popcnt", IMM_NONE, 0, "l:l" },
  { 0x7c, "i64.add", IMM_NONE, 0, "ll:l" },
  { 0x7d, "i64.sub", IMM_NONE, 0, "ll:l" },
  { 0x7e, "i64.mul", IMM_NONE, 0, "ll:l" },
  { 0x7f, "i64.div_s", IMM_NONE, 0, "ll:l" },
  { 0x80, "i64.div_u", IMM_NONE, 0, "ll:l" },
  { 0x81, "i64.rem_s", IMM_NONE, 0, "ll:l" },
  { 0x82, "i64.rem_u", IMM_NONE, 0, "ll:l" },
  { 0x83, "i64.and", IMM_NONE, 0, "ll:l" },
  { 0x84, "i64.or", IMM_NONE, 0, "ll:l" },
  { 0x85, "i64.xor", IMM_NONE, 0, "ll:l" },
  { 0x86, "i64.shl", IMM_NONE, 0, "ll:l" },
  { 0x87, "i64.shr_s", IMM_NONE, 0, "ll:l" },
  { 0x88, "i64.shr_u", IMM_NONE, 0, "ll:l" },
  { 0x89, "i64.rotl", IMM_NONE, 0, "ll:l" },
  { 0x8a, "i64.rotr", IMM_NONE, 0, "ll:l" },

  { 0x8b, "f32.abs", IMM_NONE, 0, "f:f" },
  { 0x8c, "f32.neg", IMM_NONE, 0, "f:f" },
  { 0x8d, "f32.ceil", IMM_NONE, 0, "f:f" },
  { 0x8e, "f32.floor", IMM_NONE, 0, "f:f" },
  { 0x8f, "f32.trunc", IMM_NONE, 0, "f:f" },
  { 0x90, "f32.nearest", IMM_NONE, 0, "f:f" },
  { 0x91, "f32.sqrt", IMM_NONE, 0, "f:f" },
  { 0x92, "f32.add", IMM_NONE, 0, "ff:f" },
  { 0x93, "f32.sub", IMM_NONE, 0, "ff:f" },
  { 0x94, "f32.mul", IMM_NONE, 0, "ff:f" },
  { 0x95, "f32.div", IMM_NONE, 0, "ff:f" },
  { 0x96, "f32.min", IMM_NONE, 0, "ff:f" },
  { 0x97, "f32.max", IMM_NONE, 0, "ff:f" },
  { 0x98, "f32.copysign", IMM_NONE, 0, "ff:f" },

  { 0x99, "f64.abs", IMM_NONE, 0, "d:d" },
  { 0x9a, "f64.neg", IMM_NONE, 0, "d:d" },
  { 0x9b, "f64.ceil", IMM_NONE, 0, "d:d" },
  { 0x9c, "f64.floor", IMM_NONE, 0, "d:d" },
  { 0x9d, "f64.trunc", IMM_NONE, 0, "d:d" },
  { 0x9e, "f64.nearest", IMM_NONE, 0, "d:d" },
  { 0x9f, "f64.sqrt", IMM_NONE, 0, "d:d" },
  { 0xa0, "f64.add", IMM_NONE, 0, "dd:d" },
  { 0xa1, "f64.sub", IMM_NONE, 0, "dd:d" },
  { 0xa2, "f64.mul", IMM_NONE, 0, "dd:d" },
  { 0xa3, "f64.div", IMM_NONE, 0, "dd:d" },
  { 0xa4, "f64.min", IMM_NONE, 0, "dd:d" },
  { 0xa5, "f64.max", IMM_NONE, 0, "dd:d" },
  { 0xa6, "f64.copysign", IMM_NONE, 0, "dd:d" },

  { 0xa7, "i32.wrap_i64", IMM_NONE, 0, "l:i" },
  { 0xa8, "i32.trunc_f32_s", IMM_NONE, 0, "f:i" },
  { 0xa9, "i32.trunc_f32_u", IMM_NONE, 0, "f:i" },
  { 0xaa, "i32.trunc_f64_s", IMM_NONE, 0, "d:i" },
  { 0xab, "i32.trunc_f64_u", IMM_NONE, 0, "d:i" },
  { 0xac, "i64.extend_i32_s", IMM_NONE, 0, "i:l" },
  { 0xad, "i64.extend_i32_u", IMM_NONE, 0, "i:l" },
  { 0xae, "i64.trunc_f32_s", IMM_NONE, 0, "f:l" },
  { 0xaf, "i64.trunc_f32_u", IMM_NONE, 0, "f:l" },
  { 0xb0, "i64.trunc_f64_s", IMM_NONE, 0, "d:l" },
  { 0xb1, "i64.trunc_f64_u", IMM_NONE, 0, "d:l" },
  { 0xb2, "f32.convert_i32_s", IMM_NONE, 0, "i:f" },
  { 0xb3, "f32.convert_i32_u", IMM_NONE, 0, "i:f" },
  { 0xb4, "f32.convert_i64_s", IMM_NONE, 0, "l:f" },
  { 0xb5, "f32.convert_i64_u", IMM_NONE, 0, "l:f" },
  { 0xb6, "f32.demote_f64", IMM_NONE, 0, "d:f" },
  { 0xb7, "f64.convert_i32_s", IMM_NONE, 0, "i:d" },
  { 0xb8, "f64.convert_i32_u", IMM_NONE, 0, "i:d" },
  { 0xb9, "f64.convert_i64_s", IMM_NONE, 0, "l:d" },
  { 0xba, "f64.convert_i64_u", IMM_NONE, 0, "l:d" },
  { 0xbb, "f64.promote_f32", IMM_NONE, 0, "f:d" },
  { 0xbc, "i32.reinterpret_f32", IMM_NONE, 0, "f:i" },
  { 0xbd, "i64.reinterpret_f64", IMM_NONE, 0, "d:l" },
  { 0xbe, "f32.reinterpret_i32", IMM_NONE, 0, "i:f" },
  { 0xbf, "f64.reinterpret_i64", IMM_NONE, 0, "l:d" },

  { 0xc0, "i32.extend8_s", IMM_NONE, 0, "i:i" },
  { 0xc1, "i32.extend16_s", IMM_NONE, 0, "i:i" },
  { 0xc2, "i64.extend8_s", IMM_NONE, 0, "l:l" },
  { 0xc3, "i64.extend16_s", IMM_NONE, 0, "l:l" },
  { 0xc4, "i64.extend32_s", IMM_NONE, 0, "l:l" },

  { 0xd0, "ref.null", IMM_HEAPTYPE, 0, NULL },
  { 0xd1, "ref.is_null", IMM_NONE, 0, NULL },
  { 0xd2, "ref.func", IMM_FUNC, 0, NULL },
  { 0xd3, "ref.eq", IMM_NONE, 0, NULL },
  { 0xd4, "ref.as_non_null", IMM_NONE, 0, NULL },
  { 0xd5, "br_on_null", IMM_LABEL, 0, NULL },
  { 0xd6, "br_on_non_null", IMM_LABEL, 0, NULL },

  { OP_PREFIXED(0xfc, 0), "i32.trunc_sat_f32_s", IMM_NONE, 0, "f:i" },
  { OP_PREFIXED(0xfc, 1), "i32.trunc_sat_f32_u", IMM_NONE, 0, "f:i" },
  { OP_PREFIXED(0xfc, 2), "i32.trunc_sat_f64_s", IMM_NONE, 0, "d:i" },
  { OP_PREFIXED(0xfc, 3), "i32.trunc_sat_f64_u", IMM_NONE, 0, "d:i" },
  { OP_PREFIXED(0xfc, 4), "i64.trunc_sat_f32_s", IMM_NONE, 0, "f:l" },
  { OP_PREFIXED(0xfc, 5), "i64.trunc_sat_f32_u", IMM_NONE, 0, "f:l" },
  { OP_PREFIXED(0xfc, 6), "i64.trunc_sat_f64_s", IMM_NONE, 0, "d:l" },
  { OP_PREFIXED(0xfc, 7), "i64.trunc_sat_f64_u", IMM_NONE, 0, "d:l" },
  { OP_PREFIXED(0xfc, 8), "memory.init", IMM_DATA_MEM, 0, NULL },
  { OP_PREFIXED(0xfc, 9), "data.drop", IMM_DATA, 0, NULL },
  { OP_PREFIXED(0xfc, 10), "memory.copy", IMM_MEM_MEM, 0, NULL },
  { OP_PREFIXED(0xfc, 11), "memory.fill", IMM_MEMIDX, 0, NULL },
  { OP_PREFIXED(0xfc, 12), "table.init", IMM_ELEM_TABLE, 0, NULL },
  { OP_PREFIXED(0xfc, 13), "elem.drop", IMM_ELEM, 0, NULL },
  { OP_PREFIXED(0xfc, 14), "table.copy", IMM_TABLE_TABLE, 0, NULL },
  { OP_PREFIXED(0xfc, 15), "table.grow", IMM_TABLE, 0, NULL },
  { OP_PREFIXED(0xfc, 16), "table.size", IMM_TABLE, 0, NULL },
  { OP_PREFIXED(0xfc, 17), "table.fill", IMM_TABLE, 0, NULL },

  { OP_PREFIXED(0xfd, 0), "v128.load", IMM_MEMARG, 4, "i:v" },
  { OP_PREFIXED(0xfd, 1), "v128.load8x8_s", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 2), "v128.load8x8_u", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 3), "v128.load16x4_s", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 4), "v128.load16x4_u", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 5), "v128.load32x2_s", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 6), "v128.load32x2_u", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 7), "v128.load8_splat", IMM_MEMARG, 0, "i:v" },
  { OP_PREFIXED(0xfd, 8), "v128.load16_splat", IMM_MEMARG, 1, "i:v" },
  { OP_PREFIXED(0xfd, 9), "v128.load32_splat", IMM_MEMARG, 2, "i:v" },
  { OP_PREFIXED(0xfd, 10), "v128.load64_splat", IMM_MEMARG, 3, "i:v" },
  { OP_PREFIXED(0xfd, 11), "v128.store", IMM_MEMARG, 4, "iv:" },
  { OP_PREFIXED(0xfd, 12), "v128.const", IMM_V128, 0, ":v" },
  { OP_PREFIXED(0xfd, 13), "i8x16.shuffle", IMM_LANES16, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 14), "i8x16.swizzle", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 15), "i8x16.splat", IMM_NONE, 0, "i:v" },
  { OP_PREFIXED(0xfd, 16), "i16x8.splat", IMM_NONE, 0, "i:v" },
  { OP_PREFIXED(0xfd, 17), "i32x4.splat", IMM_NONE, 0, "i:v" },
  { OP_PREFIXED(0xfd, 18), "i64x2.splat", IMM_NONE, 0, "l:v" },
  { OP_PREFIXED(0xfd, 19), "f32x4.splat", IMM_NONE, 0, "f:v" },
  { OP_PREFIXED(0xfd, 20), "f64x2.splat", IMM_NONE, 0, "d:v" },

  { OP_PREFIXED(0xfd, 21), "i8x16.extract_lane_s", IMM_LANE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 22), "i8x16.extract_lane_u", IMM_LANE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 23), "i8x16.replace_lane", IMM_LANE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 24), "i16x8.extract_lane_s", IMM_LANE, 1, "v:i" },
  { OP_PREFIXED(0xfd, 25), "i16x8.extract_lane_u", IMM_LANE, 1, "v:i" },
  { OP_PREFIXED(0xfd, 26), "i16x8.replace_lane", IMM_LANE, 1, "vi:v" },
  { OP_PREFIXED(0xfd, 27), "i32x4.extract_lane", IMM_LANE, 2, "v:i" },
  { OP_PREFIXED(0xfd, 28), "i32x4.replace_lane", IMM_LANE, 2, "vi:v" },
  { OP_PREFIXED(0xfd, 29), "i64x2.extract_lane", IMM_LANE, 3, "v:l" },
  { OP_PREFIXED(0xfd, 30), "i64x2.replace_lane", IMM_LANE, 3, "vl:v" },
  { OP_PREFIXED(0xfd, 31), "f32x4.extract_lane", IMM_LANE, 2, "v:f" },
  { OP_PREFIXED(0xfd, 32), "f32x4.replace_lane", IMM_LANE, 2, "vf:v" },
  { OP_PREFIXED(0xfd, 33), "f64x2.extract_lane", IMM_LANE, 3, "v:d" },
  { OP_PREFIXED(0xfd, 34), "f64x2.replace_lane", IMM_LANE, 3, "vd:v" },

  { OP_PREFIXED(0xfd, 35), "i8x16.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 36), "i8x16.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 37), "i8x16.lt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 38), "i8x16.lt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 39), "i8x16.gt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 40), "i8x16.gt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 41), "i8x16.le_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 42), "i8x16.le_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 43), "i8x16.ge_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 44), "i8x16.ge_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 45), "i16x8.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 46), "i16x8.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 47), "i16x8.lt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 48), "i16x8.lt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 49), "i16x8.gt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 50), "i16x8.gt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 51), "i16x8.le_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 52), "i16x8.le_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 53), "i16x8.ge_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 54), "i16x8.ge_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 55), "i32x4.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 56), "i32x4.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 57), "i32x4.lt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 58), "i32x4.lt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 59), "i32x4.gt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 60), "i32x4.gt_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 61), "i32x4.le_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 62), "i32x4.le_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 63), "i32x4.ge_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 64), "i32x4.ge_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 65), "f32x4.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 66), "f32x4.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 67), "f32x4.lt", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 68), "f32x4.gt", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 69), "f32x4.le", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 70), "f32x4.ge", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 71), "f64x2.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 72), "f64x2.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 73), "f64x2.lt", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 74), "f64x2.gt", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 75), "f64x2.le", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 76), "f64x2.ge", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 77), "v128.not", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 78), "v128.and", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 79), "v128.andnot", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 80), "v128.or", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 81), "v128.xor", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 82), "v128.bitselect", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 83), "v128.any_true", IMM_NONE, 0, "v:i" },

  { OP_PREFIXED(0xfd, 84), "v128.load8_lane", IMM_MEMARG_LANE, 0, "iv:v" },
  { OP_PREFIXED(0xfd, 85), "v128.load16_lane", IMM_MEMARG_LANE, 1, "iv:v" },
  { OP_PREFIXED(0xfd, 86), "v128.load32_lane", IMM_MEMARG_LANE, 2, "iv:v" },
  { OP_PREFIXED(0xfd, 87), "v128.load64_lane", IMM_MEMARG_LANE, 3, "iv:v" },
  { OP_PREFIXED(0xfd, 88), "v128.store8_lane", IMM_MEMARG_LANE, 0, "iv:" },
  { OP_PREFIXED(0xfd, 89), "v128.store16_lane", IMM_MEMARG_LANE, 1, "iv:" },
  { OP_PREFIXED(0xfd, 90), "v128.store32_lane", IMM_MEMARG_LANE, 2, "iv:" },
  { OP_PREFIXED(0xfd, 91), "v128.store64_lane", IMM_MEMARG_LANE, 3, "iv:" },
  { OP_PREFIXED(0xfd, 92), "v128.load32_zero", IMM_MEMARG, 2, "i:v" },
  { OP_PREFIXED(0xfd, 93), "v128.load64_zero", IMM_MEMARG, 3, "i:v" },

  { OP_PREFIXED(0xfd, 94), "f32x4.demote_f64x2_zero", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 95), "f64x2.promote_low_f32x4", IMM_NONE, 0, "v:v" },

  { OP_PREFIXED(0xfd, 96), "i8x16.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 97), "i8x16.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 98), "i8x16.popcnt", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 99), "i8x16.all_true", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 100), "i8x16.bitmask", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 101), "i8x16.narrow_i16x8_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 102), "i8x16.narrow_i16x8_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 103), "f32x4.ceil", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 104), "f32x4.floor", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 105), "f32x4.trunc", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 106), "f32x4.nearest", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 107), "i8x16.shl", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 108), "i8x16.shr_s", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 109), "i8x16.shr_u", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 110), "i8x16.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 111), "i8x16.add_sat_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 112), "i8x16.add_sat_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 113), "i8x16.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 114), "i8x16.sub_sat_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 115), "i8x16.sub_sat_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 116), "f64x2.ceil", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 117), "f64x2.floor", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 118), "i8x16.min_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 119), "i8x16.min_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 120), "i8x16.max_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 121), "i8x16.max_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 122), "f64x2.trunc", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 123), "i8x16.avgr_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 124), "i16x8.extadd_pairwise_i8x16_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 125), "i16x8.extadd_pairwise_i8x16_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 126), "i32x4.extadd_pairwise_i16x8_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 127), "i32x4.extadd_pairwise_i16x8_u", IMM_NONE, 0, "v:v" },

  { OP_PREFIXED(0xfd, 128), "i16x8.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 129), "i16x8.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 130), "i16x8.q15mulr_sat_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 131), "i16x8.all_true", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 132), "i16x8.bitmask", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 133), "i16x8.narrow_i32x4_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 134), "i16x8.narrow_i32x4_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 135), "i16x8.extend_low_i8x16_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 136), "i16x8.extend_high_i8x16_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 137), "i16x8.extend_low_i8x16_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 138), "i16x8.extend_high_i8x16_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 139), "i16x8.shl", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 140), "i16x8.shr_s", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 141), "i16x8.shr_u", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 142), "i16x8.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 143), "i16x8.add_sat_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 144), "i16x8.add_sat_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 145), "i16x8.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 146), "i16x8.sub_sat_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 147), "i16x8.sub_sat_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 148), "f64x2.nearest", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 149), "i16x8.mul", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 150), "i16x8.min_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 151), "i16x8.min_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 152), "i16x8.max_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 153), "i16x8.max_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 155), "i16x8.avgr_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 156), "i16x8.extmul_low_i8x16_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 157), "i16x8.extmul_high_i8x16_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 158), "i16x8.extmul_low_i8x16_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 159), "i16x8.extmul_high_i8x16_u", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 160), "i32x4.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 161), "i32x4.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 163), "i32x4.all_true", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 164), "i32x4.bitmask", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 167), "i32x4.extend_low_i16x8_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 168), "i32x4.extend_high_i16x8_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 169), "i32x4.extend_low_i16x8_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 170), "i32x4.extend_high_i16x8_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 171), "i32x4.shl", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 172), "i32x4.shr_s", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 173), "i32x4.shr_u", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 174), "i32x4.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 177), "i32x4.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 181), "i32x4.mul", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 182), "i32x4.min_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 183), "i32x4.min_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 184), "i32x4.max_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 185), "i32x4.max_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 186), "i32x4.dot_i16x8_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 188), "i32x4.extmul_low_i16x8_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 189), "i32x4.extmul_high_i16x8_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 190), "i32x4.extmul_low_i16x8_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 191), "i32x4.extmul_high_i16x8_u", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 192), "i64x2.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 193), "i64x2.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 195), "i64x2.all_true", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 196), "i64x2.bitmask", IMM_NONE, 0, "v:i" },
  { OP_PREFIXED(0xfd, 199), "i64x2.extend_low_i32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 200), "i64x2.extend_high_i32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 201), "i64x2.extend_low_i32x4_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 202), "i64x2.extend_high_i32x4_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 203), "i64x2.shl", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 204), "i64x2.shr_s", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 205), "i64x2.shr_u", IMM_NONE, 0, "vi:v" },
  { OP_PREFIXED(0xfd, 206), "i64x2.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 209), "i64x2.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 213), "i64x2.mul", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 214), "i64x2.eq", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 215), "i64x2.ne", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 216), "i64x2.lt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 217), "i64x2.gt_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 218), "i64x2.le_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 219), "i64x2.ge_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 220), "i64x2.extmul_low_i32x4_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 221), "i64x2.extmul_high_i32x4_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 222), "i64x2.extmul_low_i32x4_u", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 223), "i64x2.extmul_high_i32x4_u", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 224), "f32x4.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 225), "f32x4.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 227), "f32x4.sqrt", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 228), "f32x4.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 229), "f32x4.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 230), "f32x4.mul", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 231), "f32x4.div", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 232), "f32x4.min", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 233), "f32x4.max", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 234), "f32x4.pmin", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 235), "f32x4.pmax", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 236), "f64x2.abs", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 237), "f64x2.neg", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 239), "f64x2.sqrt", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 240), "f64x2.add", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 241), "f64x2.sub", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 242), "f64x2.mul", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 243), "f64x2.div", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 244), "f64x2.min", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 245), "f64x2.max", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 246), "f64x2.pmin", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 247), "f64x2.pmax", IMM_NONE, 0, "vv:v" },

  { OP_PREFIXED(0xfd, 248), "i32x4.trunc_sat_f32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 249), "i32x4.trunc_sat_f32x4_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 250), "f32x4.convert_i32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 251), "f32x4.convert_i32x4_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 252), "i32x4.trunc_sat_f64x2_s_zero", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 253), "i32x4.trunc_sat_f64x2_u_zero", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 254), "f64x2.convert_low_i32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 255), "f64x2.convert_low_i32x4_u", IMM_NONE, 0, "v:v" },

  { OP_PREFIXED(0xfd, 256), "i8x16.relaxed_swizzle", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 257), "i32x4.relaxed_trunc_f32x4_s", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 258), "i32x4.relaxed_trunc_f32x4_u", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 259), "i32x4.relaxed_trunc_f64x2_s_zero", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 260), "i32x4.relaxed_trunc_f64x2_u_zero", IMM_NONE, 0, "v:v" },
  { OP_PREFIXED(0xfd, 261), "f32x4.relaxed_madd", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 262), "f32x4.relaxed_nmadd", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 263), "f64x2.relaxed_madd", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 264), "f64x2.relaxed_nmadd", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 265), "i8x16.relaxed_laneselect", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 266), "i16x8.relaxed_laneselect", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 267), "i32x4.relaxed_laneselect", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 268), "i64x2.relaxed_laneselect", IMM_NONE, 0, "vvv:v" },
  { OP_PREFIXED(0xfd, 269), "f32x4.relaxed_min", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 270), "f32x4.relaxed_max", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 271), "f64x2.relaxed_min", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 272), "f64x2.relaxed_max", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 273), "i16x8.relaxed_q15mulr_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 274), "i16x8.relaxed_dot_i8x16_i7x16_s", IMM_NONE, 0, "vv:v" },
  { OP_PREFIXED(0xfd, 275), "i32x4.relaxed_dot_i8x16_i7x16_add_s", IMM_NONE, 0, "vvv:v" },

  { OP_PREFIXED(0xfb, 0), "struct.new", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 1), "struct.new_default", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 2), "struct.get", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 3), "struct.get_s", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 4), "struct.get_u", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 5), "struct.set", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 6), "array.new", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 7), "array.new_default", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 8), "array.new_fixed", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 9), "array.new_data", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 10), "array.new_elem", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 11), "array.get", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 12), "array.get_s", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 13), "array.get_u", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 14), "array.set", IMM_TYPE, 0, NULL },

  { OP_PREFIXED(0xfb, 15), "array.len", IMM_NONE, 0, NULL },
  { OP_PREFIXED(0xfb, 16), "array.fill", IMM_TYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 17), "array.copy", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 18), "array.init_data", IMM_TYPE_U32, 0, NULL },
  { OP_PREFIXED(0xfb, 19), "array.init_elem", IMM_TYPE_U32, 0, NULL },

  { OP_PREFIXED(0xfb, 20), "ref.test", IMM_HEAPTYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 21), "ref.test null", IMM_HEAPTYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 22), "ref.cast", IMM_HEAPTYPE, 0, NULL },
  { OP_PREFIXED(0xfb, 23), "ref.cast null", IMM_HEAPTYPE, 0, NULL },

  { OP_PREFIXED(0xfb, 24), "br_on_cast", IMM_BR_CAST, 0, NULL },
  { OP_PREFIXED(0xfb, 25), "br_on_cast_fail", IMM_BR_CAST, 0, NULL },
  { OP_PREFIXED(0xfb, 26), "any.convert_extern", IMM_NONE, 0, NULL },
  { OP_PREFIXED(0xfb, 27), "extern.convert_any", IMM_NONE, 0, NULL },
  { OP_PREFIXED(0xfb, 28), "ref.i31", IMM_NONE, 0, NULL },
  { OP_PREFIXED(0xfb, 29), "i31.get_s", IMM_NONE, 0, NULL },
  { OP_PREFIXED(0xfb, 30), "i31.get_u", IMM_NONE, 0, NULL },
};

#define NOPCODES ((uint32_t)(sizeof OPCODES / sizeof OPCODES[0]))

/* The two lookup tables are built on first use, which may be on several
   threads at once now that bodies are validated in parallel: two building
   one table together filled it with duplicates until a probe never ended.
   Built once, then, and read behind one acquire load. */
enum { OP_NSLOT = 2048 };
static int16_t g_op_slot[OP_NSLOT];
static _Atomic bool g_op_built;
static pthread_once_t g_op_once = PTHREAD_ONCE_INIT;

static void op_build(void) {
  for (uint32_t i = 0; i < OP_NSLOT; i++) g_op_slot[i] = -1;
  for (uint32_t i = 0; i < NOPCODES; i++) {

    if (OPCODES[i].op > 0xff && (OPCODES[i].op & 0xffffu) == OP_SUB_UNKNOWN)
      warmc_fatal("internal: opcode table uses the reserved sub-opcode %u",
                  OP_SUB_UNKNOWN);
    uint32_t h = (OPCODES[i].op * 2654435761u) & (OP_NSLOT - 1);
    while (g_op_slot[h] >= 0) h = (h + 1) & (OP_NSLOT - 1);
    g_op_slot[h] = (int16_t)i;
  }
  atomic_store_explicit(&g_op_built, true, memory_order_release);
}

const OpInfo *wasm_op_info(uint32_t op) {
  if (!atomic_load_explicit(&g_op_built, memory_order_acquire))
    pthread_once(&g_op_once, op_build);
  uint32_t h = (op * 2654435761u) & (OP_NSLOT - 1);
  while (g_op_slot[h] >= 0) {
    const OpInfo *oi = &OPCODES[g_op_slot[h]];
    if (oi->op == op) return oi;
    h = (h + 1) & (OP_NSLOT - 1);
  }
  return NULL;
}

const char *wasm_op_name(uint32_t op) {
  const OpInfo *oi = wasm_op_info(op);
  return oi ? oi->name : "?";
}

static int16_t g_name_slot[OP_NSLOT];
static _Atomic bool g_name_built;
static pthread_once_t g_name_once = PTHREAD_ONCE_INIT;

static void name_build(void) {
  for (uint32_t i = 0; i < OP_NSLOT; i++) g_name_slot[i] = -1;
  for (uint32_t i = 0; i < NOPCODES; i++) {
    if (strchr(OPCODES[i].name, ' ')) continue;
    size_t len = strlen(OPCODES[i].name);
    uint32_t h = 2166136261u;
    for (size_t j = 0; j < len; j++) {
      h ^= (uint8_t)OPCODES[i].name[j];
      h *= 16777619u;
    }
    h &= OP_NSLOT - 1;
    while (g_name_slot[h] >= 0) {

      if (!strcmp(OPCODES[g_name_slot[h]].name, OPCODES[i].name))
        warmc_fatal("internal: opcode table spells '%s' twice",
                    OPCODES[i].name);
      h = (h + 1) & (OP_NSLOT - 1);
    }
    g_name_slot[h] = (int16_t)i;
  }
  atomic_store_explicit(&g_name_built, true, memory_order_release);
}

bool wasm_op_by_name(const char *name, size_t n, uint32_t *op_out) {
  if (!atomic_load_explicit(&g_name_built, memory_order_acquire))
    pthread_once(&g_name_once, name_build);
  uint32_t h = 2166136261u;
  for (size_t j = 0; j < n; j++) { h ^= (uint8_t)name[j]; h *= 16777619u; }
  h &= OP_NSLOT - 1;
  while (g_name_slot[h] >= 0) {
    const OpInfo *oi = &OPCODES[g_name_slot[h]];
    if (!strncmp(oi->name, name, n) && oi->name[n] == 0) {
      *op_out = oi->op;
      return true;
    }
    h = (h + 1) & (OP_NSLOT - 1);
  }
  return false;
}

static void op_skip_valtype(Rd *r) { (void)rd_valtype(r); }

uint32_t wasm_read_op(Rd *r) {
  uint8_t b = rd_u8(r);
  if (b == 0xfb || b == 0xfc || b == 0xfd || b == 0xfe) {
    uint32_t sub = rd_u32(r);

    if (sub > 0xffffu) sub = OP_SUB_UNKNOWN;
    return OP_PREFIXED(b, sub);
  }
  return b;
}

bool wasm_skip_imm(Rd *r, uint32_t op) {
  return wasm_walk_imm(r, op, NULL, NULL);
}

int wasm_op_data_seg(uint32_t op) {
  switch (op) {
    case OP_PREFIXED(0xfc, 8): return 0;
    case OP_PREFIXED(0xfb, 9):
    case OP_PREFIXED(0xfb, 18): return 1;
    default: return -1;
  }
}

bool wasm_op_wants_gctype(uint32_t op) {
  switch (op) {
    case OP_PREFIXED(0xfb, 0):
    case OP_PREFIXED(0xfb, 1):
    case OP_PREFIXED(0xfb, 6):
    case OP_PREFIXED(0xfb, 7):
    case OP_PREFIXED(0xfb, 8):
    case OP_PREFIXED(0xfb, 9):
    case OP_PREFIXED(0xfb, 10):
    case OP_PREFIXED(0xfb, 16):
    case OP_PREFIXED(0xfb, 17):
    case OP_PREFIXED(0xfb, 18):
    case OP_PREFIXED(0xfb, 19):
      return true;
    default: return false;
  }
}

#define SAY_BT(bt) do { \
  if (on_type && !(bt).empty && !(bt).single) on_type((bt).typeidx, ctx); \
} while (0)
#define SAY_HT(h) do { \
  HeapType h_ = (h); if (on_type && h_ >= 0) on_type((uint32_t)h_, ctx); \
} while (0)

bool wasm_walk_imm(Rd *r, uint32_t op, WasmTypeFn on_type, void *ctx) {
  const OpInfo *oi = wasm_op_info(op);
  if (!oi) return false;
  switch (oi->imm) {
    case IMM_NONE: break;
    case IMM_BLOCKTYPE: { BlockType bt = rd_blocktype(r); SAY_BT(bt); break; }
    case IMM_HEAPTYPE:  SAY_HT(rd_heaptype(r)); break;
    case IMM_TYPE: {
      uint32_t t = rd_u32(r);
      if (on_type) on_type(t, ctx);
      break;
    }
    case IMM_LABEL: case IMM_FUNC: case IMM_LOCAL:
    case IMM_GLOBAL: case IMM_TABLE: case IMM_MEMIDX: case IMM_DATA:
    case IMM_ELEM: case IMM_TAG:
      rd_u32(r);
      break;
    case IMM_TRY_TABLE: {
      BlockType bt = rd_blocktype(r); SAY_BT(bt);
      uint32_t n = rd_u32(r);
      for (uint32_t i = 0; i < n; i++) {
        uint8_t kind = rd_u8(r);

        if (kind > 3) warmc_malformed("unknown catch clause 0x%02x", kind);
        if (kind < 2) rd_u32(r);
        rd_u32(r);
      }
      break;
    }
    case IMM_TYPE_TABLE: case IMM_TYPE_U32: {
      uint32_t t = rd_u32(r);
      if (on_type) on_type(t, ctx);
      rd_u32(r);
      break;
    }
    case IMM_DATA_MEM:
    case IMM_MEM_MEM: case IMM_ELEM_TABLE: case IMM_TABLE_TABLE:
      rd_u32(r); rd_u32(r);
      break;
    case IMM_MEMARG: case IMM_MEMARG_LANE: {

      uint32_t align = rd_u32(r);
      if (align & 0x40) rd_u32(r);
      rd_u64(r);
      if (oi->imm == IMM_MEMARG_LANE) (void)rd_u8(r);
      break;
    }
    case IMM_BR_CAST: {
      (void)rd_u8(r);
      rd_u32(r);
      SAY_HT(rd_heaptype(r));
      SAY_HT(rd_heaptype(r));
      break;
    }
    case IMM_LANE: (void)rd_u8(r); break;
    case IMM_LANES16: for (int i = 0; i < 16; i++) (void)rd_u8(r); break;
    case IMM_BRTABLE: {
      uint32_t n = rd_u32(r);
      for (uint32_t i = 0; i <= n; i++) rd_u32(r);
      break;
    }
    case IMM_VALTYPES: {
      uint32_t n = rd_u32(r);
      for (uint32_t i = 0; i < n; i++) op_skip_valtype(r);
      break;
    }
    case IMM_I32: rd_i32(r); break;
    case IMM_I64: rd_i64(r); break;
    case IMM_F32: rd_u32le(r); break;
    case IMM_F64: rd_u64le(r); break;
    case IMM_V128: rd_u64le(r); rd_u64le(r); break;
  }
  return true;
}
