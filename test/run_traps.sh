#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/testrun"
mkdir -p "$WORK"

if [ ! -s "$WORK/traps.wasm" ] || [ test/cases_traps/traps.wat -nt "$WORK/traps.wasm" ]; then
  wat2wasm $WABT_FLAGS test/cases_traps/traps.wat -o "$WORK/traps.wasm.$$" || exit 1
  mv -f "$WORK/traps.wasm.$$" "$WORK/traps.wasm"
fi

[ "${1:-}" = --prepare ] && exit 0

FTAG=$(wftag)
BIN="$WORK/traps$FTAG"
wtimeout ./$BUILD/warmc ${WARMC_FLAGS:-} $WARMC_TARGET_FLAGS -o "$BIN" "$WORK/traps.wasm" || exit 1

pass=0; fail=0
expect() {
  local name="$1" want="$2"
  local out rc
  out=$(wruntimeout $WARMC_RUN "$BIN" "$name" 2>&1); rc=$?
  if [ "$want" = "OK" ]; then
    if [ "$rc" -eq 0 ]; then pass=$((pass+1)); else
      echo "FAIL[traps] $name: expected success, got rc=$rc: $out"; fail=$((fail+1)); fi
    return
  fi
  if [ "$rc" -ne 1 ] || [[ "$out" != *"$want"* ]]; then
    echo "FAIL[traps] $name: expected trap '$want', got rc=$rc: $out"
    fail=$((fail+1))
  else
    pass=$((pass+1))
  fi
}

expect ok                "OK"
expect mem1_high_ok      "OK"
expect mem1_oob          "out of bounds memory access"
expect mem0_oob_vs_mem1  "out of bounds memory access"
expect mem1_store_oob    "out of bounds memory access"
expect mem1_fill_oob     "out of bounds memory access"
expect mem1_copy_oob     "out of bounds memory access"
expect mem1_oob_dyn      "out of bounds memory access"
expect m64_ok            "OK"
expect m64_oob_small     "out of bounds memory access"
expect m64_oob_2p32      "out of bounds memory access"
expect m64_oob_high      "out of bounds memory access"
expect m64_oob_top       "out of bounds memory access"
expect m64_oob_neg       "out of bounds memory access"
expect m64_off_wrap      "out of bounds memory access"
expect m64_store_oob     "out of bounds memory access"
expect m64_oob_dyn       "out of bounds memory access"
expect m64_clamp_signed_oob "out of bounds memory access"
expect m64_clamp_mirror_oob "out of bounds memory access"
expect m64_clamp_edge_oob   "out of bounds memory access"
expect m64_clamp_edge_ok    "OK"
expect m64_clamp_else_oob   "out of bounds memory access"
expect m64_clamp_else_ok    "OK"
expect m64_clamp_mirror_ok  "OK"
expect m64_clamp64_oob      "out of bounds memory access"
expect m64_clamp64_ok       "OK"
expect m64_iv_entry_oob     "out of bounds memory access"
expect m64_iv_span_oob      "out of bounds memory access"
expect m64_iv_ok            "OK"
expect m64_fill_oob      "out of bounds memory access"
expect m64_copy_oob      "out of bounds memory access"
expect t64_oob           "undefined element"
expect t64_oob_high      "undefined element"
expect t64_get_oob       "out of bounds table access"
expect unreachable       "unreachable executed"
expect ldcomb_last_ok    "OK"
expect ldcomb_oob        "out of bounds memory access"
expect ldcomb_wrap       "out of bounds memory access"
expect ldcomb_mixed      "out of bounds memory access"
expect load_oob          "out of bounds memory access"
expect load_oob_dyn      "out of bounds memory access"
expect store_oob         "out of bounds memory access"
expect load_oob_off      "out of bounds memory access"
expect v128_load_oob     "out of bounds memory access"
expect v128_store_oob    "out of bounds memory access"
expect v128_load_last_ok "OK"
expect v128_splat_oob    "out of bounds memory access"
expect v128_splat_last_ok "OK"
expect v128_lane_oob     "out of bounds memory access"
expect v128_bce_oob      "out of bounds memory access"
expect div0              "integer divide by zero"
expect div0_u            "integer divide by zero"
expect rem0              "integer divide by zero"
expect div_ovf           "integer overflow"
expect div0_64           "integer divide by zero"
expect div_ovf64         "integer overflow"
expect div0_dyn          "integer divide by zero"
expect div0_masked       "integer divide by zero"
expect div0_loop_u       "integer divide by zero"
expect rem0_loop_u       "integer divide by zero"
expect fuel_loop         "unreachable executed"
expect fuel_loop_ok      "OK"
expect fuel_loop_call    "unreachable executed"
expect fuel_tail         "unreachable executed"
expect divrem_ovf        "integer overflow"
expect rem0_masked       "integer divide by zero"
expect div0_wrap         "integer divide by zero"
expect bce_and_edge      "out of bounds memory access"
expect bce_and_edge_ok   "OK"
expect guard_under_ok    "OK"
expect guard_over_ok     "OK"
expect guard_under_oob   "out of bounds memory access"
expect guard_over_oob    "out of bounds memory access"
expect guard_far_addr    "out of bounds memory access"
expect guard_far_addr_store "out of bounds memory access"
expect guard_far_v128    "out of bounds memory access"
expect guard_far_dyn     "out of bounds memory access"
expect bce_shl           "out of bounds memory access"
expect bce_rem_ok        "OK"
expect div0_shl          "integer divide by zero"
expect div0_shl64        "integer divide by zero"
expect shl_in_range_ok   "OK"
expect trunc_nan         "invalid conversion to integer"
expect trunc_big         "invalid conversion to integer"
expect trunc_neg         "invalid conversion to integer"
expect trunc_inf         "invalid conversion to integer"
expect trunc_big64       "invalid conversion to integer"
expect ind_oob           "undefined element"
expect ind_null          "uninitialized element"
expect ind_type          "indirect call type mismatch"
expect ind_param_type    "indirect call type mismatch"
expect ind_param_null    "uninitialized element"
expect ind_param_oob     "undefined element"
expect hoist_past_call   "unreachable executed"
expect hoist_past_div    "integer divide by zero"

