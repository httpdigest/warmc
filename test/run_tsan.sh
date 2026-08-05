#!/usr/bin/env bash

# The compiler, under a race detector.
#
# warmc compiles a module's functions on a pthread pool -- ir_build,
# ir_optimize and ir_produce_function run concurrently over a frozen
# ObjBuilder and a shared effects cache -- and until this harness existed
# nothing had ever checked that sharing with a tool.  `make asan` builds
# address and undefined only; the runtime cannot be built with the thread
# sanitizer at all, being freestanding, so this covers the compiler, which
# is the half that has threads.
#
# Two things the harness has to get right or it tests nothing.  WARMC_JOBS
# must be forced, and the input must have enough functions: want_threads
# clamps the pool to nfuncs / 8, so almost every fixture in test/cases
# compiles single-threaded no matter what WARMC_JOBS says.  The modules
# picked below are the ones in build/testrun with the most functions.
#
# WARMC_PASSTIME is set for one of the runs because the pass timer is
# per-process state every worker writes -- the first thing this harness
# found.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}

# A budget a stalled machine cannot exhaust, set before hostenv.sh so its own
# `:=` leaves it alone and an explicit WARMC_TIMEOUT from a caller still wins.
# These compiles take 0.4-0.6s each here, loaded or idle -- 250 of them showed
# no tail at all -- so hostenv.sh's 6s default is only about ten times the work,
# and one transient stall inside run_all.sh's fan-out was enough to spend it:
# the row failed on one of four full runs with SIGALRM, which reads exactly like
# a race report when it is nothing of the kind.  The alarm is still here for the
# thing it is worth having, a pool that wedges rather than one that is slow.
: "${WARMC_TIMEOUT:=${TSAN_TIMEOUT:-60}}"
export WARMC_TIMEOUT

. test/hostenv.sh

TSAN="$BUILD/warmc-tsan"
if [ ! -x "$TSAN" ]; then
  echo "tsan: no $TSAN, skipping (run \`make tsan\`)"
  exit 0
fi

for src in src/*.c src/*.h src/*.inc; do
  if [ "$src" -nt "$TSAN" ]; then
    echo "tsan: $TSAN is older than $src -- run \`make tsan\`"
    exit 1
  fi
done

WORK="$BUILD/testrun/tsan"
mkdir -p "$WORK"
export TSAN_OPTIONS=halt_on_error=0

JOBS=${TSAN_JOBS:-8}

# Ranked by function count, because that is what decides the pool size.
inputs=()
while IFS= read -r f; do
  [ -n "$f" ] && inputs+=("$f")
done < <(python3 - "$BUILD" <<'PY'
import glob, os, sys
build = sys.argv[1]

def leb(b, i):
    r = s = 0
    while True:
        x = b[i]; i += 1
        r |= (x & 0x7F) << s; s += 7
        if not x & 0x80:
            return r, i

def nfuncs(path):
    b = open(path, "rb").read()
    if b[:4] != b"\x00asm":
        return 0
    i, n = 8, 0
    try:
        while i < len(b):
            sid = b[i]; i += 1
            size, i = leb(b, i)
            if sid == 10:
                n, _ = leb(b, i)
            i += size
    except IndexError:
        pass
    return n

cands = []
for pat in ("testrun/*.wasm", "gobench/*.wasm", "ktbench/*.wasm",
            "rsbench/*.wasm", "zigbench/*.wasm"):
    for f in glob.glob(os.path.join(build, pat)):
        n = nfuncs(f)
        # Under nfuncs/8 the pool is one thread and the run says nothing.
        if n >= 64:
            cands.append((n, f))
cands.sort(reverse=True)
for _n, f in cands[:int(os.environ.get("TSAN_INPUTS", "6"))]:
    print(f)
PY
)

if [ "${#inputs[@]}" -eq 0 ]; then
  echo "tsan: no module in $BUILD has enough functions to start a pool" >&2
  echo "tsan: run \`./test/run_cases.py --prepare\` first" >&2
  exit 1
fi

pass=0; fail=0
check() {
  local input="$1" label="$2" passtime="$3"
  local err="$WORK/report.err"
  (
    export WARMC_JOBS="$JOBS"
    [ -n "$passtime" ] && export WARMC_PASSTIME=1
    wtimeout ./"$TSAN" -O2 -c -o "$WORK/out.o" "$input" >/dev/null 2>"$err"
  )
  local rc=$?
  if grep -q "WARNING: ThreadSanitizer" "$err"; then
    printf 'FAIL[tsan] %s\n' "$label"
    sed -n '1,16p' "$err" | sed 's/^/    /'
    fail=$((fail + 1))
    return
  fi
  if [ "$rc" -gt 3 ]; then
    printf 'FAIL[tsan] %s: exit %s\n' "$label" "$rc"
    sed -n '1,4p' "$err" | sed 's/^/    /'
    fail=$((fail + 1))
    return
  fi
  pass=$((pass + 1))
}

for input in "${inputs[@]}"; do
  check "$input" "$(basename "$input")" ""
  check "$input" "$(basename "$input") passtime" 1
done

echo "tsan: $pass compiled clean, $fail failed (${#inputs[@]} inputs, $JOBS jobs)"
[ "$fail" -eq 0 ]
