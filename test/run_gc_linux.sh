#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
WORK="$BUILD/gclinux"
IMAGE=${WARMC_LINUX_IMAGE:-docker.io/library/debian:trixie-slim}

skip() { echo "gc-linux: SKIPPED ($1)"; exit 0; }

PODMAN=$(command -v podman || command -v docker) || skip "no podman or docker"
arch=$("$PODMAN" info --format '{{.Version.OsArch}}' 2>/dev/null) ||
  skip "$(basename "$PODMAN") is installed but not running"
[ "$arch" = linux/arm64 ] ||
  skip "the container runs $arch, and warmc emits aarch64"
[ -f "$BUILD/warmrt.a" ] || skip "no $BUILD/warmrt.a; run make all"

rm -rf "$WORK"; mkdir -p "$WORK"
fail=0
bad() { echo "FAIL[gc-linux/$1]: $2"; fail=1; }

printf '(module (func (export "nop")))\n' > "$WORK/nop.wat"

if ! "$BUILD/warmc" -O2 --rt "$BUILD/warmgcth.a" -o "$WORK/nop" "$WORK/nop.wat" \
     2>"$WORK/link.err"; then
  echo "FAIL[gc-linux]: cannot link a module against the Linux runtime"
  sed 's/^/    /' "$WORK/link.err"
  exit 1
fi

inside() {
  local label="$1" want="$2"; shift 2
  local args=() cmd=() v
  for v in "$@"; do
    shift
    [ "$v" = -- ] && { cmd=("$@"); break; }
    args+=(-e "$v")
  done
  [ "${#cmd[@]}" -gt 0 ] || cmd=(/w/nop nop)
  local out rc
  out=$("$PODMAN" run --rm -v "$PWD/$WORK:/w:ro" "${args[@]}" "$IMAGE" \
        "${cmd[@]}" 2>&1); rc=$?
  printf '%s\n' "$out" > "$WORK/$label.log"
  if [ "$rc" -ne "$want" ]; then
    bad "$label" "exited $rc, expected $want"
    sed 's/^/    /' "$WORK/$label.log"
    return 1
  fi
  return 0
}

N=${GCTHREAD_ROUNDS:-300000}
if inside pingpong 0 "WARMC_GC_THREADTEST=$N"; then
  parked=$(sed -n 's/.*round trips parked:.*, \([0-9]*\) into the kernel/\1/p' \
           "$WORK/pingpong.log")
  grep -q 'gcth: ok$' "$WORK/pingpong.log" || bad pingpong "the run did not finish"
  if [ -z "$parked" ]; then
    bad pingpong "no parked phase in the output"
  elif [ "$parked" -lt "$((N / 10))" ]; then
    bad pingpong "only $parked waits reached the futex with the spin off"
  fi
  sed 's/^/  /' "$WORK/pingpong.log"
fi

if inside die 1 WARMC_GC_THREADTEST=1000 WARMC_GC_THREADTEST_DIE=1; then
  grep -q '^warmc-rt: the collector.s thread reached a state' "$WORK/die.log" ||
    bad die "died without saying so: $(tail -1 "$WORK/die.log")"
fi

if inside fault 139 WARMC_GC_THREADTEST=1000 WARMC_GC_THREADTEST_FAULT=1; then
  grep -q "^warmc-rt: fault on the collector.s thread" "$WORK/fault.log" ||
    bad fault "faulted as if it were the mutator: $(tail -1 "$WORK/fault.log")"
fi

if inside rounds 0 WARMC_GC_ROUNDS=0 WARMC_GC_STATS=1; then
  grep -q "  rounds: " "$WORK/rounds.log" || bad rounds "the round driver never ran"
  grep -q "0 exits the collector outlasted" "$WORK/rounds.log" ||
    bad rounds "the collector had to be left behind: $(grep '  rounds:' "$WORK/rounds.log")"
fi

n=4
if [ ! -s "$BUILD/testrun/gccollect.wasm" ] &&
   ! ./test/run_gc.sh --prepare >/dev/null 2>&1; then
  echo "gc-linux: the collector's cases need \`make wasm-tools\`; ran the substrate only"
elif ! "$BUILD/warmc" -O2 --rt "$BUILD/warmgcth.a" -o "$WORK/gccollect" \
       "$BUILD/testrun/gccollect.wasm" 2>"$WORK/link2.err"; then
  bad collector "cannot link the collector's cases against the Linux runtime"
  sed 's/^/    /' "$WORK/link2.err"
else
  conc="WARMC_GC_ROUNDS=1"
  for case in "churn 20000" "live_heap 1500 20000" "table_late_store 20 300" \
              "many_throws 1500"; do
    label="conc-${case%% *}"

    if inside "$label" 0 "$conc" WARMC_GC_VERIFY=1 WARMC_GC_STATS=1 \
              WARMC_GC_MIN_KB=16 -- /w/gccollect $case; then
      cycles=$(sed -n 's/^gc: \([0-9]*\) cycles.*/\1/p' "$WORK/$label.log")
      [ -n "$cycles" ] && [ "$cycles" -gt 0 ] ||
        bad "$label" "no cycle ran, so nothing was collected concurrently"
      grep -q '  rounds: ' "$WORK/$label.log" ||
        bad "$label" "the collector's thread never started"
      grep -q '0 exits the collector outlasted' "$WORK/$label.log" ||
        bad "$label" "the collector had to be left behind at exit"
      n=$((n + 1))
    fi
  done

  if inside conc-paced 0 "$conc" WARMC_GC_VERIFY=1 WARMC_GC_STATS=1 \
            WARMC_GC_MIN_KB=64 WARMC_GC_RELOC=8 WARMC_GC_HEAP_KB=4096 \
            -- /w/gccollect fragment 20000; then

    trigger=$(sed -n 's/.*[ ,]\([0-9][0-9]*\) byte trigger.*/\1/p' "$WORK/conc-paced.log")
    [ -n "$trigger" ] && [ "$trigger" -gt 16384 ] ||
      bad conc-paced "the pacer never left the configured trigger: $trigger"
    n=$((n + 1))
  fi

  if inside conc-oom 1 "$conc" WARMC_GC_STATS=1 WARMC_GC_MIN_KB=16 \
            WARMC_GC_HEAP_KB=1024 -- /w/gccollect --catch \
            oom_fill 1000000 -- oom_throw -- oom_recover 2000; then
    [ "$(grep -c '^trap: allocation failed$' "$WORK/conc-oom.log")" = 2 ] &&
      grep -q '^i32:1$' "$WORK/conc-oom.log" ||
      bad conc-oom "a full heap did not recover after the trap"
    grep -q '0 exits the collector outlasted' "$WORK/conc-oom.log" ||
      bad conc-oom "the collector had to be left behind at exit"
    n=$((n + 1))
  fi

  sed -n 's/^gc: /  churn: /p' "$WORK/conc-churn.log" | head -1
fi

if [ "$fail" -eq 0 ]; then echo "gc-linux: $n passed, 0 failed"; else echo "gc-linux: failed"; fi
exit $fail
