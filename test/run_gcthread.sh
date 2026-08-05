#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. ./test/hostenv.sh

WORK="$BUILD/gcthread"
mkdir -p "$WORK"
fail=0

case "$WARMC_TARGET_FLAGS" in
  *macos*) GCTH_RT="$BUILD/warmgcth-macos.a" ;;
  *)       GCTH_RT="$BUILD/warmgcth.a" ;;
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

t0=$(perl -MTime::HiRes=time -e 'print time')
if run_test exit 0 WARMC_GC_ROUNDS=0 WARMC_GC_STATS=1; then
  el=$(perl -e "printf \"%.2f\", $(perl -MTime::HiRes=time -e 'print time')-$t0")

  if [ "$(perl -e "print(($el > 2.0) ? 1 : 0)")" = 1 ]; then
    bad exit "a module that never allocates took ${el}s to leave"
  fi
  grep -q "  rounds: " "$WORK/exit.log" ||
    bad exit "the round driver never ran"
fi

if [ "$fail" -eq 0 ]; then say "5 passed, 0 failed"; else say "failed"; fi
exit $fail
