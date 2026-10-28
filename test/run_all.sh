#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
export BUILD
LOGS="$BUILD/checklogs"
rm -rf "$LOGS"
mkdir -p "$LOGS"

# The last run's trim of the caches, still unlinking (see the end): a change
# to warmc leaves gigabytes of programs it no longer makes, and taking them
# away while the rows compile starved those of the disk -- a module that
# compiles in a fraction of a second timed out at thirty, in four rows at
# once, when three runs followed one another within minutes.
TRIM_PID="$BUILD/.cache-trim.pid"
if [ -f "$TRIM_PID" ] && kill -0 "$(cat "$TRIM_PID")" 2>/dev/null; then
  echo "waiting for the last run's cache trim to finish"
  while kill -0 "$(cat "$TRIM_PID")" 2>/dev/null; do sleep 1; done
fi

# A compile's limit (test/hostenv.sh's wtimeout): six seconds alone, where
# the slowest of the suites' modules takes a fifth of one, but every row
# starts at once here, and in that first half minute a compile of a tenth of
# a second has taken more than six.  Thirty still ends a hang.
export WARMC_TIMEOUT=${WARMC_TIMEOUT:-30}

# What a compile made, kept under what it made it of (src/main.c's
# WARMC_CACHE): the rows compile the same modules the same way run after
# run.  At most WARMC_CACHE_MB of it (20 GB: a change to warmc makes every
# corpus's programs anew), the least recently used going first, taken away
# at the end; test/testcache.py keeps the suites' own results beside it,
# under the same rule.
export WARMC_CACHE=${WARMC_CACHE:-$PWD/$BUILD/warmc-cache}
# wasmtime, which a dozen suites compare with, answered from test/testcache.py
# where it was asked the same before (test/shims/wasmtime), and so are
# tinygo's, go's and rustc's builds (test/shims/buildcache.py); a row reads
# no stdin of make's, which the wasmtime shim would wait on.  The shims are
# run by wrappers made here, in $BUILD/shims, one for each tool there is,
# naming python3, the shim and the tool by absolute path: a suite that runs
# one under `env -i` leaves it no PATH to find them on, and CI's wasmtime is
# under build/ci.  What wasmtime answers there must be what it answers with a
# PATH, or every suite comparing with it would fail.  (Not on Windows, where
# wasmtime is wasmtime.exe and a shim no program.)
case "$(uname -s)" in
  MINGW*|CLANG*|MSYS*) ;;
  *)
    SHIMS="$PWD/$BUILD/shims"
    rm -rf "$SHIMS"; mkdir -p "$SHIMS"
    noshims=$(printf '%s' "$PATH" | tr ':' '\n' | grep -vxF -e "$PWD/test/shims" -e "$SHIMS" |
              paste -sd: -)
    py=$(PATH="$noshims" command -v python3)
    for t in wasmtime tinygo go rustc; do
      tool=$(PATH="$noshims" command -v "$t") || continue
      printf '#!/bin/sh\nWARMC_SHIM_REAL=%s exec %s %s "$@"\n' "'$tool'" "'$py'" \
        "'$PWD/test/shims/$t'" >"$SHIMS/$t"
      chmod +x "$SHIMS/$t"
    done
    export PATH="$SHIMS:$PATH"
    if [ -x "$SHIMS/wasmtime" ] &&
       [ "$(env -i "$SHIMS/wasmtime" --version 2>&1)" != "$(PATH="$noshims" wasmtime --version 2>&1)" ]; then
      echo "FAIL[shims]: wasmtime under env -i is not wasmtime: $(env -i "$SHIMS/wasmtime" --version 2>&1)"
      exit 1
    fi
    ;;
esac

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
prepare fma     ./test/run_fma.sh --prepare
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

# what every row's answer depends on but its own command and settings
# (test/row.py base): warmc, the runtimes, the pinned tools, the tree outside
# src/, the toolchains on PATH
CHECK_ROW_BASE=""
[ -n "${WARMC_NO_TESTCACHE:-}" ] || CHECK_ROW_BASE=$(python3 test/row.py base)
export CHECK_ROW_BASE

