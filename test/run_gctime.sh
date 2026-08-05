#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
REPS=${REPS:-7}
ONLY=${ONLY:-}
. test/hostenv.sh
WORK="$BUILD/testrun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

"$WT" parse test/bench/gcbench.wat -o "$WORK/gcbench.wasm" || exit 1
wtimeout ./$BUILD/warmc -O2 $WARMC_TARGET_FLAGS \
  -o "$WORK/gcbench" "$WORK/gcbench.wasm" || exit 1

timeit() {
  local envs=()
  while [ "$1" != "--" ]; do envs+=("$1"); shift; done
  shift
  python3 -c '
import os, subprocess, sys, time
env = dict(os.environ)
argv = sys.argv[1:]
i = argv.index("--")
env.update(dict(a.split("=", 1) for a in argv[:i]))
t0 = time.monotonic_ns()
r = subprocess.run(argv[i+1:], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
t1 = time.monotonic_ns()
if r.returncode: print("FAILED", r.returncode, file=sys.stderr)
print((t1 - t0) // 1000000)
' "${envs[@]}" -- "$WORK/gcbench" "$@"
}

bench() {
  local label="$1"; shift
  local common=()
  while [ "$1" != "--" ]; do common+=("$1"); shift; done
  shift
  [ -n "$ONLY" ] && [ "$ONLY" != "$label" ] && return
  local gbest=999999 mbest=999999 t
  for _ in $(seq "$REPS"); do
    t=$(timeit "${common[@]}" WARMC_GC_MINOR=1 -- "$@"); [ "$t" -lt "$gbest" ] && gbest=$t
    t=$(timeit "${common[@]}" WARMC_GC_MINOR=0 -- "$@"); [ "$t" -lt "$mbest" ] && mbest=$t
  done
  printf '%-12s gen %5d ms   major-only %5d ms   %.2fx\n' \
    "$label" "$gbest" "$mbest" \
    "$(python3 -c "print($gbest/$mbest)")"
}

PACE="WARMC_GC_MIN_KB=64 WARMC_GC_GROWTH=1"
bench tree       WARMC_GC_INCREMENTAL=1 -- tree 21 5000000
bench tree14     WARMC_GC_INCREMENTAL=1 -- tree 14 20000000
bench bigtable   WARMC_GC_INCREMENTAL=1 -- bigtable 1048576 5000000
bench churnstore WARMC_GC_INCREMENTAL=1 $PACE -- churnstore 18 700000 400
