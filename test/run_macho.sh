#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export PATH="/usr/lib/llvm-18/bin:$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/machorun"
mkdir -p "$WORK"

if ! command -v ld64.lld >/dev/null; then
  echo "run_macho: ld64.lld not available, skipping the Darwin backend check"
  exit 0
fi

one() {
  local wasm="$1"
  local name outbin
  name=$(basename "$wasm" .wasm)
  outbin="$WORK/$name"
  # -j1: the row runs a build per CPU, and a build that also took every core
  # for its own threads, beside the rest of make check starting, could miss
  # its timeout on a module it compiles in milliseconds (test/run_jobs.sh
  # shows the thread count never reaches the output)
  if ! WARMC_MACOS_LIBDIR="$PWD/test/sdk" wtimeout ./$BUILD/warmc --target=macos -j1 \
        --rt "$BUILD/warmrt-macos.a" --ld ld64.lld -o "$outbin" "$wasm" \
        2>"$WORK/$name.err"; then
    echo "FAIL[macho] $name: $(head -3 "$WORK/$name.err")" >"$WORK/$name.fail"
    return 1
  fi
  if ! python3 test/check_macho.py --wasm="$wasm" "$outbin" >"$WORK/$name.chk" 2>&1; then
    cp "$WORK/$name.chk" "$WORK/$name.fail"
    return 1
  fi
  return 0
}
export -f one wtimeout
export BUILD WORK WARMC_TIMEOUT WARMC_RUN_TIMEOUT

rm -f "$WORK"/*.fail
total=0
for wasm in "$BUILD"/testrun/*.wasm; do total=$((total+1)); done
jobs=${MACHO_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
ls "$BUILD"/testrun/*.wasm | xargs -P "$jobs" -n1 -I{} bash -c 'one "$@"' _ {}

fail=0
for f in "$WORK"/*.fail; do
  [ -e "$f" ] || continue
  cat "$f"
  fail=$((fail+1))
done
pass=$((total - fail))

echo "macho: $pass module(s) cross-built, linked and verified, $fail failure(s)"
[ "$fail" -eq 0 ]
