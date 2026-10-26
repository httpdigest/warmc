#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

if [ ! -x "$WT" ]; then
  echo "run_gc: $WT not found (run \`make wasm-tools\`)" >&2
  exit 1
fi

assemble() {
  local src="$1" name="$2"
  if [ ! -s "$WORK/$name.wasm" ] || [ "$src" -nt "$WORK/$name.wasm" ]; then
    "$WT" parse "$src" -o "$WORK/$name.wasm.$$" || exit 1
    mv -f "$WORK/$name.wasm.$$" "$WORK/$name.wasm"
  fi
}

python3 test/gen_gc_cases.py "$WORK" >/dev/null || exit 1
assemble test/cases_gc/gc.wat gc
assemble test/cases_gc/gccollect.wat gccollect
assemble test/cases_gc/constroot.wat constroot
assemble test/cases_gc/exnpayload.wat exnpayload
assemble test/cases_gc/devirt_vt.wat devirt_vt
assemble test/cases_gc/ictab_sub.wat ictab_sub
assemble test/cases_gc/escape.wat escape
assemble test/cases_gc/castfacts.wat castfacts
assemble test/cases_gc/castfacts.wat castfacts_nt
assemble test/cases_gc/brcast.wat brcast
assemble test/cases_gc/tailcast.wat tailcast
assemble test/cases_gc/arraycopy.wat arraycopy
assemble test/cases_gc/arraynew.wat arraynew
assemble test/cases_gc/tightheap.wat tightheap
assemble "$WORK/gcgen.wat" gcgen

[ "${1:-}" = --prepare ] && exit 0

# The code is AArch64's but on Windows, whose is x86-64: the load barrier's
# fast path and the inline arrays of a computed length are AArch64's alone.
A64=1
[ "${WARMC_HOST:-}" = native-windows ] && A64=0

FTAG=$(wftag)

pass=0; fail=0
BIN=""

build_one() {
  BIN="$WORK/$1$FTAG"

  local dep="$WORK/$1.wasm" d
  for d in "./$BUILD/warmc" "$BUILD/warmrt.a" "$BUILD/warmrt-macos.a"; do
    [ -e "$d" ] && [ "$d" -nt "$dep" ] && dep="$d"
  done
  if [ -s "$BIN" ] && [ "$BIN" -nt "$dep" ]; then return; fi
  wtimeout ./$BUILD/warmc ${WARMC_FLAGS:-} $WARMC_TARGET_FLAGS \
    -o "$BIN.$$" "$WORK/$1.wasm" || { echo "FAIL[gc] cannot compile $1"; exit 1; }
  mv -f "$BIN.$$" "$BIN"
}

log_run() {
  local case="$1" args="$2"; shift 2
  ( unset WARMC_GC_STRESS WARMC_GC_GROWTH WARMC_GC_MIN_KB WARMC_GC_VERIFY \
        WARMC_GC_RELOC WARMC_GC_RELOCATE_ALL WARMC_GC_LAB WARMC_GC_RELEASE \
        WARMC_GC_HEAP_KB WARMC_GC_JITTER WARMC_GC_JITTER_SEED
    wruntimeout env WARMC_GC_STATS=1 WARMC_GC_MIN_KB=64 \
        "$@" sh -c 'exec "$0" "$1" $2' "$BIN" "$case" "$args" 2>&1 >/dev/null )
}

# While the collector waits for the program to answer mark end, it takes
# what the program publishes, so the pause has little left to drain.  That
# needs the collector to have a core to run on: with every core taken, the
# program reaches its safepoint first and drains the mark itself, which is
# correct and what this check reports as a failure.  Darwin's runtime asks
# for user-interactive QoS and gets the core anyway; Linux gives its thread
# nothing over the rest of a loaded machine.  So run_all.sh, which loads it
# with every row at once, sets GC_PAUSE_CHECK=0 for the rows and runs this
# alone at the end with GC_PAUSE_CHECK=only.
pause_check() {
  local out took
  out=$(log_run walk_no_alloc "60 20000")
  took=$(printf '%s' "$out" | sed -n 's/.*mark end, \([0-9]*\) taken waiting for it.*/\1/p')
  case "$out" in
    *"on a thread"*)
      if [ -n "$took" ] && [ "$took" -gt 1000 ]; then
        pass=$((pass+1))
      else
        echo "FAIL[gc] the mark end pause drained the mark: $out"
        fail=$((fail+1))
      fi
      ;;
  esac
}

expect() {
  local name="$1" want="$2"; shift 2
  local out rc t0 t1

  [ -n "${GC_TIMING:-}" ] && t0=$(perl -MTime::HiRes=time -e 'print time')
  out=$(wruntimeout $WARMC_RUN "$BIN" "$name" "$@" 2>&1); rc=$?
  if [ -n "${GC_TIMING:-}" ]; then
    t1=$(perl -MTime::HiRes=time -e 'print time')
    perl -e "printf \"%7.0f ms  %s\\n\", ($t1-$t0)*1000, \"$name $*\"" >&2
  fi
  if [ "$want" = "OK" ]; then
    if [ "$rc" -eq 0 ]; then pass=$((pass+1)); else
      echo "FAIL[gc] $name: expected success, got rc=$rc: $out"; fail=$((fail+1)); fi
    return
  fi
  if [ "$rc" -ne 1 ] || [[ "$out" != *"$want"* ]]; then
    echo "FAIL[gc] $name: expected trap '$want', got rc=$rc: $out"
    fail=$((fail+1))
  else
    pass=$((pass+1))
  fi
}

