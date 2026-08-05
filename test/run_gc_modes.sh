#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export BUILD
JOBS=${GC_MODES_JOBS:-1}

fail=0
OUTDIR=
if [ "$JOBS" -gt 1 ]; then
  OUTDIR=$(mktemp -d) || exit 1
  trap 'rm -rf "$OUTDIR"' EXIT
fi
nrun=0

run() {
  local label="$1"; shift
  if [ "$JOBS" -gt 1 ]; then

    nrun=$((nrun+1))
    local slot
    slot=$(printf '%03d' "$nrun")
    {
      local out rc
      out=$(env "$@" ./test/run_gc.sh 2>&1); rc=$?
      if [ $rc -ne 0 ]; then
        { echo "FAIL[gc-modes $label]"; echo "$out"; } > "$OUTDIR/$slot"
        echo x > "$OUTDIR/$slot.fail"
      else
        echo "gc-modes $label: $(printf '%s' "$out" | tail -1)" > "$OUTDIR/$slot"
      fi
    } &
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n 2>/dev/null || break; done
    return
  fi
  local out
  out=$(env "$@" ./test/run_gc.sh 2>&1)
  if [ $? -ne 0 ]; then
    echo "FAIL[gc-modes $label]"
    echo "$out"
    fail=1
  else
    echo "gc-modes $label: $(printf '%s' "$out" | tail -1)"
  fi
}

SEED=${WARMC_GC_JITTER_SEED:-${JITTER_SEED:-1}}

FREQ=WARMC_GC_MIN_KB=64
FREQ_OFF=WARMC_GC_MIN_KB=256

SMALL="GC_N=5000 GC_M=500 GC_L=20"
MEDIUM="GC_N=15000 GC_M=1500 GC_L=40"

run frequent  $FREQ WARMC_FLAGS=-O2

run verified  $FREQ $MEDIUM WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run stress-O2 WARMC_GC_STRESS=1 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2
run stress-O1 WARMC_GC_STRESS=1 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O1
run stress-O0 WARMC_GC_STRESS=1 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O0

run reloc-all-O2 $FREQ $MEDIUM WARMC_GC_RELOC=8 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2
run reloc-all-O0 $FREQ $MEDIUM WARMC_GC_RELOC=8 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O0
run reloc-stress WARMC_GC_STRESS=1 WARMC_GC_RELOC=8 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run reloc-off $FREQ $MEDIUM WARMC_GC_RELOC=0 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run tiny-heap $FREQ $MEDIUM WARMC_GC_HEAP_KB=4096 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run nolab $FREQ $MEDIUM WARMC_GC_LAB=0 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run release $FREQ $MEDIUM WARMC_GC_RELEASE=65536 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run big-trigger $FREQ_OFF $MEDIUM WARMC_GC_GROWTH=2000 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run nogcpass $FREQ $MEDIUM WARMC_GC_VERIFY=1 \
             WARMC_FLAGS="-O2 -fno-labcache -fno-escape -fno-barelide"

run noelide $FREQ $MEDIUM WARMC_GC_VERIFY=1 WARMC_GC_RELOCATE_ALL=1 \
            WARMC_FLAGS="-O2 -fno-barelide"

run stw           $FREQ WARMC_GC_CONC=0 WARMC_FLAGS=-O2
run stw-verified  $FREQ $MEDIUM WARMC_GC_CONC=0 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run conc-tight    WARMC_GC_MIN_KB=4 $MEDIUM WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2
run conc-tight-O0 WARMC_GC_MIN_KB=4 $SMALL WARMC_GC_VERIFY=1 WARMC_FLAGS=-O0

run conc-jitter   WARMC_GC_MIN_KB=4 $SMALL WARMC_GC_JITTER=8 \
                  WARMC_GC_JITTER_SEED=$SEED WARMC_GC_VERIFY=1 \
                  WARMC_FLAGS=-O2

run reloc-conc-jitter WARMC_GC_MIN_KB=4 $SMALL WARMC_GC_RELOC=8 \
                  WARMC_GC_JITTER=8 WARMC_GC_JITTER_SEED=$SEED \
                  WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run reloc-stall   $FREQ $MEDIUM WARMC_GC_RELOC=8 WARMC_GC_RELOC_STALL=3 \
                  WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2
run reloc-stall-stw $FREQ $MEDIUM WARMC_GC_RELOC=8 WARMC_GC_RELOC_STALL=3 \
                  WARMC_GC_CONC=0 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2

run rounds-O2 $FREQ $MEDIUM WARMC_GC_ROUNDS=1 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O2
run rounds-O0 $FREQ $MEDIUM WARMC_GC_ROUNDS=1 WARMC_GC_VERIFY=1 WARMC_FLAGS=-O0

run rounds-jitter $FREQ $SMALL WARMC_GC_ROUNDS=1 WARMC_GC_JITTER=8 \
                  WARMC_GC_JITTER_SEED=$SEED WARMC_FLAGS=-O2

if [ "$JOBS" -gt 1 ]; then
  wait
  for fpath in "$OUTDIR"/*; do
    case "$fpath" in *.fail) fail=1; continue;; esac
    cat "$fpath"
  done
fi

exit $fail
