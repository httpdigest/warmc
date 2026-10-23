#!/usr/bin/env bash
# -ffma's own harness (test/cases_fma/fma.wat): every export checks its own
# answer, since wasm never fuses and no engine is an oracle for it.
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

if [ ! -x "$WT" ]; then
  echo "run_fma: $WT not found (run \`make wasm-tools\`)" >&2
  exit 1
fi
if [ ! -s "$WORK/fma.wasm" ] || [ test/cases_fma/fma.wat -nt "$WORK/fma.wasm" ]; then
  "$WT" parse test/cases_fma/fma.wat -o "$WORK/fma.wasm.$$" || exit 1
  mv -f "$WORK/fma.wasm.$$" "$WORK/fma.wasm"
fi
[ "${1:-}" = --prepare ] && exit 0

pass=0; fail=0
BIN=""
build_one() {
  BIN="$WORK/fma_$1"
  wtimeout ./$BUILD/warmc $2 $WARMC_TARGET_FLAGS -o "$BIN.$$" "$WORK/fma.wasm" ||
    { echo "FAIL[fma] cannot compile $2"; exit 1; }
  mv -f "$BIN.$$" "$BIN"
}
expect_ok() {
  local name="$1" out rc
  out=$(wruntimeout $WARMC_RUN "$BIN" "$name" 2>&1); rc=$?
  if [ "$rc" -eq 0 ]; then pass=$((pass+1)); else
    echo "FAIL[fma] $LABEL $name: rc=$rc: $out"; fail=$((fail+1)); fi
}

for lvl in 0 1 2; do
  build_one "O$lvl" "-O$lvl"
  LABEL="-O$lvl"
  for c in unfused shared exact; do expect_ok "$c"; done
done
# -O0 runs no passes, so it rounds twice with -ffma too
for lvl in 0 1 2; do
  build_one "O${lvl}_fma" "-O$lvl -ffma"
  LABEL="-O$lvl -ffma"
  if [ "$lvl" = 0 ]; then expect_ok unfused; else expect_ok fused; fi
  for c in shared exact; do expect_ok "$c"; done
done

echo "fma: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