# What the program's barrier shades it shows the collector 32 at a time
# (warmc_gc_load_stub, gc_mark_ref), so the collector takes most of it while
# it marks rather than at the mark's end -- given a core of its own, which is
# why this runs where pause_check does.  Shown only at the end, 220 thousand
# waited for the pause in a concurrent walk, two thirds of what was shaded;
# shown as it goes, hundreds, and under a loaded machine a quarter at most.
publish_check() {
  local out shaded atend
  out=$(log_run walk_no_alloc "60 20000")
  shaded=$(printf '%s' "$out" | sed -n 's/.*, \([0-9]*\) objects shaded.*/\1/p')
  atend=$(printf '%s' "$out" | sed -n 's/.*, \([0-9]*\) taken at mark end.*/\1/p')
  case "$out" in
    *"on a thread"*)
      if [ -n "$shaded" ] && [ -n "$atend" ] && [ "$shaded" -gt 1000 ] &&
         [ $((atend * 2)) -lt "$shaded" ]; then
        pass=$((pass+1))
      else
        echo "FAIL[gc] what the program shaded waited for the mark's end: $out"
        fail=$((fail+1))
      fi
      ;;
  esac
}

if [ "${GC_PAUSE_CHECK:-1}" = only ]; then
  build_one gccollect
  pause_check
  publish_check
  echo "gc: $pass passed, $fail failed (the mark end pause alone)"
  [ "$fail" -eq 0 ]
  exit
fi

build_one gc
expect mixed_roundtrip   "OK"
expect no_overlap        "OK"
expect packed_widths     "OK"
expect new_fields        "OK"
expect distinct          "OK"
expect subtype_offsets   "OK"
expect subtype_wide_offsets "OK"
expect live_across_alloc "OK"
expect set_both_spilled  "OK"
expect i31_range         "OK"
expect i31_not_null      "OK"
expect const_struct      "OK"
expect const_widths      "OK"
expect licm_guard_null   "OK" 5
expect licm_guard_test   "OK" 5
expect licm_guard_vtable "OK" 5
expect licm_guard_array  "OK" 5
expect get_null          "null reference"
expect set_null          "null reference"
expect i31_get_null      "null reference"
expect get_null_local    "null reference"
expect set_null_loop     "null reference" 4
expect nc_second_read    "OK" 0
expect nc_second_read    "null reference" 999
expect nc_diamond        "OK" 0
expect nc_diamond        "null reference" 1
expect nc_as_non_null    "OK" 0
expect nc_as_non_null    "null reference" 1
expect nc_br_on_null     "OK" 0
expect nc_br_on_null     "OK" 1
expect nc_cast_alias     "OK" 0
expect nc_cast_alias     "null reference" 1
expect nc_loop           "OK" 4
expect len_licm          "OK" 0
expect len_licm          "null reference" 1
expect bce_guarded       "OK" 2
expect bce_guarded       "OK" 9
expect bce_unguarded     "OK" 4
expect bce_unguarded     "out of bounds array access" 5
expect bce_wrong_idx     "OK" 2
expect bce_wrong_idx     "out of bounds array access" 4
expect bce_loop          "OK" 7
expect bce_loop          "OK" 0
expect bce_counted       "OK" 6
expect bce_counted       "OK" 0
expect bce_step2         "OK" 4
expect bce_step2         "out of bounds array access" 5
expect fwd_basic         "OK" 7
expect fwd_call_reload   "OK" 3
expect fwd_cast          "OK" 11
expect fwd_null          "OK" 0
expect fwd_null          "null reference" 1
expect fwd_two           "OK" 5

expect array_new_widths     "OK"
expect array_new_default    "OK"
expect array_new_fixed      "OK"
expect array_set_get        "OK"
expect array_strides        "OK"
expect array_fill           "OK"
expect array_copy_overlap   "OK"
expect array_copy_between   "OK"
expect array_new_data       "OK"
expect array_init_data      "OK"
expect array_new_elem       "OK"
expect array_init_elem      "OK"
expect elem_evaluated_once  "OK"
expect array_large          "OK"
expect const_array          "OK"
expect arr_get_null         "null reference"
expect arr_set_null         "null reference"
expect arr_len_null         "null reference"
expect arr_fill_null        "null reference"
expect arr_copy_null_dst    "null reference"
expect arr_copy_null_src    "null reference"
expect arr_init_data_null   "null reference"
expect arr_mask_fits        "OK" 200
expect arr_mask_overruns    "out of bounds array access" 200
expect arr_len_unknown      "out of bounds array access" 50
expect arr_seg_len          "out of bounds array access" 50
expect arr_get_oob          "out of bounds array access"
expect arr_get_oob_neg      "out of bounds array access"
expect arr_get_oob_empty    "out of bounds array access"
expect arr_set_oob          "out of bounds array access"
expect arr_fill_oob         "out of bounds array access"
expect arr_fill_oob_wrap    "out of bounds array access"
expect arr_copy_oob_dst     "out of bounds array access"
expect arr_copy_oob_src     "out of bounds array access"
expect arr_init_data_oob_seg "out of bounds memory access"
expect arr_init_data_oob_arr "out of bounds array access"
expect arr_init_elem_oob_seg "out of bounds table access"
expect arr_new_data_oob      "out of bounds memory access"
expect arr_new_size_overflow  "allocation failed"
expect arr_new_size_overflow2 "allocation failed"

expect live_across_size        "OK"
expect ea_fwd                  "OK"
expect ea_chain                "OK"
expect ea_default              "OK"
expect ea_packed               "OK"
expect ea_mut                  "OK"
expect ea_escaped              "OK"
expect ea_elided               "OK" 200000

case "${WARMC_FLAGS:-}" in
  *-O0* | *no-escape*) ;;
  *)
    out=$(WARMC_GC_STATS=1 WARMC_GC_STRESS=0 WARMC_GC_MIN_KB=4096 \
          wruntimeout $WARMC_RUN "$BIN" ea_elided 200000 2>&1 >/dev/null)
    if [[ "$out" == *" 0 cycles"* ]]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] ea_elided still allocates: $out"
      fail=$((fail+1))
    fi
    ;;
esac

expect array_ops_spilled       "OK"
expect array_fill_copy_spilled "OK"
expect cast_spilled            "OK"

