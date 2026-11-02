#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. ./test/hostenv.sh

WORK="$BUILD/gcthread"
mkdir -p "$WORK"
fail=0

case "$WARMC_TARGET_FLAGS" in
  *macos*)   GCTH_RT="$BUILD/warmgcth-macos.a" ;;
  *windows*) GCTH_RT="$BUILD/warmgcth-windows.a" ;;
  *)         GCTH_RT="$BUILD/warmgcth.a" ;;
esac
if [ ! -s "$GCTH_RT" ]; then
  echo "FAIL[gcthread]: $GCTH_RT not built (run \`make\`)"
  exit 1
fi

printf '(module (func (export "nop")))\n' > "$WORK/nop.wat"
if ! "$BUILD/warmc" -O2 $WARMC_TARGET_FLAGS --rt "$GCTH_RT" \
     -o "$WORK/nop" "$WORK/nop.wat" 2>"$WORK/link.err"; then
  echo "FAIL[gcthread]: cannot link a module against the runtime"
  sed 's/^/    /' "$WORK/link.err"
  exit 1
fi

say() { echo "gcthread $1"; }
bad() { echo "FAIL[gcthread/$1]: $2"; fail=1; }

run_test() {
  local label="$1" want="$2"; shift 2
  local out rc
  out=$(env "$@" $WARMC_RUN "$WORK/nop" nop 2>&1); rc=$?
  printf '%s\n' "$out" > "$WORK/$label.log"
  if [ "$rc" -ne "$want" ]; then
    bad "$label" "exited $rc, expected $want"
    sed 's/^/    /' "$WORK/$label.log"
    return 1
  fi
  return 0
}

N=${GCTHREAD_ROUNDS:-1000000}
if run_test pingpong 0 WARMC_GC_THREADTEST="$N"; then

  parked=$(sed -n 's/.*round trips parked:.*, \([0-9]*\) into the kernel/\1/p' \
           "$WORK/pingpong.log")
  grep -q 'gcth: ok$' "$WORK/pingpong.log" ||
    bad pingpong "the run did not finish"
  if [ -z "$parked" ]; then
    bad pingpong "no parked phase in the output"
  elif [ "$parked" -lt "$((N / 10))" ]; then

    bad pingpong "only $parked waits reached the kernel with the spin off"
  fi
  sed 's/^/  /' "$WORK/pingpong.log"
fi

if run_test die 1 WARMC_GC_THREADTEST=1000 WARMC_GC_THREADTEST_DIE=1; then
  grep -q '^warmc-rt: the collector.s thread reached a state' "$WORK/die.log" ||
    bad die "died without saying so: $(tail -1 "$WORK/die.log")"
fi

if run_test fault 139 WARMC_GC_THREADTEST=1000 WARMC_GC_THREADTEST_FAULT=1; then
  grep -q "^warmc-rt: fault on the collector.s thread" "$WORK/fault.log" ||
    bad fault "faulted as if it were the mutator: $(tail -1 "$WORK/fault.log")"
fi

M=$((N / 25)); [ "$M" -lt 1000 ] && M=1000
if run_test nospin 0 WARMC_GC_THREADTEST="$M" WARMC_GC_THREAD_SPIN=0; then
  k=$(sed -n 's/.*round trips spinning:.*, \([0-9]*\) into the kernel/\1/p' \
      "$WORK/nospin.log")
  [ -n "$k" ] && [ "$k" -ge "$M" ] ||
    bad nospin "WARMC_GC_THREAD_SPIN=0 reached the kernel ${k:-no} times in $M"
fi

# Leaving with the round driver running.  gcth_stop waits for the driver at
# most 50 ms and counts an exit it outlasted, and that count is the measure
# rather than the launch's wall time: on a Windows runner starting fifty rows
# at once two launches of this module differ by more than 2 s with or without
# the driver.  What is tested is that the driver hears the stop, not how soon
# a loaded machine runs it -- a runner with a row on every core did not in
# 50 ms, three times running -- so it is given 5 s here; one that does not
# hear the stop outlasts that too.
if run_test exit 0 WARMC_GC_ROUNDS=0 WARMC_GC_STATS=1 WARMC_GC_STOP_WAIT_MS=5000; then
  if ! grep -q "  rounds: " "$WORK/exit.log"; then
    bad exit "the round driver never ran"
  elif ! grep -q ", 0 exits the collector outlasted" "$WORK/exit.log"; then
    bad exit "the collector outlasted the exit: $(grep '  rounds: ' "$WORK/exit.log")"
  fi
fi

# Where a page of one granule goes (rt.c gc_gran_take_hole): into the
# shortest hole that a medium page could not use, and only then into the
# first free granule, of a run of eight that a medium page could -- in a
# heap of 24 granules laid out by hand, page after page.
if run_test place 0 WARMC_GC_PLACETEST=1; then
  grep -q '^gcplace: ok$' "$WORK/place.log" ||
    bad place "did not finish: $(tail -1 "$WORK/place.log")"
fi

if [ "$fail" -eq 0 ]; then say "6 passed, 0 failed"; else say "failed"; fi
exit $fail
