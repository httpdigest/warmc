#!/usr/bin/env bash

# Every fixture in the tree, put through a compiler built with the sanitizers.
#
# The suites elsewhere ask what warmc *answers*.  None of them asks whether it
# stayed inside its own allocations while working it out, and three of the
# defects this harness exists for wrote past a heap block while producing a
# perfectly correct answer: a br_table label vector whose count wrapped its
# size to zero, an inliner scratch buffer that grew for two of its three
# writers, and a per-block flag array read past its end after a pass appended
# blocks to the function.  A release build reports none of them.
#
# So: compile everything with build/warmc-asan and let the sanitizer speak.
# Both front ends are exercised -- the .wat fixtures go in as text, the .wasm
# in build/testrun as binary -- and -O2 is used throughout because the passes
# that own two of those three bugs do not run below it.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh

ASAN="$BUILD/warmc-asan"
if [ ! -x "$ASAN" ]; then
  echo "asan: no $ASAN, skipping (run \`make asan\`)"
  exit 0
fi

for src in src/*.c src/*.h src/*.inc; do
  if [ "$src" -nt "$ASAN" ]; then
    echo "asan: $ASAN is older than $src -- run \`make asan\`"
    exit 1
  fi
done

WORK="$BUILD/testrun/asan"
mkdir -p "$WORK"
export ASAN_OPTIONS=abort_on_error=0:detect_leaks=0

LEVELS=${ASAN_LEVELS:--O2 -O0}

pass=0; fail=0
check() {
  local input="$1" level="$2" label
  label="$(basename "$input")$level"
  local err="$WORK/report.err"
  wtimeout ./"$ASAN" $level -c -o "$WORK/out.o" "$input" >/dev/null 2>"$err"
  local rc=$?
  if grep -q "AddressSanitizer\|runtime error\|LeakSanitizer" "$err"; then
    printf 'FAIL[asan] %s\n' "$label"
    sed -n '1,12p' "$err" | sed 's/^/    /'
    fail=$((fail + 1))
    return
  fi
  # A rejection is a verdict, not a crash: these fixtures include modules that
  # are meant to be refused, and 1/2/3 are how warmc says so.
  if [ "$rc" -gt 3 ]; then
    printf 'FAIL[asan] %s: exit %s\n' "$label" "$rc"
    sed -n '1,4p' "$err" | sed 's/^/    /'
    fail=$((fail + 1))
    return
  fi
  pass=$((pass + 1))
}

inputs=()
for d in test/cases test/cases_gc test/cases_traps test/cases_relaxed test/wasi; do
  [ -d "$d" ] || continue
  for f in "$d"/*.wat; do [ -e "$f" ] && inputs+=("$f"); done
done
for f in "$BUILD"/testrun/*.wasm; do [ -e "$f" ] && inputs+=("$f"); done
# Components, which nothing else here puts through the sanitizers: the
# mutation sweep in run_components.py probes with --validate-only, and that
# returns before the link plan and before any compiling.  A component's core
# modules reach ir_build by a different road from a core module named on the
# command line, and the one thing that road did not do was validate them.
for f in "$BUILD"/components/*.wasm; do [ -e "$f" ] && inputs+=("$f"); done

for input in "${inputs[@]}"; do
  for level in $LEVELS; do
    check "$input" "$level"
  done
done

echo "asan: $pass compiled clean, $fail failed (${#inputs[@]} inputs)"
[ "$fail" -eq 0 ]
