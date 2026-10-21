#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
export BUILD
LOGS="$BUILD/checklogs"
rm -rf "$LOGS"
mkdir -p "$LOGS"

fail=0

# The prepares are disjoint by construction -- each writes its own generator
# output under its own tag (run_workload.sh keys on module name + clang opt),
# so they fan out like the rows below and the barrier is one wait.
plabels=()
prepare() {
  local label="$1"; shift
  plabels[${#plabels[@]}]="$label"
  ( "$@" >"$LOGS/prepare-$label.log" 2>&1; echo $? >"$LOGS/prepare-$label.rc" ) &
}

prepare cases   ./test/run_tests.sh --prepare
prepare traps   ./test/run_traps.sh --prepare
prepare gc      ./test/run_gc.sh --prepare
prepare relaxed ./test/run_relaxed.sh --prepare
prepare components python3 test/run_components.py --prepare
prepare wit     python3 test/run_wit.py --prepare
prepare wl-O2   env WORKLOAD_CLANG_OPT=-O2 ./test/run_workload.sh --prepare
prepare wl-O0   env WORKLOAD_CLANG_OPT=-O0 ./test/run_workload.sh --prepare
prepare vecwork env WORKLOAD_SRC=test/bench/vecwork.c ./test/run_workload.sh --prepare

for m in addr32 addrmulti addr64 simd control; do
  prepare "$m" env WORKLOAD_SRC=test/bench/$m.wat ./test/run_workload.sh --prepare
done

wait
for label in "${plabels[@]}"; do
  rc=$(cat "$LOGS/prepare-$label.rc" 2>/dev/null || echo 1)
  if [ "$rc" != 0 ]; then
    echo "FAIL[prepare $label]"
    cat "$LOGS/prepare-$label.log"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1

for m in addrmulti addr64; do
  if ! cmp -s "$BUILD/testrun/bench_addr32.ref" "$BUILD/testrun/bench_$m.ref"; then
    echo "FAIL[gen_bench]: $m does not compute what addr32 computes"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1

labels=()

start() {
  local label="$1"; shift
  labels[${#labels[@]}]="$label"

  if [ -n "${CHECK_TIMING:-}" ]; then
    ( t0=$(perl -MTime::HiRes=time -e 'print time')
      "$@" >"$LOGS/$label.log" 2>&1; echo $? >"$LOGS/$label.rc"
      perl -e "printf \"%.1f\", $(perl -MTime::HiRes=time -e 'print time')-$t0" \
        >"$LOGS/$label.sec" ) &
  else
    ( "$@" >"$LOGS/$label.log" 2>&1; echo $? >"$LOGS/$label.rc" ) &
  fi
}

start cases-O0 env WARMC_FLAGS=-O0 ./test/run_tests.sh
start cases-O1 env WARMC_FLAGS=-O1 ./test/run_tests.sh
start cases-O2 env WARMC_VERIFY_RA=1 WARMC_VERIFY_SSA=1 WARMC_FLAGS=-O2 ./test/run_tests.sh

# A second, independent engine over the same corpus; the row skips itself
# politely where wasmtime is not installed.
start cases-wasmtime \
  bash -c '. test/hostenv.sh; WARMC_FLAGS=-O2 python3 test/run_wasmtime.py'

start cases-noinl env WARMC_VERIFY_RA=1 WARMC_FLAGS="-O2 -fno-inline" ./test/run_tests.sh
start gc-noinl    env WARMC_FLAGS="-O2 -fno-inline" ./test/run_gc.sh
start workload-noinl \
  env WARMC_FLAGS="-O2 -fno-inline" WORKLOAD_CLANG_OPT=-O0 ./test/run_workload.sh

for p in mem strength cse loopclose; do
  start "cases-no-$p" env WARMC_FLAGS="-O2 -fno-$p" ./test/run_tests.sh
done

for p in mem cse licm dce fold bce escape partial-escape reinline throw-jump loopclose barelide; do
  start "gc-no-$p" env WARMC_FLAGS="-O2 -fno-$p" ./test/run_gc.sh
done

start nobar     ./test/run_nobar.sh
start jobs      ./test/run_jobs.sh

if [ -z "${GC_MODES_JOBS:-}" ]; then
  _cores=$( (sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null) || echo 4)
  GC_MODES_JOBS=$(( _cores / 2 ))
  [ "$GC_MODES_JOBS" -gt 8 ] && GC_MODES_JOBS=8
  [ "$GC_MODES_JOBS" -lt 1 ] && GC_MODES_JOBS=1
  export GC_MODES_JOBS
fi
start gc-modes ./test/run_gc_modes.sh

start gcthread env GCTHREAD_ROUNDS=50000 ./test/run_gcthread.sh

for lv in -O0 -O1 -O2; do
  start "traps$lv" env WARMC_FLAGS=$lv ./test/run_traps.sh
  start "gc$lv" env WARMC_FLAGS=$lv ./test/run_gc.sh
  start "workload$lv" env WARMC_FLAGS=$lv ./test/run_workload.sh
  start "workload-clang0$lv" \
    env WORKLOAD_CLANG_OPT=-O0 WARMC_FLAGS=$lv ./test/run_workload.sh
  start "vecwork$lv" \
    env WORKLOAD_SRC=test/bench/vecwork.c WARMC_FLAGS=$lv ./test/run_workload.sh
  for m in addr32 addrmulti addr64 simd control; do
    start "$m$lv" env WORKLOAD_SRC=test/bench/$m.wat WARMC_FLAGS=$lv ./test/run_workload.sh
  done
done

for p in cse mem licm branch rotate vectorize loopclose; do
  start "workload-no-$p" \
    env WARMC_FLAGS="-O2 -fno-$p" WORKLOAD_CLANG_OPT=-O0 ./test/run_workload.sh
done

start pressure ./test/run_pressure.sh
start link     ./test/run_link.sh
start macho    ./test/run_macho.sh

start elf      ./test/run_elf.sh
start pe       ./test/run_pe.sh

start tinygo   ./test/run_tinygo.sh

start gobench  python3 test/gobench.py --check
start vecbench python3 test/gobench.py --corpus vecbench --check
start asbench  python3 test/gobench.py --corpus asbench --check
start rsbench  python3 test/gobench.py --corpus rsbench --check
start stdgo    python3 test/gobench.py --corpus stdgo --check
start ktbench  python3 test/gobench.py --corpus ktbench --check
start zigbench python3 test/gobench.py --corpus zigbench --check
start cbench   python3 test/gobench.py --corpus cbench --check
start cppbench python3 test/gobench.py --corpus cppbench --check
start hsbench  python3 test/gobench.py --corpus hsbench --check

start wasi     ./test/run_wasi.sh

start wasi-noexp env WARMC_FLAGS="--no-export-names" ./test/run_wasi.sh
start gowasi   ./test/run_gowasi.sh
start wasip2   ./test/run_wasip2.sh
start gowasip2 ./test/run_gowasip2.sh
start rswasip2 ./test/run_rswasip2.sh
start bincases ./test/run_binary_cases.sh
start relaxed  ./test/run_relaxed.sh
start components python3 test/run_components.py
start wit       python3 test/run_wit.py
start canon     python3 test/run_canon.py
start adapter   python3 test/run_adapter.py

start clink     python3 test/run_clink.py

start wat       python3 test/run_wat.py

start cases-nofold env WARMC_FLAGS="-O2 -fno-fold" ./test/run_tests.sh

start cases-farbr env WARMC_FAR_BRANCH=1 WARMC_FLAGS=-O2 ./test/run_tests.sh
start traps-farbr env WARMC_FAR_BRANCH=1 ./test/run_traps.sh
start gc-farbr    env WARMC_FAR_BRANCH=1 ./test/run_gc.sh

# Three lowerings that had no coverage outside one generated module.  -fgssa
# is a whole pass that nothing ran; the naive allocator and the explicit
# bounds checks were exercised only by run_pressure.sh.  The naive allocator
# gives every value its own slot, so the three widest fixtures in test/cases
# ask for a frame past the addressing limit and it runs on the other two
# suites instead.
start cases-gssa  env WARMC_FLAGS="-O2 -fgssa" ./test/run_tests.sh
start gc-gssa     env WARMC_FLAGS="-O2 -fgssa" ./test/run_gc.sh
start traps-naive env WARMC_FLAGS="-O2 -fno-regalloc" ./test/run_traps.sh
start gc-naive    env WARMC_FLAGS="-O2 -fno-regalloc" ./test/run_gc.sh
start cases-noguard env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_tests.sh
start traps-noguard env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_traps.sh
start gc-noguard    env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_gc.sh

start magic    python3 test/magic.py
start reject   ./test/run_reject.sh
start asan     ./test/run_asan.sh
start tsan     ./test/run_tsan.sh

wait

for label in "${labels[@]}"; do
  rc=$(cat "$LOGS/$label.rc" 2>/dev/null || echo 1)
  printf -- '--- %s\n' "$label"
  cat "$LOGS/$label.log"
  if [ "$rc" != 0 ]; then
    echo "FAIL[$label]: exit $rc"
    fail=1
  fi
done

if [ -n "${CHECK_TIMING:-}" ]; then
  echo "--- timing (slowest first; the run costs the longest job)"
  for label in "${labels[@]}"; do
    printf '%8s  %s\n' "$(cat "$LOGS/$label.sec" 2>/dev/null || echo '?')" "$label"
  done | sort -rn
fi

exit $fail