expect cast_shapes           "OK"
expect cast_nullability      "OK"
expect cast_named            "OK"
expect cast_func             "OK"
expect cast_extern           "OK"
expect const_convert         "OK"
expect cast_null_ok          "OK"
expect br_on_cast_paths      "OK"
expect br_on_cast_fail_paths "OK"
expect br_on_cast_carries    "OK"
expect br_on_cast_null_edge  "OK"
expect cast_fail_i31         "cast failure"
expect cast_fail_struct      "cast failure"
expect cast_fail_named       "cast failure"
expect cast_fail_null        "cast failure"
expect br_on_cast_fail_null_deref "null reference"
expect br_on_cast_null_deref      "null reference"

build_one gcgen
expect wide_v128    "OK"
expect wide_narrow  "OK"
expect wide_distinct "OK"
expect farpool_array "OK"
expect farpool_field "OK"

if [ "${WARMC_GC_STRESS:-0}" != 0 ]; then
  N=30; M=12; L=3
else
  N=${GC_N:-150000}; M=${GC_M:-15000}; L=${GC_L:-200}
fi

WB=$((L * 10))

H=${GC_H:-$(( N < 4000 ? N : 4000 ))}

build_one gcgen
expect manytypes          "OK" $M
expect deepconst          "OK"

build_one constroot
expect struct_chain       "OK"
expect array_elems        "OK"
expect global_get_identity "OK"
expect global_get_shared  "OK"

build_one brcast
expect loop_field   "OK" 0 1000 1000
expect loop_field   "OK" 1 1000 0
expect loop_field   "OK" 2 1000 0
expect loop_field   "OK" 3 1000 0
expect restart      "OK" 0 0
expect restart      "OK" 3 1
expect restart      "OK" 2 2
expect restart      "OK" 1 3
expect to_return    "OK" 0 1 1
expect to_return    "OK" 3 1 2
expect to_return    "OK" 1 0 0
expect to_return    "OK" 4 0 0
expect to_loop      "OK" 0 2
expect to_loop      "OK" 3 1
expect to_loop      "OK" 1 0
expect to_loop      "OK" 4 0
expect fail_branch  "OK" 0 10 20
expect fail_branch  "OK" 1 10 30
expect fail_branch  "OK" 3 10 20
expect fail_branch  "OK" 2 10 40

# Without inlining, so that the functions whose only use of the instance is a
# cast's type test stay functions of their own.
WARMC_FLAGS="${WARMC_FLAGS:-} -fno-inline" build_one tailcast
expect pressure_cast "OK" 0
expect pressure_cast "OK" 1
for y in 0 1; do
  got=$(wruntimeout $WARMC_RUN "$BIN" tail_cast $y 2>&1)
  if [ "$got" = "i32:$((3 * y))" ]; then pass=$((pass+1)); else
    echo "FAIL[gc] tail_cast $y: expected i32:$((3 * y)), got: $got"; fail=$((fail+1)); fi
done

build_one arraycopy
for t in 8 16 32 64; do expect array_copy_$t "OK"; done

build_one arraynew
for t in 8 16 32 64 f32 f64 v128 ref; do expect lengths_$t "OK"; done
expect keep_across_cycles "OK" $N
expect too_long "allocation failed" 2147483647
# and inline: most of those arrays' bytes from the allocation buffer, not
# the runtime's call, which made them all before -- stopped-the-world, so
# that a collector short of a core does not hold the next cycle back and
# send what comes past the trigger to the call
case "$A64${WARMC_GC_STRESS:-}${WARMC_GC_LAB:-}${WARMC_GC_MIN_KB:-}${WARMC_GC_GROWTH:-}\
${WARMC_GC_HEAP_KB:-}" in
  1)
    out=$(WARMC_GC_STATS=1 WARMC_GC_CONC=0 \
          wruntimeout $WARMC_RUN "$BIN" keep_across_cycles 150000 2>&1 >/dev/null)
    cb=$(printf '%s' "$out" | sed -n 's/.*live, \([0-9]*\) bytes allocated.*/\1/p')
    fb=$(printf '%s' "$out" | sed -n 's/.*regions, \([0-9]*\) bytes fast-allocated.*/\1/p')
    if [ -n "$cb" ] && [ -n "$fb" ] && [ "$fb" -ge $((cb * 2)) ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] arrays of a computed length were not allocated inline: $out"
      fail=$((fail+1))
    fi
    ;;
esac

# A heap smaller than four times the minimum trigger (16 MB) waits a quarter
# of itself between cycles (rt.c gc_min_trigger), not 16 MB it does not
# have: waiting for the whole of it, it would collect only when an
# allocation failed, the program waiting out whole cycles each time.
case "${WARMC_GC_MIN_KB:-}${WARMC_GC_GROWTH:-}${WARMC_GC_STRESS:-}" in
  "")
    out=$(WARMC_GC_STATS=1 WARMC_GC_HEAP_KB=8192 \
          wruntimeout $WARMC_RUN "$BIN" keep_across_cycles 150000 2>&1 >/dev/null)
    trig=$(printf '%s' "$out" | sed -n 's/.*[ ,]\([0-9][0-9]*\) byte trigger.*/\1/p')
    cyc=$(printf '%s' "$out" | sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p')
    if [ "$trig" = 2097152 ] && [ -n "$cyc" ] && [ "$cyc" -gt 4 ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] a small heap did not wait a quarter of itself between cycles: $out"
      fail=$((fail+1))
    fi
    ;;
esac

# A 4 MB heap with little room to spare (cases_gc/tightheap.wat): live data
# on every page, of 1.75 to 3 MB, that only relocation frees; and one filled
# past where the program's allocations stop for the room relocation keeps,
# then an exception thrown.  Stopped-the-world and with the collector on its
# thread.  Each passes with 256 KB or more of live data to spare, and one or
# more runs out of memory without any one of: the room relocation keeps
# (rt.c gc_reserve_gran), relocation's use of it and a last resort's, the
# second cycle a failed allocation waits for, or the allocation buffers'
# small pages.
case "${WARMC_GC_STRESS:-}${WARMC_GC_RELOC:-}${WARMC_GC_RELOCATE_ALL:-}\
${WARMC_GC_RELOC_STALL:-}${WARMC_GC_HEAP_KB:-}${WARMC_GC_MIN_KB:-}\
${WARMC_GC_GROWTH:-}${WARMC_GC_LAB:-}${WARMC_GC_ROUNDS:-}${WARMC_GC_CONC:-}\
${WARMC_GC_JITTER:-}" in
  "")
    build_one tightheap
    for conc in 0 1; do
      export WARMC_GC_HEAP_KB=4096 WARMC_GC_CONC=$conc
      expect frag           "OK" 458752 20000 7 40 6
      expect frag           "OK" 720896 20000 7 8 6
      expect frag           "OK" 622592 20000 61 400 6
      expect frag_structs   "OK" 589824 20000 7 6
      expect full_then_throw "OK" 942
      unset WARMC_GC_HEAP_KB WARMC_GC_CONC
    done
    ;;
