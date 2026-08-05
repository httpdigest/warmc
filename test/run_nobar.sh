#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}

WORK="$BUILD/nobar"
mkdir -p "$WORK"
same=0
fail=0

shopt -s nullglob
mods=("$BUILD"/gobench/*.wasm "$BUILD"/rsbench/*.wasm "$BUILD"/asbench/*.wasm
      "$BUILD"/zigbench/*.wasm "$BUILD"/vecbench/*.wasm)
if [ ${#mods[@]} -eq 0 ]; then
  echo "nobar: no corpus built; run test/gobench.py first (skipped)"
  exit 0
fi

for w in "${mods[@]}"; do

  if ! "$BUILD/warmc" -O2 -c "$w" -o "$WORK/a.o" 2>"$WORK/err"; then
    echo "FAIL[nobar]: $w does not compile"
    sed 's/^/    /' "$WORK/err"
    fail=$((fail + 1))
    continue
  fi
  if ! "$BUILD/warmc" -O2 -fno-loadbar -c "$w" -o "$WORK/b.o" 2>"$WORK/err"; then
    echo "FAIL[nobar]: $w does not compile without the barrier"
    sed 's/^/    /' "$WORK/err"
    fail=$((fail + 1))
    continue
  fi
  if cmp -s "$WORK/a.o" "$WORK/b.o"; then
    same=$((same + 1))
  else
    echo "FAIL[nobar]: $(basename "$w") differs with and without the load barrier"
    fail=$((fail + 1))
  fi
done

echo "nobar: $same module(s) byte-identical with and without the load barrier, $fail failed"
[ "$fail" -eq 0 ]