# Left to the end, run on its own: see pause_check in run_gc.sh.
export GC_PAUSE_CHECK=0

# `make smoke` (CHECK_SMOKE=1) runs only these: between them the hand-written
# cases at every level, traps, the collector, relaxed SIMD, the C, SIMD,
# control-flow and addressing workloads, WASI, linking, rejection, register
# pressure, far branches, the canonical ABI, component linking and shared
# libraries -- most of warmc, in under 30 CPU-seconds of a workstation and
# well under a minute of a CI runner, against the fifteen minutes the whole
# of this takes there.  CI runs it on every push.  adapter and wit are left
# out: their wasmtime runs and subprocesses cost a minute and more of a macOS
# or Windows runner.
SMOKE_ROWS="cases-O0 cases-O1 cases-O2 traps-O0 traps-O2 gc-O2 gcthread relaxed
  workload-O2 vecwork-O2 simd-O2 control-O2 addr32-O2 addrmulti-O2 addr64-O2
  wasi link reject pressure bincases canon clink shared"
# Under MSYS2 every process costs a tenth of a second to start, and reject
# starts warmc two hundred times to ask what no host can answer differently
# -- whether the front end refuses a malformed module, and why -- so on
# Windows it is left to the other two hosts' smoke runs and the full suite.
case "$(uname -s)" in
  MINGW*|CLANG*|MSYS*) SMOKE_ROWS=${SMOKE_ROWS/reject/} ;;
esac
SMOKE_ROWS=" $(echo $SMOKE_ROWS) "

# The rows' output is held back and printed in order once they have all
# finished, so a row says when it is done, at once -- with the seconds since
# the run began, which a subshell's $SECONDS still counts -- and one that
# failed shows the start of its log with it, in one write.
row_done() {
  if [ "$2" = 0 ]; then printf '[%4ds] %s: ok\n' "$SECONDS" "$1"
  else printf '[%4ds] %s: exit %s\n%s\n' "$SECONDS" "$1" "$2" \
         "$(head -n 12 "$LOGS/$1.log" | sed 's/^/    /')"
  fi
}

