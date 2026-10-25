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

  if [ -n "${CHECK_TIMING:-}" ]; then
    ( t0=$(perl -MTime::HiRes=time -e 'print time')
      "$@" >"$LOGS/$label.log" 2>&1; rc=$?; echo $rc >"$LOGS/$label.rc"
      perl -e "printf \"%.1f\", $(perl -MTime::HiRes=time -e 'print time')-$t0" \
        >"$LOGS/$label.sec"
      row_done "$label" $rc ) &
  else
    ( "$@" >"$LOGS/$label.log" 2>&1; rc=$?; echo $rc >"$LOGS/$label.rc"
      row_done "$label" $rc ) &
  fi
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

start cases-noinl env WARMC_VERIFY_RA=1 WARMC_FLAGS="-O2 -fno-inline" ./test/run_tests.sh
start gc-noinl    env WARMC_FLAGS="-O2 -fno-inline" ./test/run_gc.sh
start workload-noinl \
  env WARMC_FLAGS="-O2 -fno-inline" WORKLOAD_CLANG_OPT=-O0 ./test/run_workload.sh

for p in mem strength cse loopclose dce; do
  start "cases-no-$p" env WARMC_FLAGS="-O2 -fno-$p" ./test/run_tests.sh
done

for p in mem cse licm dce fold bce escape partial-escape reinline throw-jump loopclose barelide; do
  start "gc-no-$p" env WARMC_FLAGS="-O2 -fno-$p" ./test/run_gc.sh
done

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

for p in cse mem licm branch rotate vectorize loopclose; do
  start "workload-no-$p" \
    env WARMC_FLAGS="-O2 -fno-$p" WORKLOAD_CLANG_OPT=-O0 ./test/run_workload.sh
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

start wasi     ./test/run_wasi.sh

start wasi-noexp env WARMC_FLAGS="--no-export-names" ./test/run_wasi.sh
start gowasi   ./test/run_gowasi.sh
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

start cases-nofold env WARMC_FLAGS="-O2 -fno-fold" ./test/run_tests.sh

start cases-farbr env WARMC_FAR_BRANCH=1 WARMC_FLAGS=-O2 ./test/run_tests.sh
start traps-farbr env WARMC_FAR_BRANCH=1 ./test/run_traps.sh
start gc-farbr    env WARMC_FAR_BRANCH=1 ./test/run_gc.sh

# Three lowerings that had no coverage outside one generated module.
# -fgssa-all is the global promotion -O2 makes only where a loop pays for
# it, applied to every global a function reads and writes; the naive
# allocator and the explicit bounds checks were exercised only by
# run_pressure.sh.  The naive allocator gives every value its own slot, so
# the three widest fixtures in test/cases ask for a frame past the
# addressing limit and it runs on the other two suites instead.  The
# promotion also gets a real program: it once passed every hand-written
# case and returned from TinyGo's runtime with the stack pointer of the
# last call it made.
start cases-gssa  env WARMC_FLAGS="-O2 -fgssa-all" ./test/run_tests.sh
start gc-gssa     env WARMC_FLAGS="-O2 -fgssa-all" ./test/run_gc.sh
bench gobench-gssa python3 test/gobench.py --corpus gobench --check --variant '-O2 -fgssa-all'
start traps-naive env WARMC_FLAGS="-O2 -fno-regalloc" ./test/run_traps.sh
start gc-naive    env WARMC_FLAGS="-O2 -fno-regalloc" ./test/run_gc.sh
start cases-noguard env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_tests.sh
start traps-noguard env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_traps.sh
start gc-noguard    env WARMC_FLAGS="-O2 -fno-guard-pages" ./test/run_gc.sh
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
start trapstate python3 test/run_trapstate.py
start reject   ./test/run_reject.sh
start asan     ./test/run_asan.sh
start tsan     ./test/run_tsan.sh

printf '[%4ds] %d rows started\n' "$SECONDS" "${#labels[@]}"
watch_rows &
wait

# Whether the collector keeps up while the program answers mark end, asked
# of a machine no row is loading any more.
if [ -z "${CHECK_SMOKE:-}" ]; then
  start gc-pause env GC_PAUSE_CHECK=only WARMC_FLAGS=-O2 ./test/run_gc.sh
  wait
  # gosp.c: the modules in Go's shape that rule its rewrite out, and random
  # ones it makes, against wasm-interp; tramp.c: the modules in GHC's shape
  # that rule its rewrite out or need more of it, at -O0 and -O2.  Also
  # after the others: they link and run some five hundred fresh binaries,
  # and macOS checks a fresh binary's signature on its first run one at a
  # time across the machine (test/run_cases.py), which beside the other
  # rows starves their runs.
  start gosp python3 test/run_gosp.py
  # Programs on a 64-bit memory past 4 GiB, at each level against -O0 and
  # wasmtime: four hundred fresh binaries, so here too.
  start mem64 python3 test/gen_mem64.py --seed 1 --count 100
  start tramp python3 test/run_tramp.py
  # The Mach-O row builds a module per CPU, each with a 6 s limit, which the
  # first half minute of the rows above, when all of them start at once,
  # pushed past it; here it takes a second or two.
  start macho ./test/run_macho.sh
  wait
  # -fexact-trap over programs in Go's, C's, Rust's and AssemblyScript's
  # shape.  Each compiles a whole corpus, a module per CPU; with the rows
  # above, which all start at once, they pushed the cases rows' compiles
  # past their 6 s.
  for c in stdgo cbench rsbench asbench; do
    bench "$c-exacttrap" python3 test/gobench.py --corpus "$c" --check --variant '-O2 -fexact-trap'
  done
  wait
  # The official testsuite, as make check-spec runs it: every malformed and
  # invalid module refused, and every assertion of the rest held -- the
  # bounds, traps and type checks a module has no way round.  Alone, after
  # the rest: it links and runs some 750 fresh binaries.
  start spec python3 test/spec/run.py "--levels=${SPEC_LEVELS:--O1,-O2,-O2 --profile=deterministic}"
  wait
  # Every pair of passes, off and on in all four combinations, over the
  # cases, traps and the collector: nine settings (make check-combos does
  # triples).  Last and alone, since each setting runs those suites on
  # every core, which beside the other rows pushes their compiles past
  # their timeouts.
  start combos python3 test/run_combos.py
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
  echo "--- timing (slowest first; the run costs the longest job)"
  for label in "${labels[@]}"; do
    printf '%8s  %s\n' "$(cat "$LOGS/$label.sec" 2>/dev/null || echo '?')" "$label"
  done | sort -rn
fi

exit $fail
