#!/usr/bin/env bash
# What a cgroup v2 hierarchy limits the process to (rt/cgroup.inc): the
# processors the compiler, and the JIT, take threads for (src/limits.c), and
# the GC heap a program reserves (rt.c gc_heap_from_cgroup).
#
# Trees laid out by hand under build/limits, each a /proc/self/cgroup, a
# /proc/self/mountinfo and the cgroup files below the mount, which
# WARMC_CGROUP_ROOT puts in front of every path: no limits; a container's
# quota and memory at the mount's top; a cgroup nested two deep whose parent
# limits more than it does; a mount whose root is the process's own cgroup;
# a mount point with a space in it; cgroup v1 alone; a memory limit under
# the least heap; limits of "max"; v1 mounts listed before the v2 one.  The
# compiler's processors are checked on any host but Windows, which has no
# cgroups; the heap a Linux program reserves on Linux, or in a container
# where podman runs one -- which then runs the program with real limits as
# well (--cpus, --memory).
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
WORK="$BUILD/limits"
rm -rf "$WORK"; mkdir -p "$WORK"
fail=0; pass=0
bad() { echo "FAIL[limits/$1]: $2"; fail=$((fail+1)); }

# tree NAME CGROUP-LINE MOUNT-ROOT MOUNT-POINT [FILE=CONTENT ...]
tree() {
  local d="$WORK/$1" line="$2" mroot="$3" mpoint="$4"; shift 4
  mkdir -p "$d/proc/self"
  printf '%s\n' "$line" > "$d/proc/self/cgroup"
  {
    echo "22 1 0:21 / /proc rw,nosuid shared:13 - proc proc rw"
    [ "${1:-}" = "--v1-first" ] && {
      echo "30 25 0:26 / /sys/fs/cgroup/cpu rw shared:9 - cgroup cgroup rw,cpu"
      shift
    }
    # mountinfo spells a space \040
    echo "29 23 0:25 $mroot ${mpoint// /\\040} rw,nosuid shared:8 - cgroup2 cgroup2 rw"
  } > "$d/proc/self/mountinfo"
  local kv
  for kv in "$@"; do
    mkdir -p "$d/$(dirname "${kv%%=*}")"
    printf '%s\n' "${kv#*=}" > "$d/${kv%%=*}"
  done
}

G=/sys/fs/cgroup
tree none       "0::/" / $G
tree container  "0::/" / $G "$G/cpu.max=150000 100000" "$G/memory.max=268435456" \
                "$G/memory.high=max"
tree nested     "0::/user.slice/app.scope" / $G \
                "$G/user.slice/cpu.max=50000 100000" \
                "$G/user.slice/app.scope/cpu.max=400000 100000" \
                "$G/user.slice/app.scope/memory.max=max" \
                "$G/user.slice/memory.high=100000000"
# what is below the mount's top is not the process's: a tighter limit there,
# where the mount's root taken for / would look first, is another cgroup's
tree mountroot  "0::/docker/abc" /docker/abc $G "$G/cpu.max=300000 100000" \
                "$G/memory.max=536870912" "$G/docker/abc/cpu.max=100000 100000" \
                "$G/docker/abc/memory.max=1048576"
tree spaced     "0::/a" / "/sys/fs/cg root" "/sys/fs/cg root/a/cpu.max=200000 100000"
tree v1         "4:cpu,cpuacct:/user.slice" / $G "$G/cpu.max=100000 100000"
tree tiny       "0::/" / $G "$G/memory.max=1048576"
tree max        "0::/" / $G "$G/cpu.max=max 100000" "$G/memory.max=max"
tree hybrid     "0::/" / $G --v1-first "$G/cpu.max=100000 100000"
# v1's line alone, where a v2 line would be
printf '4:cpu,cpuacct:/user.slice\n1:name=systemd:/user.slice\n' > "$WORK/v1/proc/self/cgroup"

printf '(module (func (export "nop")))\n' > "$WORK/nop.wat"