start() {
  local label="$1"; shift
  if [ -n "${CHECK_SMOKE:-}" ]; then
    case "$SMOKE_ROWS" in *" $label "*) ;; *) return 0 ;; esac
  fi
  labels[${#labels[@]}]="$label"

  # through test/row.py, which answers a row that passed before with nothing
  # it depends on changed, and keeps its CPU time, its descendants'
  # included, for CHECK_TIMING: on a machine every row loads, a row's wall
  # time says more about the others than itself
  ( [ -n "${CHECK_TIMING:-}" ] && t0=$(perl -MTime::HiRes=time -e 'print time')
    python3 test/row.py run "$label" "${CHECK_TIMING:+$LOGS/$label.cpu}" -- "$@" \
      </dev/null >"$LOGS/$label.log" 2>&1
    rc=$?; echo $rc >"$LOGS/$label.rc"
    [ -n "${CHECK_TIMING:-}" ] &&
      perl -e "printf \"%.1f\", $(perl -MTime::HiRes=time -e 'print time')-$t0" \
        >"$LOGS/$label.sec"
    row_done "$label" $rc ) &
}

# Every half minute, how far the run has got, and once few rows are left,
# which: a row that wedges is then the one still named at the end.  It looks
# every second, so that it is gone by the time the last row is.
watch_rows() {
  local tick=0 n l running
  while :; do
    sleep 1
    n=0; running=""
    for l in "${labels[@]}"; do
      if [ -e "$LOGS/$l.rc" ]; then n=$((n + 1)); else running="$running $l"; fi
    done
    [ -n "$running" ] || return 0
    tick=$((tick + 1))
    [ $((tick % 30)) = 0 ] || continue
    if [ $((${#labels[@]} - n)) -le 15 ]; then
      printf '[%4ds] %d of %d rows done, waiting for:%s\n' "$SECONDS" "$n" "${#labels[@]}" "$running"
    else
      printf '[%4ds] %d of %d rows done\n' "$SECONDS" "$n" "${#labels[@]}"
    fi
  done
}

start cases-O0 env WARMC_FLAGS=-O0 ./test/run_tests.sh
start cases-O1 env WARMC_FLAGS=-O1 ./test/run_tests.sh
start cases-O2 env WARMC_VERIFY_RA=1 WARMC_VERIFY_SSA=1 WARMC_FLAGS=-O2 ./test/run_tests.sh

# A second, independent engine over the same corpus; the row skips itself
# politely where wasmtime is not installed.
start cases-wasmtime \
  bash -c '. test/hostenv.sh; WARMC_FLAGS=-O2 python3 test/run_wasmtime.py'

# No row turns a single pass off: test/run_combos.py, after the rows, runs
# the cases, traps, GC tests and the C workload at nine settings in which
# every pass is off beside every other pass both on and off.

# The benchmark corpora -- the Makefile's BENCH_CORPORA -- are programs
# sized to be timed, and building and running them is most of a run on a
# small machine; CHECK_BENCH=0, which CI sets, leaves them to `make
# bench-compare`, with the rows that compile nothing else.
bench() { [ "${CHECK_BENCH:-1}" = 0 ] || start "$@"; }

bench nobar     ./test/run_nobar.sh
bench jobs      ./test/run_jobs.sh

if [ -z "${GC_MODES_JOBS:-}" ]; then
  _cores=$( (sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null) || echo 4)
  GC_MODES_JOBS=$(( _cores / 2 ))
  [ "$GC_MODES_JOBS" -gt 8 ] && GC_MODES_JOBS=8
  [ "$GC_MODES_JOBS" -lt 1 ] && GC_MODES_JOBS=1
  export GC_MODES_JOBS
fi
start gc-modes ./test/run_gc_modes.sh

start gcthread env GCTHREAD_ROUNDS=50000 ./test/run_gcthread.sh

# -O3, in the rows whose answers do not depend on how a float rounds: fma
# fuses a multiply into the add that reads it, which every row comparing
# float results with another engine would see
start traps-O3 env WARMC_FLAGS=-O3 ./test/run_traps.sh
start gc-O3    env WARMC_FLAGS=-O3 ./test/run_gc.sh

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

start pressure ./test/run_pressure.sh
start link     ./test/run_link.sh
# functions' symbols named as the name section names them, in each object
# format, and nothing that refers to one by its name
start names    python3 test/run_names.py
# the WASI adapter for TeaVM's modules, on modules shaped as TeaVM writes
# them, and the fdlibm it calls for Math against the JVM's StrictMath
start teavm    python3 test/run_teavm.py
# --native-module: imports called as C functions, on the host's own target
start native   python3 test/run_native.py
start shared   ./test/run_shared.sh

start elf      ./test/run_elf.sh
start pe       ./test/run_pe.sh

start tinygo   ./test/run_tinygo.sh

bench gobench  python3 test/gobench.py --check
bench vecbench python3 test/gobench.py --corpus vecbench --check
bench asbench  python3 test/gobench.py --corpus asbench --check
bench rsbench  python3 test/gobench.py --corpus rsbench --check
bench stdgo    python3 test/gobench.py --corpus stdgo --check
bench ktbench  python3 test/gobench.py --corpus ktbench --check
bench zigbench python3 test/gobench.py --corpus zigbench --check
bench cbench   python3 test/gobench.py --corpus cbench --check
bench cppbench python3 test/gobench.py --corpus cppbench --check
bench hsbench  python3 test/gobench.py --corpus hsbench --check
bench javabench python3 test/gobench.py --corpus javabench --check

# The language test corpora: programs that print what they compute, line by
# line, rather than a checksum, so a miscompile shows where it starts; each is
# compared with wasmtime at every optimisation level, and with -fdead-frames.
# Not benchmarks, so not in the Makefile's BENCH_CORPORA.
for c in ctests cpptests rstests gotests stdgotests astests kttests zigtests hstests javatests; do
  start "$c" python3 test/gobench.py --corpus "$c" --check \
    --variant -O0 --variant -O1 --variant -O2 --variant '-O2 -fdead-frames'
done

# Go's resume dispatch with the jumps back to it left in place, which jump
# threading otherwise takes away before pass_go_resume runs: its cases for
# the constants they pass must stay.
start stdgotests-nothread python3 test/gobench.py --corpus stdgotests --check \
  --variant '-O2 -fno-thread'

start wasi     ./test/run_wasi.sh

start wasi-noexp env WARMC_FLAGS="--no-export-names" ./test/run_wasi.sh
start gowasi   ./test/run_gowasi.sh
# Go's net/http server, on a listener and a directory preopened
start gohttp   python3 test/run_gohttp.py
start wasip2   ./test/run_wasip2.sh
start gowasip2 ./test/run_gowasip2.sh
start rswasip2 ./test/run_rswasip2.sh
start bincases ./test/run_binary_cases.sh
start relaxed  ./test/run_relaxed.sh
start fma      ./test/run_fma.sh
start components python3 test/run_components.py
start wit       python3 test/run_wit.py
start canon     python3 test/run_canon.py
start adapter   python3 test/run_adapter.py

start clink     python3 test/run_clink.py
start levels    ./test/run_levels.sh
start customs   python3 test/run_customs.py

start wat       python3 test/run_wat.py

# The active data segments as src/datapack.c packs them: its compressor and
# rt/lzunpack.inc's decoder on their own, random segments against
# wasm-interp, and the cases with blocks of 64 bytes, whose ends fall where
# their data is (with no compression at all they are run_combos')
start datapack   ./build/datapack_test
start datapack-gen python3 test/gen_datapack.py --seed 1 --count 60
start cases-packsmall env WARMC_PACK_BLOCK_LOG2=6 WARMC_FLAGS=-O2 ./test/run_tests.sh
# br_table's halfword entries reaching 8 instructions: most functions with a
# table are made again, their branches far and their entries words
start cases-table16 env WARMC_TABLE16_REACH=8 WARMC_FLAGS=-O2 ./test/run_tests.sh
start cases-table16-stubs env WARMC_NO_DIRECT_BRTABLE=1 WARMC_TABLE16_REACH=8 \
  WARMC_FLAGS=-O2 ./test/run_tests.sh
# and tables of 70000 entries, which halfwords cannot reach past
start bigtable python3 test/run_bigtable.py

start cases-farbr env WARMC_FAR_BRANCH=1 WARMC_FLAGS=-O2 ./test/run_tests.sh
start traps-farbr env WARMC_FAR_BRANCH=1 ./test/run_traps.sh
start gc-farbr    env WARMC_FAR_BRANCH=1 ./test/run_gc.sh

# Without the inline memory.copy and memory.fill code every one is
# warmc_memmove_r's or warmc_memset_r's, or the runtime's, and nothing
# inline needs the memory's base for them (memop_r.wat).
start cases-nosmallmemop env WARMC_NO_SMALL_MEMOP=1 WARMC_FLAGS=-O2 ./test/run_tests.sh

# -fgssa-all is the global promotion -O2 makes only where a loop pays for
# it, applied to every global a function reads and writes; it is no pass of
# its own, so not run_combos'.  It also gets a real program: it once passed
# every hand-written case and returned from TinyGo's runtime with the stack
# pointer of the last call it made.
start cases-gssa  env WARMC_FLAGS="-O2 -fgssa-all" ./test/run_tests.sh
start gc-gssa     env WARMC_FLAGS="-O2 -fgssa-all" ./test/run_gc.sh
bench gobench-gssa python3 test/gobench.py --corpus gobench --check --variant '-O2 -fgssa-all'
# -fdead-frames removes stores into a frame that nothing reads again, so the
# hand-written cases run with it too (deadframes.wat holds its shapes, and
# deadframes_above.wat a stack it must leave alone), and so do the corpora
# built on C's stack, whose output under it is wasmtime's.
start cases-deadframes env WARMC_FLAGS="-O2 -fdead-frames" ./test/run_tests.sh
for c in cbench cppbench rsbench zigbench; do
  bench "$c-deadframes" python3 test/gobench.py --corpus "$c" --check --variant '-O2 -fdead-frames'
done

# -fexact-trap: every hand-written case, trap and collection with what it
# keeps exact (four corpora have it too, after the rows here)
start cases-exacttrap env WARMC_FLAGS="-O2 -fexact-trap" ./test/run_tests.sh
start traps-exacttrap env WARMC_FLAGS="-O2 -fexact-trap" ./test/run_traps.sh
start gc-exacttrap    env WARMC_FLAGS="-O2 -fexact-trap" ./test/run_gc.sh

start magic    python3 test/magic.py
start optcases python3 test/run_optcases.py
# random shapes of the optimiser's folds and their near misses -- bit
# reversals, float computations of integers truncated and compared,
# TinyGo's select tree round a truncation -- against wasm-interp
start folds    python3 test/gen_folds.py --seed 1 --count 40
start trapstate python3 test/run_trapstate.py
start reject   ./test/run_reject.sh
start asan     ./test/run_asan.sh
start tsan     ./test/run_tsan.sh

# gosp.c: the modules in Go's shape that rule its rewrite out, and random
# ones it makes, against wasm-interp; tramp.c: the modules in GHC's shape
# that rule its rewrite out or need more of it, at -O0 and -O2.  Programs on
# a 64-bit memory past 4 GiB, at each level against -O0 and wasmtime.  The
# Mach-O row's modules.  These ran after the other rows, alone, while each
# of their modules was a fresh program of its own, which macOS checks on its
# first run one at a time across the machine, and while a compile had six
# seconds; now their modules share a few programs, and a compile has thirty.
start gosp  python3 test/run_gosp.py
start mem64 python3 test/gen_mem64.py --seed 1 --count 100
start tramp python3 test/run_tramp.py
start macho ./test/run_macho.sh
# -fexact-trap over programs in Go's, C's, Rust's and AssemblyScript's shape
for c in stdgo cbench rsbench asbench; do
  bench "$c-exacttrap" python3 test/gobench.py --corpus "$c" --check --variant '-O2 -fexact-trap'
done
# The official testsuite, as make check-spec runs it: every malformed and
# invalid module refused, and every assertion of the rest held -- the
# bounds, traps and type checks a module has no way round.
start spec python3 test/spec/run.py "--levels=${SPEC_LEVELS:--O1,-O2,-O2 --profile=deterministic}"
# Every pair of passes, off and on in all four combinations, over the
# cases, traps, the collector and the C workload: nine settings (make
# check-combos does triples), a quarter of the cores' worth at a time.
start combos python3 test/run_combos.py

printf '[%4ds] %d rows started\n' "$SECONDS" "${#labels[@]}"
watch_rows &
wait

# Whether the collector keeps up while the program answers mark end, asked
# of a machine no row is loading any more.
if [ -z "${CHECK_SMOKE:-}" ]; then
  start gc-pause env GC_PAUSE_CHECK=only WARMC_FLAGS=-O2 ./test/run_gc.sh
  wait
fi

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
  echo "--- timing (wall and CPU seconds, slowest first; the run costs the longest job)"
  for label in "${labels[@]}"; do
    printf '%8s %8s  %s\n' "$(cat "$LOGS/$label.sec" 2>/dev/null || echo '?')" \
      "$(cat "$LOGS/$label.cpu" 2>/dev/null || echo '?')" "$label"
  done | sort -rn
fi

# Each cache cut to its size where a run took it past, the least recently
# used first: after the rows, and on its own, since taking away tens of
# thousands of entries -- gigabytes after a change to warmc -- is minutes
# of unlinking make check need not wait on; the next run does, at its start.
# A suite run meanwhile on its own reads an entry whole or not at all (warmc
# and test/testcache.py hold one open before using it).  Its output goes
# nowhere, so a pipe from make check ends with make check.
( python3 test/testcache.py trim
  python3 test/testcache.py trim "$WARMC_CACHE" "${WARMC_CACHE_MB:-20000}" 2
) </dev/null >/dev/null 2>&1 &
echo $! >"$TRIM_PID"

exit $fail
