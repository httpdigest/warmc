#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

if [ ! -x "$WT" ]; then
  echo "run_relaxed: $WT not found (run \`make wasm-tools\`)" >&2
  exit 1
fi

if [ ! -s "$WORK/relaxed.wasm" ] ||
   [ test/cases_relaxed/relaxed.wat -nt "$WORK/relaxed.wasm" ]; then
  "$WT" parse test/cases_relaxed/relaxed.wat -o "$WORK/relaxed.wasm.$$" || exit 1
  mv -f "$WORK/relaxed.wasm.$$" "$WORK/relaxed.wasm"
fi

[ "${1:-}" = --prepare ] && exit 0

pass=0; fail=0
BIN=""

build_one() {
  local lvl="$1" prof="$2" pflag=""
  [ "$prof" = det ] && pflag="--profile=deterministic"
  BIN="$WORK/relaxed_${lvl}_${prof}"
  local dep="$WORK/relaxed.wasm" d
  for d in "./$BUILD/warmc" "$BUILD/warmrt.a" "$BUILD/warmrt-macos.a"; do
    [ -e "$d" ] && [ "$d" -nt "$dep" ] && dep="$d"
  done
  if [ -s "$BIN" ] && [ "$BIN" -nt "$dep" ]; then return; fi
  wtimeout ./$BUILD/warmc "-O$lvl" $pflag $WARMC_TARGET_FLAGS \
    -o "$BIN.$$" "$WORK/relaxed.wasm" ||
    { echo "FAIL[relaxed] cannot compile -O$lvl $prof"; exit 1; }
  mv -f "$BIN.$$" "$BIN"
}

expect_ok() {
  local name="$1" out rc
  out=$(wruntimeout $WARMC_RUN "$BIN" "$name" 2>&1); rc=$?
  if [ "$rc" -eq 0 ]; then
    pass=$((pass+1))
  else
    echo "FAIL[relaxed] $LABEL $name: rc=$rc: $out"
    fail=$((fail+1))
  fi
}

COMMON="swizzle trunc laneselect minmax q15mulr dot dot_add nmadd_operand nan_invalid nan_excluded"

for lvl in 0 1 2; do
  for prof in full det; do
    build_one "$lvl" "$prof"
    LABEL="-O$lvl/$prof"
    for c in $COMMON; do expect_ok "$c"; done

    if [ "$prof" = det ]; then expect_ok madd_unfused; expect_ok nan_det
    else                       expect_ok madd_fused;   expect_ok nan_full; fi
  done
done

cli() {
  local what="$1" want="$2"; shift 2
  wtimeout ./$BUILD/warmc "$@" -c -o "$WORK/relaxed_cli.o" \
    "$WORK/relaxed.wasm" >/dev/null 2>&1
  local rc=$?
  if [ "$rc" -eq "$want" ]; then pass=$((pass+1)); else
    echo "FAIL[relaxed] $what: expected rc=$want, got $rc"; fail=$((fail+1)); fi
}
cli "an unknown profile name is refused" 1 --profile=bogus -O1
cli "--profile=full is accepted"         0 --profile=full -O1
cli "a bare --profile is still the counter" 0 --profile -O1

echo "relaxed: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