expect guard_past_fault  "out of bounds memory access"
expect stack_overflow    "call stack exhausted"

expect mem_far_offset      "out of bounds memory access"
expect mem_far_offset_v128 "out of bounds memory access"
expect mem_far_store       "out of bounds memory access"
expect mem_far_store_v128  "out of bounds memory access"

expect exn_uncaught              "uncaught exception"
expect exn_uncaught_past_handler "uncaught exception"
expect exn_trap_not_caught       "unreachable executed"
expect exn_trap_not_caught_deep  "integer divide by zero"
expect exn_throw_null            "null reference"
expect exn_throw_null_global     "null reference"
expect exn_throw_null_table      "null reference"
expect exn_uncaught_deep         "uncaught exception"
expect exn_uncaught_indirect     "uncaught exception"
expect exn_uncaught_before_region "uncaught exception"
expect exn_uncaught_before_region_direct "uncaught exception"
expect exn_uncaught_from_handler "uncaught exception"
expect exn_stale_rethrow         "uncaught exception"
expect exn_tail_out_uncaught     "uncaught exception"
expect exn_uncaught_in_loop      "uncaught exception"
expect exn_uncaught_nested_regions "uncaught exception"
expect exn_uncaught_other_tag    "uncaught exception"
expect exn_uncaught_deep_pads    "uncaught exception"
expect exn_uncaught_tail_chain   "uncaught exception"
expect exn_throw_null_select     "null reference"
expect exn_trap_in_handler       "out of bounds memory access"

expect exn_trap_oob_in_region      "out of bounds memory access"
expect exn_trap_ind_type_in_region "indirect call type mismatch"
expect exn_trap_ind_null_in_region "uninitialized element"
expect exn_trap_conv_in_region     "invalid conversion to integer"
expect exn_trap_overflow_in_region "call stack exhausted"
expect exn_trap_after_catch        "integer divide by zero"

expect exn_caught_deep_ok        "OK"
expect exn_caught_past_pads_ok   "OK"
expect tail_into_catcher_ok      "OK"
expect dead_table_get            "out of bounds table access"

echo "traps: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
