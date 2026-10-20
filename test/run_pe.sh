#!/usr/bin/env bash
# The PE twin of run_elf.sh and run_macho.sh: cross-link every cases module
# for Windows and read the image back.  Structural only, so it runs on any
# host -- `make` builds warmrt-windows.a everywhere.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/perun"
mkdir -p "$WORK"

one() {
  local wasm="$1" name
  name=$(basename "$wasm" .wasm)
  if ! python3 test/check_pe.py "./$BUILD/warmc" "$wasm" "$BUILD/warmrt-windows.a" \
       >"$WORK/$name.chk" 2>&1; then
    { echo "FAIL[pe] $name:"; sed 's/^/    /' "$WORK/$name.chk"; } >"$WORK/$name.fail"
    return 1
  fi
  return 0
}
export -f one
export BUILD WORK

rm -f "$WORK"/*.fail
total=0
for wasm in "$BUILD"/testrun/*.wasm; do total=$((total+1)); done
jobs=${PE_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
ls "$BUILD"/testrun/*.wasm | xargs -P "$jobs" -I{} bash -c 'one "$@"' _ {}

fail=0
for f in "$WORK"/*.fail; do
  [ -e "$f" ] || continue
  cat "$f"
  fail=$((fail+1))
done
pass=$((total - fail))

echo "pe: $pass module(s) cross-built, linked and verified, $fail failure(s)"
[ "$fail" -eq 0 ]