esac

build_one exnpayload
expect payload_survives   "OK"
expect pad_keeps_ref      "OK"

build_one devirt_vt
expect vt_mono            "OK"
expect vt_sub             "OK"
expect vt_poly            "OK"
expect vt_nullslot_ok     "OK"
expect vt_nulltrap        "null reference"
expect vt_mut             "OK"
expect vt_dup             "OK"

build_one ictab_sub
expect sub_ok             "OK"
expect sub_f_other        "indirect call type mismatch"
expect sub_f_null         "uninitialized element"
expect sub_g_super        "indirect call type mismatch"
expect sub_g_sibling      "indirect call type mismatch"
expect sub_h_sibling      "indirect call type mismatch"
expect sub_o_struct       "indirect call type mismatch"

# The objects by index: 0 null, 1 i31, 2 $base, 3 $d1, 4 $d2, 5 $leaf,
# 6 $other, 7 $twin1, 8 $twin2, 9 $arr, 10 $d3, 11 $pad.
build_one castfacts
for ke in 0:-7 1:1005 2:10 3:21 4:32 5:33 6:-7 7:1 8:3 9:7 10:14 11:-7; do
  expect when_chain       "OK" ${ke%:*} ${ke#*:}
done
expect false_cast         "OK" 3
expect false_cast         "OK" 5
for k in 0 1 2 4 8 10; do expect false_cast "cast failure" $k; done
for ke in 0:1 1:0 2:0 3:100 5:100; do expect false_test "OK" ${ke%:*} ${ke#*:}; done
for ke in 0:1 1:-1 2:-1 3:11 5:11; do expect null_true "OK" ${ke%:*} ${ke#*:}; done
expect null_true_get      "null reference" 0
expect null_true_get      "OK" 3
expect null_true_get      "OK" 2
expect null_true_cast     "cast failure" 0
expect null_true_cast     "OK" 3
expect null_true_cast     "OK" 4
for ke in 0:-1 1:50 2:10 3:-1 4:12 5:-1 6:50 9:50 10:14; do
  expect null_false       "OK" ${ke%:*} ${ke#*:}
done
for ke in 0:0 2:0 3:0 5:36; do expect sub_super "OK" ${ke%:*} ${ke#*:}; done
for k in 0 1 3 5; do expect super_sub "OK" $k; done
for k in 2 4 10; do expect super_sub "cast failure" $k; done
for ke in 0:-1 1:-1 2:0 3:1 4:2 5:5 6:-1 10:8; do
  expect super_tests      "OK" ${ke%:*} ${ke#*:}
done
for ke in 0:0 1:102 2:0 3:48 5:48 6:0; do expect disjoint "OK" ${ke%:*} ${ke#*:}; done
for ke in 0:-1 1:-1 2:5 3:5 4:5 5:5 6:4 7:6 8:6 9:-1 10:5 11:4; do
  expect abstract         "OK" ${ke%:*} ${ke#*:}
done
for ke in 3:1 5:3; do expect after_cast "OK" ${ke%:*} ${ke#*:}; done
for k in 0 2 4; do expect after_cast "cast failure" $k 0; done
for ke in 0:6 3:3 5:3; do expect after_null_cast "OK" ${ke%:*} ${ke#*:}; done
expect after_null_cast    "cast failure" 2 0
# The merges of a test with a null check, and of two tests.
merge_cases() {
  for ke in 0:-2 1:-1 2:-1 3:21 5:123; do expect merge_and "OK" ${ke%:*} ${ke#*:}; done
  for ke in 0:101 1:50 2:50 3:111 5:111; do expect merge_or "OK" ${ke%:*} ${ke#*:}; done
  expect merge_or_get       "null reference" 0
  for k in 1 2 3 5; do expect merge_or_get "OK" $k; done
  expect merge_or_cast      "cast failure" 0
  for k in 1 2 3 5; do expect merge_or_cast "OK" $k; done
  for ke in 0:-1 2:-1 3:1 4:-1 5:11; do expect merge_tests "OK" ${ke%:*} ${ke#*:}; done
  for ke in 0:-1 2:-1 3:-1 4:110 5:101; do expect merge_unrelated "OK" ${ke%:*} ${ke#*:}; done
  expect merge_loop_and     "OK" 0 11 44
  expect merge_loop_and     "OK" 0 2 0
  expect merge_loop_and     "OK" 4 11 23
  expect merge_loop_or      "OK" 1 11 46
  expect merge_loop_or      "OK" 4 4 0
  expect merge_loop_or      "null reference" 0 3 0
}
merge_cases
for ke in 0:2 2:-1 3:3 5:7; do expect static_type "OK" ${ke%:*} ${ke#*:}; done
for ke in 0:-1 2:-1 3:1 5:3; do expect br_cast "OK" ${ke%:*} ${ke#*:}; done
for ke in 1:77 2:77 3:21 5:23; do expect br_cast_fail "OK" ${ke%:*} ${ke#*:}; done
expect br_cast_fail       "null reference" 0 0
# Jump threading makes most of those merges into blocks two ways meet at;
# built without it they stay merges, branched on by their param.
WARMC_FLAGS="${WARMC_FLAGS:-} -fno-thread" build_one castfacts_nt
merge_cases

build_one escape
expect sr_box_loop        "OK" 10 45
expect sr_box_cond        "OK" 10 20
expect sr_nested          "OK" 4 16
expect sr_branches        "OK" 1 1002
expect sr_branches        "OK" 0 120
expect sr_merge           "OK" 1 34
expect sr_merge           "OK" 0 56
expect sr_loop_fresh      "OK" 10 55
expect sr_loop_fresh      "OK" 0 0
expect sr_swap            "OK" 3 21
expect sr_swap            "OK" 4 12
expect sr_cast            "OK" 5 12
expect sr_eq              "OK" 2
expect sr_array_fixed     "OK" 6 4030
expect sr_array_default   "OK" 1 500
expect sr_array_default   "OK" 0 7
expect sr_array_fill      "OK" 0x3FF8000000000000 17
expect sr_vectors         "OK" 34
expect sr_packed          "OK" -5580025536
expect sr_packed_array    "OK" 1422
expect sr_ref_field       "OK" 9
expect sr_helper_loop     "OK" 10 670
expect sr_cold_report     "OK" 10 180
expect sr_closure_dispatch "OK" 10 3 340
expect sr_caught          "OK" 20 54
expect sr_caught          "OK" 1 0
expect sr_throw_two       "OK" 10 135
expect sr_throw_clause    "OK" 10 55
expect keep_call          "OK" 0 6
expect keep_global        "OK" 8
expect keep_select        "OK" 1 11
expect keep_select        "OK" 0 22
expect keep_oob           "out of bounds array access" 0
expect keep_eh            "OK" 1 2
expect keep_eh            "OK" 0 3
expect keep_reused        "OK" 1 5
expect keep_reused        "OK" 0 2
expect keep_shared_edge   "OK" 0 2
expect keep_shared_edge   "OK" 3 100
expect pe_rare_call       "OK" 20 0 570
expect pe_best            "OK" 50 32100
expect pe_array           "OK" 1 180
expect pe_array           "OK" 0 11
expect pe_packed          "OK" 1 -199000
expect pe_packed          "OK" 0 -199000
expect pe_merge           "OK" 1 1 111
expect pe_merge           "OK" 0 1 113
expect pe_merge           "OK" 1 0 11
expect pe_merge           "OK" 0 0 13
expect pe_edge            "OK" 1 15
expect pe_edge            "OK" 0 -1
expect sr_found           "OK" 10 49 7049
expect sr_found           "OK" 10 50 -1
expect sr_found_loop      "OK" 10 49 7049
expect sr_found_loop      "OK" 10 50 -1
expect sr_nested_cond     "OK" 1 8
expect sr_nested_cond     "OK" 0 -1
expect sr_nullable        "OK" 1 43
expect sr_nullable        "OK" 0 -1
expect sr_nullable_trap   "OK" 1 2
expect sr_nullable_trap   "null reference" 0 2
expect sr_nullable_test   "OK" 1 110
expect sr_nullable_test   "OK" 0 10
expect keep_escape_write  "OK" 1 9
expect keep_escape_write  "OK" 0 9
expect keep_escape_identity "OK" 1 1
expect keep_escape_identity "OK" 0 0
expect keep_loop_escape   "OK" 5 5
expect keep_nullable_escape "OK" 1 5
expect keep_nullable_escape "OK" 0 -1
expect keep_nullable_eq   "OK" 1 0
expect keep_nullable_eq   "OK" 0 1
expect keep_caught_ref    "OK" 5 7
expect keep_dispatch_mutable "OK" 10 3 150

# The sr_ and pe_ cases are right whether or not their objects were
# replaced, so whether they were is asked separately: at the default -O2,
# no allocation in an sr_ function may be left standing -- one made at an
# escape point on the way must go too -- and a pe_ function must have one
# made at its escape points, with nothing else standing but the objects so
# made, which escape on every path.  A verdict counts only for an
# allocation still standing in the function as optimised: one the pass saw
# while a branch that cannot be taken still made it, and that went with the
# branch, is not left standing.
# WARMC_ESCAPE_DEBUG names functions by index; wasm-tools maps them back.
if [ -z "${WARMC_FLAGS:-}" ] || [ "${WARMC_FLAGS:-}" = -O2 ]; then
  if why=$(WARMC_ESCAPE_DEBUG=1 wtimeout ./$BUILD/warmc -O2 -j1 --print-ir -c -o /dev/null \
             "$WORK/escape.wasm" 2>&1 >/dev/null |
           python3 -c '
import re, subprocess, sys
text = subprocess.run([sys.argv[1], "print", sys.argv[2]],
                      capture_output=True, text=True).stdout
ids = {m.group(1): int(m.group(2)) for m in re.finditer(r"\(func (\$[^\s()]+) \(;(\d+);\)", text)}
names = {}
for m in re.finditer(r"\(export \"((?:sr|pe)_\w+)\" \(func (\d+|\$[^\s()]+)\)\)", text):
    names[int(m.group(2)) if m.group(2).isdigit() else ids[m.group(2)]] = m.group(1)
last = {}
standing = set()
cur = None
for line in sys.stdin:
    h = re.match(r"; ---- (\w+): wasm_func_(\d+) ", line)
    if h:
        cur = int(h.group(2)) if h.group(1) == "optimised" else None
        continue
    if cur is not None:
        a = re.match(r"\s+(v\d+):.*? = (struct\.new|array\.new)", line)
        if a: standing.add((cur, a.group(1)))
        continue
    m = re.match(r"escape wasm_func_(\d+) (v\d+) \w+ d\d+: (.*)", line.strip())
    if m and int(m.group(1)) in names: last[(int(m.group(1)), m.group(2))] = m.group(3)
def fine(n, w):
    if n.startswith("sr_"): return w == "replaced" or w.startswith("partial:")
    return w == "replaced" or w.startswith("partial:") or w.endswith(" on every path")
bad = ["%s %s: %s" % (names[f], v, w) for (f, v), w in sorted(last.items())
       if not fine(names[f], w) and (f, v) in standing]
bad += ["%s: no allocation reached the pass" % n for f, n in sorted(names.items())
        if not any(k[0] == f for k in last)]
bad += ["%s: nothing was made at an escape point" % n for f, n in sorted(names.items())
        if n.startswith("pe_") and not any(k[0] == f and w.startswith("partial:")
                                          for k, w in last.items())]
print("; ".join(bad))
sys.exit(1 if bad else 0)' "$WT" "$WORK/escape.wasm"); then
    pass=$((pass+1))
  else
    echo "FAIL[gc] escape: an sr_ or pe_ allocation was not replaced: $why"
    fail=$((fail+1))
  fi
fi

build_one gccollect
expect churn              "OK" $N
expect churn_global       "OK" $N
expect cycles_die         "OK" $M
expect keep_local         "OK" $N
expect keep_many_locals   "OK" $N
expect keep_param         "OK" $N
expect keep_global        "OK" $N
expect keep_table         "OK" $N
expect keep_deep          "OK" $M
expect list_survives      "OK" $N
expect walk_no_alloc      "OK" $L 2000
expect live_heap          "OK" $M $N
expect tree_survives      "OK" $M
expect array_refs         "OK" $N
expect big_array_refs     "OK" $M
expect nested_arrays      "OK" $N
expect opaque_arrays      "OK" $N
expect identity           "OK" $M
expect mixed_tags         "OK" $N 6
expect funcrefs_untouched "OK" $N
expect hidden_by_store    "OK" $N

expect reload_across_collect "OK" $L
expect extern_field_moves    "OK" $N
expect copy_then_reload      "OK" $L
expect hide_and_restore   "OK" $N
expect array_set_barrier  "OK" $M
expect bulk_ref_stores    "OK" $M
expect copy_refs_overlap  "OK" $L
expect remset_overflow    "OK" $L $L
expect born_old_kept      "OK" $L
expect table_late_store   "OK" $L 300
expect loop_many_targets  "OK" $L
expect init_stores        "OK" $N
expect exn_payload        "OK" $N
expect exnref_kept        "OK" $M
expect many_throws        "OK" $M
expect keep_across_try    "OK" $M
expect keep_across_cold   "OK" $M
expect block_params       "OK" $N
expect cast_paths         "OK" $N
expect subtype_fields     "OK" $N
expect fragment           "OK" $M
expect large_objects      "OK" $L

if [ "${WARMC_GC_HEAP_KB:-1048576}" -ge 65536 ]; then
  expect huge_object      "OK" 2
fi
expect wide_header        "OK" $N
expect satb_v128_live     "OK" $M
expect satb_stall         "OK" $M
expect keep_across_new_data "OK" $N
expect keep_across_new_elem "OK" $N
expect table_hide_restore "OK" $H
expect table_fill_hide    "OK" $H
expect table_copy_hide    "OK" $H
expect table_init_hide    "OK" $H
expect global_hide_restore "OK" $N
expect table_grow_keeps   "OK" $N
expect funcref_table_churn "OK" $M
expect lab_live_chain     "OK" $M
expect version_remember   "OK" $L
expect version_shapes     "OK" $L
expect version_satb       "OK" $L
expect version_hide       "OK" $H
expect arr_copy_hide      "OK" $N
expect arr_fill_hide      "OK" $N
expect arr_init_elem_hide "OK" $N
expect chunk_edges        "OK" $M
expect chunk_mutate       "OK" $N
expect deep_frames        "OK" $M
expect tail_ref_chain     "OK" $N
expect many_ref_fields    "OK" $M
expect zero_len_churn     "OK" $N
expect exn_field_rethrow  "OK" $M
expect extern_roundtrip   "OK" $M
expect grow_during_cycle  "OK" $M
expect mixed_during_sweep "OK" $N
expect ea_mixed_live      "OK" $N
expect old_to_new         "OK" $M
expect old_to_new_arrayops "OK" $M

expect mark_writeback_array  "OK" $WB
expect mark_writeback_struct "OK" $WB
expect mark_writeback_table  "OK" $WB
expect remset_dedupe      "OK" $M
expect remset_wide        "OK" $M
expect promote_chain      "OK" $M

# The load barrier's slow path -- where relocation healing, the self-healing
# store and the mark write all live -- is not reached by a churn workload
# however the collector is paced: the collector's own healing keeps up.  It
# takes a live graph large enough to fall behind, and nothing in this file
# asked for one, so `gc_barrier_slow` was zero for every row of the matrix.
# The probe fixes its own configuration and runs only when the caller has set
# none of its own.  It collects stop-the-world, which is what makes the count
# a number rather than a race: relocation then happens entirely at a
# safepoint, so every later load of a moved reference heals.  Concurrently the
# collector heals most of them itself and the count swings between four
# thousand and zero from run to run.
case "${WARMC_GC_STRESS:-}${WARMC_GC_VERIFY:-}${WARMC_GC_RELOC:-}\
${WARMC_GC_CONC:-}${WARMC_GC_ROUNDS:-}${WARMC_GC_JITTER:-}${WARMC_GC_MIN_KB:-}\
${WARMC_GC_GROWTH:-}${WARMC_GC_HEAP_KB:-}${WARMC_GC_LAB:-}\
${WARMC_GC_RELEASE:-}${WARMC_GC_RELOC_STALL:-}${WARMC_GC_RELOCATE_ALL:-}" in
  "")
    out=$(unset WARMC_GC_VERIFY WARMC_GC_RELOCATE_ALL WARMC_GC_HEAP_KB \
                WARMC_GC_LAB WARMC_GC_RELEASE WARMC_GC_GROWTH WARMC_GC_JITTER \
                WARMC_GC_CONC WARMC_GC_ROUNDS WARMC_GC_RELOC_STALL
          WARMC_GC_STATS=1 WARMC_GC_MIN_KB=4 WARMC_GC_RELOC=8 WARMC_GC_CONC=0 \
          wruntimeout $WARMC_RUN "$BIN" tree_survives 60000 2>&1 >/dev/null)
    healed=$(printf '%s' "$out" | sed -n 's/.*barrier: \([0-9]*\) loads healed.*/\1/p')
    if [ -n "$healed" ] && [ "$healed" -gt 0 ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] no load took the barrier's slow path: $out"
      fail=$((fail+1))
    fi
    ;;
esac

# The barrier's fast path (rt/start.S, warmc_gc_load_stub) heals a reference
# that needs only the good colour, and shades its object while a mark is on,
# with no call of the C slow path.  A concurrent walk of a chain rebuilt each
# turn loads every reference of it stale, and nearly all take the fast path:
# the chain's nodes come from allocation buffers on medium pages, most past
# their head granule, whose mark epoch the granule map leads it to.  Half is
# the floor, which an object on a later granule sent to C would break.  (What
# it shades it shows the collector as it goes: publish_check.)  It writes
# what it heals back to
# the slot, so a second walk of a chain between the same two flips heals
# nothing more: stopped-the-world, the count is a number, and without the
# write it doubled.  And a cycle that relocates nothing leaves the mark's
# colour good, so stopped-the-world with relocation off the program heals
# nothing at all: a flip to R after every mark had it heal half a million
# loads.
case "${WARMC_GC_STRESS:-}${WARMC_GC_VERIFY:-}${WARMC_GC_RELOC:-}\
${WARMC_GC_CONC:-}${WARMC_GC_ROUNDS:-}${WARMC_GC_JITTER:-}${WARMC_GC_MIN_KB:-}\
${WARMC_GC_GROWTH:-}${WARMC_GC_HEAP_KB:-}${WARMC_GC_LAB:-}\
${WARMC_GC_RELEASE:-}${WARMC_GC_RELOC_STALL:-}${WARMC_GC_RELOCATE_ALL:-}" in
  "")
    out=$(WARMC_GC_STATS=1 WARMC_GC_MIN_KB=64 \
          wruntimeout $WARMC_RUN "$BIN" walk_no_alloc 60 20000 2>&1 >/dev/null)
    healed=$(printf '%s' "$out" | sed -n 's/.*barrier: \([0-9]*\) loads healed.*/\1/p')
    hfast=$(printf '%s' "$out" | sed -n 's/.*loads healed (\([0-9]*\) by the fast path).*/\1/p')
    shaded=$(printf '%s' "$out" | sed -n 's/.*, \([0-9]*\) objects shaded.*/\1/p')
    sfast=$(printf '%s' "$out" | sed -n 's/.*objects shaded (\([0-9]*\) by the fast path).*/\1/p')
    if [ "$A64" = 0 ]; then
      :
    elif [ -n "$healed" ] && [ -n "$hfast" ] && [ -n "$shaded" ] && [ -n "$sfast" ] && \
       [ "$healed" -gt 1000 ] && [ $((hfast * 2)) -ge "$healed" ] && \
       [ "$shaded" -gt 1000 ] && [ $((sfast * 2)) -ge "$shaded" ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] the barrier's fast path healed or shaded too few: $out"
      fail=$((fail+1))
    fi
    once=$(WARMC_GC_STATS=1 WARMC_GC_MIN_KB=64 WARMC_GC_CONC=0 \
           wruntimeout $WARMC_RUN "$BIN" rewalk 100 10000 1 10000 2>&1 >/dev/null |
           sed -n 's/.*barrier: \([0-9]*\) loads healed.*/\1/p')
    twice=$(WARMC_GC_STATS=1 WARMC_GC_MIN_KB=64 WARMC_GC_CONC=0 \
            wruntimeout $WARMC_RUN "$BIN" rewalk 100 10000 2 10000 2>&1 >/dev/null |
            sed -n 's/.*barrier: \([0-9]*\) loads healed.*/\1/p')
    if [ -n "$once" ] && [ -n "$twice" ] && [ "$once" -gt 1000 ] && \
       [ "$twice" -le $((once + once / 20)) ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] a healed load left its slot stale: $once healed walking once, $twice twice"
      fail=$((fail+1))
    fi
    out=$(WARMC_GC_STATS=1 WARMC_GC_MIN_KB=64 WARMC_GC_CONC=0 WARMC_GC_RELOC=0 \
          wruntimeout $WARMC_RUN "$BIN" walk_no_alloc 20 20000 2>&1 >/dev/null)
    healed=$(printf '%s' "$out" | sed -n 's/.*barrier: \([0-9]*\) loads healed.*/\1/p')
    cycles=$(printf '%s' "$out" | sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p')
    if [ "$healed" = 0 ] && [ -n "$cycles" ] && [ "$cycles" -gt 5 ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] a cycle that moved nothing left references stale: $out"
      fail=$((fail+1))
    fi
    ;;
esac

case "${WARMC_GC_STRESS:-0}${WARMC_GC_GROWTH:+g}" in
  0)
    out=$(WARMC_GC_STATS=1 WARMC_GC_MIN_KB=16 WARMC_GC_CONC=0 \
          wruntimeout $WARMC_RUN "$BIN" churn 200000 2>&1 >/dev/null)
    cycles=$(printf '%s' "$out" | sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p')
    live=$(printf '%s' "$out" | sed -n 's/.*cycles, \([0-9]*\) bytes live.*/\1/p')
    opened=$(printf '%s' "$out" | sed -n 's/.*, \([0-9]*\) pages opened.*/\1/p')
    freed=$(printf '%s' "$out" | sed -n 's/.*pages opened, \([0-9]*\) freed.*/\1/p')
    if [ -n "$cycles" ] && [ "$cycles" -gt 0 ] && \
       [ -n "$live" ] && [ "$live" -lt 1048576 ] && \
       [ -n "$opened" ] && [ "$opened" -gt 8 ] && \
       [ -n "$freed" ] && [ "$freed" -ge $((opened - 8)) ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] a churn that keeps nothing did not give its pages back: $out"
      fail=$((fail+1))
    fi

    held() {
      local o
      o=$(unset WARMC_GC_STRESS WARMC_GC_GROWTH WARMC_GC_VERIFY WARMC_GC_RELOC \
                WARMC_GC_RELOCATE_ALL WARMC_GC_LAB WARMC_GC_RELEASE \
                WARMC_GC_HEAP_KB
          WARMC_GC_STATS=1 WARMC_GC_MIN_KB=16 \
          wruntimeout $WARMC_RUN "$BIN" churn "$1" 2>&1 >/dev/null)
      local op fr cy
      cy=$(printf '%s' "$o" | sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p')
      op=$(printf '%s' "$o" | sed -n 's/.*, \([0-9]*\) pages opened.*/\1/p')
      fr=$(printf '%s' "$o" | sed -n 's/.*pages opened, \([0-9]*\) freed.*/\1/p')
      [ -n "$cy" ] && [ "$cy" -gt 0 ] && [ -n "$op" ] && [ -n "$fr" ] &&
        [ "$op" -gt 8 ] && echo $((op - fr))
      printf '%s\n' "$o" >&2
    }
    # 100000: churn's arrays are allocated inline, from buffers of 16 KB,
    # and 50000 of them opened only 8 pages with the collector stopped
    short=$(held 100000 2>"$WORK/held.$FTAG.$$.s")
    long=$(held 400000 2>"$WORK/held.$FTAG.$$.l")

    if [ -n "$short" ] && [ -n "$long" ] && [ "$long" -le $((short * 3 + 8)) ]; then
      pass=$((pass+1))
    else
      echo "FAIL[gc] pages held by the concurrent collector grew with the run:" \
           "$short at 100000, $long at 400000"
      cat "$WORK/held.$FTAG.$$.s" "$WORK/held.$FTAG.$$.l"
      fail=$((fail+1))
    fi
    rm -f "$WORK/held.$FTAG.$$.s" "$WORK/held.$FTAG.$$.l"
    ;;
esac

out=$(log_run satb_stall 8000)
cycles=$(printf '%s' "$out" | sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p')
live=$(printf '%s' "$out" | sed -n 's/.*cycles, \([0-9]*\) bytes live.*/\1/p')
if [ -n "$cycles" ] && [ "$cycles" -gt 0 ] && [ -n "$live" ] && [ "$live" -lt 65536 ]; then
  pass=$((pass+1))
else
  echo "FAIL[gc] a run that stops storing did not finish its cycles: $out"
  fail=$((fail+1))
fi

if [ "${WARMC_GC_STRESS:-0}" = 0 ]; then

  out=$(log_run fragment 3000 WARMC_GC_RELOC=8)
  pages=$(printf '%s' "$out" | sed -n 's/.*relocation: \([0-9]*\) pages.*/\1/p')
  objs=$(printf '%s' "$out" | sed -n 's/.*pages and \([0-9]*\) objects over the run.*/\1/p')
  if [ -n "$pages" ] && [ "$pages" -gt 0 ] && [ -n "$objs" ] && [ "$objs" -gt 0 ]; then
    pass=$((pass+1))
  else
    echo "FAIL[gc] nothing was ever relocated: $out"
    fail=$((fail+1))
  fi
fi

if [ "${WARMC_GC_STRESS:-0}" = 0 ] && [ "${GC_PAUSE_CHECK:-1}" = 1 ]; then
  pause_check
  publish_check
fi

oom_err="$WORK/oom.$FTAG.$$"
oom_out=$(
  unset WARMC_GC_STRESS WARMC_GC_GROWTH WARMC_GC_MIN_KB WARMC_GC_VERIFY \
        WARMC_GC_RELOC WARMC_GC_RELOCATE_ALL WARMC_GC_LAB WARMC_GC_RELEASE \
        WARMC_GC_HEAP_KB
  export WARMC_GC_HEAP_KB=1024 WARMC_GC_MIN_KB=16 WARMC_GC_STATS=1
  export WARMC_GC_TRAP_ROUND=1

  export WARMC_GC_CONC=0
  wruntimeout $WARMC_RUN "$BIN" --catch \
      oom_fill 1000000 -- oom_throw -- oom_recover 2000 2>"$oom_err" )
oom_rc=$?

oom_traps=$(grep -c '^trap: allocation failed$' "$oom_err")
if [ "$oom_rc" -eq 1 ] && [ "$oom_out" = "i32:1" ] && [ "$oom_traps" -eq 2 ]; then
  pass=$((pass+1))
else
  echo "FAIL[gc] a full heap did not recover after the trap (rc=$oom_rc):"
  echo "$oom_out"; grep '^trap' "$oom_err"
  fail=$((fail+1))
fi

if [ -z "${WARMC_GC_ROUNDS:-}" ]; then
  lost=$(sed -n 's/.*, \([0-9]*\) lost to a trap.*/\1/p' "$oom_err")
  if [ -n "$lost" ] && [ "$lost" -ge 2 ]; then
    pass=$((pass+1))
  else
    echo "FAIL[gc] a trap did not withdraw the round it unwound out of: $lost"
    grep 'rounds:' "$oom_err"
    fail=$((fail+1))
  fi
fi

conc_out=$(
  unset WARMC_GC_STRESS WARMC_GC_GROWTH WARMC_GC_MIN_KB WARMC_GC_VERIFY \
        WARMC_GC_RELOC WARMC_GC_RELOCATE_ALL WARMC_GC_LAB WARMC_GC_RELEASE \
        WARMC_GC_HEAP_KB
  export WARMC_GC_HEAP_KB=1024 WARMC_GC_MIN_KB=16
  wruntimeout $WARMC_RUN "$BIN" --catch \
      oom_fill 1000000 -- oom_throw -- oom_recover 2000 2>"$oom_err" )
conc_rc=$?
conc_traps=$(grep -c '^trap: allocation failed$' "$oom_err")
if [ "$conc_rc" -eq 1 ] && [ "$conc_out" = "i32:1" ] && [ "$conc_traps" -eq 2 ]; then
  pass=$((pass+1))
else
  echo "FAIL[gc] a full heap did not recover with the collector on a thread (rc=$conc_rc):"
  echo "$conc_out"; grep '^trap' "$oom_err"
  fail=$((fail+1))
fi
rm -f "$oom_err"

echo "gc: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
