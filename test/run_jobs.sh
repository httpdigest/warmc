#!/usr/bin/env bash

# -j chooses the worker count, and the worker count must not reach the output.
# Threads change how the work is partitioned, which order the objects are
# emitted in, and which allocations each thread makes, so a shared structure
# that is not as private as it looks shows up here as an object that depends
# on the width.  It does not perturb the contents of a recycled block, so it
# is not a test for reading memory nobody wrote: that one wants an allocator
# that poisons what it hands back, and MallocScribble no longer reaches far
# enough, because xfree keeps most blocks on its own free list.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}

WORK="$BUILD/testrun/jobs"
mkdir -p "$WORK"
fail=0

check() {
  local want=$1 desc=$2; shift 2
  "$BUILD/warmc" "$@" >/dev/null 2>"$WORK/err"
  local rc=$?
  if [ "$rc" != "$want" ]; then
    echo "FAIL[jobs]: $desc: want exit $want, got $rc: $(head -1 "$WORK/err")"
    fail=$((fail + 1))
  fi
}

shopt -s nullglob
mods=("$BUILD"/gobench/*.wasm "$BUILD"/rsbench/*.wasm "$BUILD"/asbench/*.wasm
      "$BUILD"/zigbench/*.wasm "$BUILD"/vecbench/*.wasm)
if [ ${#mods[@]} -eq 0 ]; then
  echo "jobs: no corpus built; run test/gobench.py first (skipped)"
  exit 0
fi
one="${mods[0]}"

for form in -j1 -j4 -j16; do
  check 0 "$form is accepted" -O2 "$form" -c -o "$WORK/a.o" "$one"
done
check 0 "-j N is accepted" -O2 -j 4 -c -o "$WORK/a.o" "$one"

for bad in -j0 -j -j-2 -jx; do
  check 1 "$bad is refused" -O2 "$bad" -c -o "$WORK/a.o" "$one"
done
check 1 "-j 0 is refused" -O2 -j 0 -c -o "$WORK/a.o" "$one"

if ! grep -qF -- "worker count of 1 or more" "$WORK/err"; then
  echo "FAIL[jobs]: the refusal does not say what -j wants"
  fail=$((fail + 1))
fi

widths="1 2 3 5 8 16"
first=${widths%% *}
agree=0
for w in "${mods[@]}"; do
  ref=""
  ok=1
  for j in $widths; do
    if ! "$BUILD/warmc" -O2 -j"$j" -c "$w" -o "$WORK/j$j.o" 2>"$WORK/err"; then
      echo "FAIL[jobs]: $(basename "$w") does not compile at -j$j"
      sed 's/^/    /' "$WORK/err"
      fail=$((fail + 1))
      ok=0
      break
    fi
    if [ -z "$ref" ]; then
      ref="$WORK/j$j.o"
    elif ! cmp -s "$ref" "$WORK/j$j.o"; then
      # both kept, to be told apart function by function: once, under make
      # check's load, a module differed at every -j from 3 up and never again
      keep="$BUILD/checklogs/jobs"
      mkdir -p "$keep"
      cp "$ref" "$keep/$(basename "$w" .wasm)-j$first.o"
      cp "$WORK/j$j.o" "$keep/$(basename "$w" .wasm)-j$j.o"
      echo "FAIL[jobs]: $(basename "$w") differs at -j$j from -j$first (both in $keep)"
      fail=$((fail + 1))
      ok=0
    fi
  done
  [ "$ok" = 1 ] && agree=$((agree + 1))
done

# -j is the later word, so it must be the one that counts.
if "$BUILD/warmc" -O2 -j1 -c "$one" -o "$WORK/x.o" 2>/dev/null &&
   WARMC_JOBS=16 "$BUILD/warmc" -O2 -j1 -c "$one" -o "$WORK/y.o" 2>/dev/null; then
  cmp -s "$WORK/x.o" "$WORK/y.o" || {
    echo "FAIL[jobs]: WARMC_JOBS changed the output of -j1"
    fail=$((fail + 1))
  }
else
  echo "FAIL[jobs]: -j1 with WARMC_JOBS set does not compile"
  fail=$((fail + 1))
fi

echo "jobs: $agree of ${#mods[@]} module(s) identical across -j$(echo $widths | tr ' ' '/'), $fail failed"
[ "$fail" -eq 0 ]
