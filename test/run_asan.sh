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

# One compile, its verdict on stdout: "ok", or FAIL and the report.  The
# loop below runs one per input and level, on every core at once (alone,
# one after another, they were make check's longest row by two minutes).
check() {
  local input="$1" level="$2" label
  label="$(basename "$input")$level"
  local err out
  err=$(mktemp "$WORK/report.XXXXXX") out=$(mktemp "$WORK/out.XXXXXX")
  wtimeout ./"$ASAN" $level -c -o "$out" "$input" >/dev/null 2>"$err"
  local rc=$?
  if grep -q "AddressSanitizer\|runtime error\|LeakSanitizer" "$err"; then
    printf 'FAIL[asan] %s\n' "$label"
    sed -n '1,12p' "$err" | sed 's/^/    /'
  # A rejection is a verdict, not a crash: these fixtures include modules that
  # are meant to be refused, and 1/2/3 are how warmc says so.
  elif [ "$rc" -gt 3 ]; then
    printf 'FAIL[asan] %s: exit %s\n' "$label" "$rc"
    sed -n '1,4p' "$err" | sed 's/^/    /'
  else
    echo ok
    # clean: kept under the key its caller worked out
    [ -n "${3:-}" ] && mkdir -p "$CLEAN/${3:0:2}" && : >"$CLEAN/${3:0:2}/$3"
  fi
  rm -f "$err" "$out"
}

# Compiles found clean, each an empty file named by a digest of the ASan
# build, the input, the level and ASAN_OPTIONS: what a clean compile depends
# on.  WARMC_NO_TESTCACHE=1 makes them all again.
CLEAN="$BUILD/testcache/asan-clean"

if [ "${1:-}" = --one ]; then
  check "$2" "$3" "${4:-}"
  exit 0
fi

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

# Each input at every level, but where the levels add nothing: the binary in
# build/testrun of a fixture that is here as text (whose text goes through
# the binary parser too), at the first level alone.  An ASan process takes
# 40 ms to start.
JOBS=$( (sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null) || echo 4)
first=${LEVELS%% *}
results="$WORK/results"
want=0
pairs="$WORK/pairs"
: >"$pairs"
for input in "${inputs[@]}"; do
  levels=$LEVELS
  case "$input" in
    "$BUILD"/testrun/*.wasm)
      b=$(basename "$input" .wasm)
      for d in test/cases test/cases_gc test/cases_traps test/cases_relaxed test/wasi; do
        [ -e "$d/$b.wat" ] && levels=$first
      done ;;
  esac
  for level in $levels; do printf '%s\0%s\0' "$input" "$level" >>"$pairs"; want=$((want + 1)); done
done
# each pair with its key, those already clean answered here
python3 - "$ASAN" "$CLEAN" "$pairs" "$WORK/todo" "$results" <<'EOF'
import os, sys
sys.path.insert(0, "test")
import testcache
asan, clean, pairs, todo, results = sys.argv[1:]
items = open(pairs, "rb").read().split(b"\0")[:-1]
with open(todo, "wb") as t, open(results, "w") as r:
    for i in range(0, len(items), 2):
        inp, lv = items[i].decode(), items[i + 1].decode()
        k = testcache.key("asan clean", testcache.File(asan), testcache.File(inp), lv,
                          os.environ.get("ASAN_OPTIONS", ""))
        if not testcache.OFF and os.path.exists(os.path.join(clean, k[:2], k)):
            r.write("ok\n")
        else:
            t.write(items[i] + b"\0" + items[i + 1] + b"\0" + k.encode() + b"\0")
EOF
xargs -0 -n 3 -P "$JOBS" "$0" --one <"$WORK/todo" >>"$results"

pass=$(grep -c '^ok$' "$results")
fail=$(grep -c '^FAIL' "$results")
grep -v '^ok$' "$results"
echo "asan: $pass compiled clean, $fail failed (${#inputs[@]} inputs)"
[ "$fail" -eq 0 ] && [ "$pass" -eq "$want" ]