# The processors: QUOTA as printed, and the least of online, affinity and
# the quota rounded up
cpus() {
  local name="$1" quota="$2" out n online aff q want
  out=$(env -u WARMC_CACHE WARMC_CGROUP_ROOT="$PWD/$WORK/$name" WARMC_LIMITS_DEBUG=1 \
        "$BUILD/warmc" -c -o "$WORK/$name.o" "$WORK/nop.wat" 2>&1)
  if [[ ! "$out" =~ warmc:\ ([0-9]+)\ processors\ \(online\ ([0-9]+),\ affinity\ ([0-9]+),\ cgroup\ quota\ ([0-9]+\.[0-9]+)\) ]]; then
    bad "cpus/$name" "no processors line: $out"; return
  fi
  n=${BASH_REMATCH[1]} online=${BASH_REMATCH[2]} aff=${BASH_REMATCH[3]} q=${BASH_REMATCH[4]}
  if [ "$q" != "$quota" ]; then bad "cpus/$name" "a quota of $q, not $quota"; return; fi
  want=$online
  [ "$aff" -gt 0 ] && [ "$aff" -lt "$want" ] && want=$aff
  q=${q/./}; q=$((10#$q))
  [ "$q" -gt 0 ] && [ $(( (q + 999) / 1000 )) -lt "$want" ] && want=$(( (q + 999) / 1000 ))
  if [ "$n" != "$want" ]; then bad "cpus/$name" "$n processors, not $want: $out"; return; fi
  pass=$((pass+1))
}
# Windows has no cgroups, and its programs no /proc to read a tree as
case "$(uname -s)" in
  MINGW*|MSYS*|CLANG*|CYGWIN*)
    echo "limits: SKIPPED (Windows has no cgroups)"; exit 0 ;;
esac
cpus none 0.000
cpus container 1.500
cpus nested 0.500
cpus mountroot 3.000
cpus spaced 2.000
cpus v1 0.000
cpus tiny 0.000
cpus max 0.000
cpus hybrid 1.000

# The heap a Linux program reserves: run where Linux runs it
. ./test/hostenv.sh
RUNNER=
case "$(uname -s)/$(uname -m)" in
  Linux/aarch64) RUNNER=native ;;
  *)
    PODMAN=$(command -v podman || command -v docker) &&
      [ "$("$PODMAN" info --format '{{.Version.OsArch}}' 2>/dev/null)" = linux/arm64 ] &&
      RUNNER=podman ;;
esac
heap() {
  local name="$1" mem="$2" heap="$3" out
  if [ "$RUNNER" = native ]; then
    out=$(env WARMC_CGROUP_ROOT="$PWD/$WORK/$name" WARMC_LIMITS_DEBUG=1 "$WORK/nop" nop 2>&1)
  else
    out=$("$PODMAN" run --rm -v "$PWD/$WORK:/w:ro" -e WARMC_CGROUP_ROOT="/w/$name" \
          -e WARMC_LIMITS_DEBUG=1 "$LINUX_BASE" /w/nop nop 2>&1)
  fi
  local want="warmc-rt: heap $heap bytes (cgroup memory $mem)"
  if ! grep -qxF "$want" <<<"$out"; then bad "heap/$name" "wanted \"$want\", got: $out"; return; fi
  pass=$((pass+1))
}
if [ -n "$RUNNER" ] && [ -f "$BUILD/warmrt.a" ] &&
   "$BUILD/warmc" -O2 --rt "$BUILD/warmrt.a" -o "$WORK/nop" "$WORK/nop.wat" 2>"$WORK/link.err"; then
  . ./test/linux_image.sh
  G1=1073741824
  heap none 0 $G1
  heap container 268435456 134217728
  heap nested 100000000 50000000
  heap mountroot 536870912 268435456
  heap v1 0 $G1
  heap tiny 1048576 4194304
  heap max 0 $G1
  heap hybrid 0 $G1
  # a container's real limits
  if [ "$RUNNER" = podman ]; then
    out=$("$PODMAN" run --rm --cpus=1.5 --memory=256m -v "$PWD/$WORK:/w:ro" \
          -e WARMC_LIMITS_DEBUG=1 "$LINUX_BASE" /w/nop nop 2>&1)
    if grep -qxF "warmc-rt: heap 134217728 bytes (cgroup memory 268435456)" <<<"$out"; then
      pass=$((pass+1))
    else
      bad heap/podman "--memory=256m: $out"
    fi
  fi
  echo "limits: $pass passed, $fail failed (the heap ${RUNNER:+run }${RUNNER:-not run})"
else
  echo "limits: $pass passed, $fail failed (the heap not run: no Linux to run a program in)"
fi
[ "$fail" -eq 0 ]
