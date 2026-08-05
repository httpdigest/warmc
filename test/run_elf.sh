#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/elfrun"
mkdir -p "$WORK"

one() {
  local wasm="$1" name
  name=$(basename "$wasm" .wasm)
  if ! python3 test/check_elf.py "./$BUILD/warmc" "$wasm" "$BUILD/warmrt.a" \
       >"$WORK/$name.chk" 2>&1; then
    { echo "FAIL[elf] $name:"; sed 's/^/    /' "$WORK/$name.chk"; } >"$WORK/$name.fail"
    return 1
  fi
  return 0
}
export -f one
export BUILD WORK

rm -f "$WORK"/*.fail
total=0
for wasm in "$BUILD"/testrun/*.wasm; do total=$((total+1)); done
jobs=${ELF_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
ls "$BUILD"/testrun/*.wasm | xargs -P "$jobs" -n1 -I{} bash -c 'one "$@"' _ {}

fail=0
for f in "$WORK"/*.fail; do
  [ -e "$f" ] || continue
  cat "$f"
  fail=$((fail+1))
done
pass=$((total - fail))

echo "elf: $pass module(s) cross-built, linked and verified, $fail failure(s)"
[ "$fail" -eq 0 ]
