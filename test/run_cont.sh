#!/usr/bin/env bash
# Stack switching beyond the proposal's own tests (test/cases_cont/cont.wat):
# what a continuation holds across collections -- in a suspended fiber's
# frames, bound to it, on its way through a suspension, in the heap --
# fibers dropped and freed by the collector, exceptions into and out of
# fibers, traps, a fiber's stack running out, switch, many values through
# every way a value goes, a chain of a hundred fibers.  Each check runs the
# program on one export and compares what it prints, or the trap; the ones
# that hold references across collections run again under the collector's
# test modes: a collection at every allocation, everything relocated and the
# heap checked -- what was freed poisoned, every reference a kept
# continuation holds checked at each mark's end -- and both at once, a mark
# the program runs through, jitter, the collector on the program's thread,
# a small heap.  WARMC_FLAGS is the level.
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

if [ ! -x "$WT" ]; then
  echo "run_cont: $WT not found (run \`make wasm-tools\`)" >&2
  exit 1
fi
if [ ! -s "$WORK/cont.wasm" ] || [ test/cases_cont/cont.wat -nt "$WORK/cont.wasm" ]; then
  "$WT" parse test/cases_cont/cont.wat -o "$WORK/cont.wasm.$$" || exit 1
  mv -f "$WORK/cont.wasm.$$" "$WORK/cont.wasm"
fi
[ "${1:-}" = --prepare ] && exit 0

FTAG=$(wftag)
EXE="$WORK/cont$FTAG"
wbuild "$EXE" ./$BUILD/warmc ${WARMC_FLAGS:-} $WARMC_TARGET_FLAGS "$WORK/cont.wasm" ||
  { echo "FAIL[cont] cannot compile"; exit 1; }

ROWKEY=$(python3 test/testcache.py key --file test/run_cont.sh --file test/hostenv.sh \
  --file "$EXE" --env WARMC_ --env GC_ --str "$*")
if python3 test/testcache.py get row "$ROWKEY"; then exit 0; fi

pass=0; fail=0
GCVARS="WARMC_GC_STRESS WARMC_GC_GROWTH WARMC_GC_MIN_KB WARMC_GC_VERIFY WARMC_GC_RELOC
  WARMC_GC_RELOCATE_ALL WARMC_GC_HEAP_KB WARMC_GC_JITTER WARMC_GC_MARK_STALL WARMC_GC_CONC
  WARMC_CONT_STACK_KB WARMC_GC_STATS"

# the program on EXPORT [ARGS...], the collector's settings MODE's alone
run_mode() {
  local kv
  # shellcheck disable=SC2086
  unset $GCVARS
  export WARMC_GC_MIN_KB=64
  for kv in $MODE; do export "${kv?}"; done
  wruntimeout $WARMC_RUN "$EXE" "$@" 2>&1
}

# expect WANT EXPORT [ARGS...]: WANT is the value printed (i32:N) or the
# trap's message; MODE (an env assignment list) is set around the run
MODE=""
expect() {
  local want="$1"; shift
  local out rc
  out=$(run_mode "$@")
  rc=$?
  case "$want" in
    i32:*)
      if [ "$rc" -eq 0 ] && [ "$out" = "$want" ]; then pass=$((pass+1)); return; fi ;;
    *)
      if [ "$rc" -eq 1 ] && [ "$out" = "trap: $want" ]; then pass=$((pass+1)); return; fi ;;
  esac
  echo "FAIL[cont${MODE:+ $MODE}] $*: expected $want, got rc=$rc: $out"
  fail=$((fail+1))
}

expect i32:45            gen_sum 10
expect i32:0             gen_sum 0
expect i32:285           ask_sum
expect i32:465           bind_gc
expect i32:16            payload_gc
expect i32:130           nested
expect i32:171700        deep_chain 100
expect i32:42            exn_out
expect i32:43            exn_in
expect i32:78            exn_in_out
expect i32:109           throw_fresh
expect i32:42            exn_deep
expect i32:52            exn_then_more
expect i32:1000          switch_pp 1000
expect i32:1             start_wide
expect i32:1             results_wide
expect i32:1             suspend_wide
expect i32:1             result_f64
expect i32:108           result_v128
expect i32:203           two_handlers
expect i32:1781          floats_across
expect i32:1783293664    round_trips 1000000
expect "unreachable executed"          trap_in_fiber
expect "call stack exhausted"          overflow_in_fiber
expect "continuation already consumed" consumed
expect "null continuation reference"   null_cont
expect "unhandled tag"                 unhandled
expect "unhandled tag"                 unhandled_root
expect "null reference"                null_func
expect "continuation already consumed" switch_consumed

# a fiber's stack of 128 KiB: its own limit, not the thread's
MODE="WARMC_CONT_STACK_KB=128"
expect "call stack exhausted" overflow_in_fiber
expect i32:37820              deep_chain 60

for MODE in "" "WARMC_GC_STRESS=1" "WARMC_GC_RELOCATE_ALL=1 WARMC_GC_VERIFY=1" \
    "WARMC_GC_STRESS=1 WARMC_GC_RELOCATE_ALL=1 WARMC_GC_VERIFY=1" \
    "WARMC_GC_MARK_STALL=200" "WARMC_GC_JITTER=64" \
    "WARMC_GC_CONC=0 WARMC_GC_RELOCATE_ALL=1" "WARMC_GC_HEAP_KB=4096"; do
  # a collection at every allocation: fewer rounds, which collect as often
  case "$MODE" in
    *STRESS*) expect i32:20 gc_frames 20; expect i32:8 gc_many 8 6 ;;
    *)        expect i32:200 gc_frames 200; expect i32:40 gc_many 40 30 ;;
  esac
  expect i32:3000   dropped 3000
  expect i32:465    bind_gc
  expect i32:16     payload_gc
  expect i32:465    bind_old
  expect i32:16     payload_old
  expect i32:37820  deep_chain 60
  expect i32:3000   switch_pp 3000
  expect i32:1      suspend_wide
  expect i32:52     exn_then_more
done
MODE=""

# fibers dropped unfinished are freed by the collector: of 3000, all but
# the few the last cycles had not reached
out=$(MODE="WARMC_GC_STATS=1" run_mode dropped 3000)
freed=$(printf '%s' "$out" | sed -n 's/.*continuations: [0-9]* fibers made, \([0-9]*\) freed unreached.*/\1/p')
if [ -n "$freed" ] && [ "$freed" -ge 2900 ]; then pass=$((pass+1)); else
  echo "FAIL[cont] dropped fibers were not freed: $out"; fail=$((fail+1)); fi

echo "cont: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
echo "cont: $pass passed, $fail failed (as when nothing it depends on was as now)" |
  python3 test/testcache.py put row "$ROWKEY"
