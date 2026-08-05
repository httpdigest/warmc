#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

"$WT" parse test/bench/gcbench.wat -o "$WORK/gcbench.wasm" || exit 1
wtimeout ./$BUILD/warmc -O2 $WARMC_TARGET_FLAGS \
  -o "$WORK/gcbench" "$WORK/gcbench.wasm" || exit 1

run() {
  local label="$1"; shift
  local envs=()
  while [ "$1" != "--" ]; do envs+=("$1"); shift; done
  shift
  local out
  out=$(env WARMC_GC_STATS=1 "${envs[@]}" "$WORK/gcbench" "$@" 2>&1 >/dev/null)
  printf '%-22s %s\n' "$label" "$out"
}

echo "== tree 21 5000000 (4M live nodes) =="
run stw        WARMC_GC_CONC=0 -- tree 21 5000000
run concurrent WARMC_GC_CONC=1 -- tree 21 5000000

echo "== bigtable 1048576 5000000 =="
run stw        WARMC_GC_CONC=0 -- bigtable 1048576 5000000
run concurrent WARMC_GC_CONC=1 -- bigtable 1048576 5000000

BIG=WARMC_GC_HEAP_KB=4194304
echo "== press 8000000 4 (320 MB live) =="
run stw        WARMC_GC_CONC=0 $BIG -- press 8000000 4
run concurrent WARMC_GC_CONC=1 $BIG -- press 8000000 4
run no-assist  WARMC_GC_CONC=1 WARMC_GC_ASSIST=0 $BIG -- press 8000000 4

echo "== coarse 20 65536 3000 (512 KB per allocation) =="
run stw        WARMC_GC_CONC=0 -- coarse 20 65536 3000
run concurrent WARMC_GC_CONC=1 -- coarse 20 65536 3000

PACE="WARMC_GC_MIN_KB=64 WARMC_GC_GROWTH=1"
echo "== churnstore 18 700000 400 =="
run stw        WARMC_GC_CONC=0 $PACE -- churnstore 18 700000 400
run concurrent WARMC_GC_CONC=1 $PACE -- churnstore 18 700000 400
