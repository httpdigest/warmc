#!/usr/bin/env bash
# The -O levels.  Each -O sets everything a level decides, so the last one
# given is the level whatever came before it, and -O3 is -O2 with the
# passes that change what a program can see, fma, dead-frames and acc-tail.
# So two spellings of one level compile a module to the same bytes -- -O0
# -O2 and -O2, -O3 and -O2 -ffma -fdead-frames -facc-tail -- and -O3 is not
# -O2 where those passes have something to do.  The modules are ones each
# level-decided pass changes: Go's stack pointer (go-sp), a trampoline, C
# frames, fusable floats, recursions that sum.  And the -fno- form of a flag that turns another on as well, and
# -fexact-trap, which turns others off.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
WORK="$BUILD/testrun/levels"
mkdir -p "$WORK"
fail=0; n=0

obj() {
  local out=$1 src=$2; shift 2
  if ! "$BUILD/warmc" "$@" -c -o "$out" "$src" 2>"$WORK/err"; then
    echo "FAIL[levels]: warmc $* $src: $(head -1 "$WORK/err")"
    fail=$((fail + 1))
    return 1
  fi
}

# same|differ MODULE "FLAGS A" "FLAGS B"
check() {
  local want=$1 src=$2 a=$3 b=$4
  local name; name=$(basename "$src" .wat)
  obj "$WORK/$name.a.o" "$src" $a || return
  obj "$WORK/$name.b.o" "$src" $b || return
  n=$((n + 1))
  if cmp -s "$WORK/$name.a.o" "$WORK/$name.b.o"; then
    [ "$want" = same ] && return
    echo "FAIL[levels]: $name: $a and $b compile to the same object"
  else
    [ "$want" = differ ] && return
    echo "FAIL[levels]: $name: $a and $b compile to different objects"
  fi
  fail=$((fail + 1))
}

for src in test/cases/gosp.wat test/cases/trampoline.wat test/cases/deadframes.wat \
           test/cases_fma/fma.wat test/cases_opt/acc_tail.wat; do
  check same "$src" "-O0 -O1" "-O1"
  check same "$src" "-O0 -O2" "-O2"
  check same "$src" "-O1 -O2" "-O2"
  check same "$src" "-O3 -O2" "-O2"
  check same "$src" "-O3 -O1" "-O1"
  check same "$src" "-O0 -O3" "-O3"
  check same "$src" "-O3" "-O2 -ffma -fdead-frames -facc-tail"
  check differ "$src" "-O0" "-O1"
done
check differ test/cases_fma/fma.wat "-O2" "-O3"
check differ test/cases/deadframes.wat "-O2" "-O3"
check differ test/cases_opt/acc_tail.wat "-O2" "-O3"

# A flag's -fno- form turns that flag off and nothing else: -fgssa-all
# turns gssa on as well, and -fno-gssa-all leaves it on.
check same test/cases/gosp.wat "-O2 -fno-gssa-all" "-O2"
check same test/cases/gosp.wat "-O2 -fgssa-all -fno-gssa-all" "-O2"
check same test/cases/gosp.wat "-O2 -fno-gssa -fgssa-all" "-O2 -fgssa-all"
check differ test/cases/gosp.wat "-O2 -fno-gssa" "-O2"
check differ test/cases/gosp.wat "-O2 -fgssa-all" "-O2"
# -floadbar is the default, and turns back on what -fno-loadbar turned off.
check same test/cases_gc/castfacts.wat "-O2 -floadbar" "-O2"
check same test/cases_gc/castfacts.wat "-O2 -fno-loadbar -floadbar" "-O2"
check differ test/cases_gc/castfacts.wat "-O2 -fno-loadbar" "-O2"
# -fexact-trap is on at no level, -fno-exact-trap undoes it, and with it the
# passes that keep a trap's state where a trap cannot see it stay off
# whatever their own flags say, before it or after.
check differ test/cases_trapstate/gloop_global_at_trap.wat "-O2" "-O2 -fexact-trap"
check same test/cases_trapstate/gloop_global_at_trap.wat "-O3 -fexact-trap -fno-exact-trap" "-O3"
check same test/cases/deadframes.wat "-O3 -fexact-trap -fdead-frames" "-O3 -fexact-trap"
check same test/cases/deadframes.wat "-O3 -fdead-frames -fexact-trap" "-O3 -fexact-trap"
check same test/cases_opt/acc_tail.wat "-O3 -fexact-trap -facc-tail" "-O3 -fexact-trap"
check differ test/cases_opt/acc_tail.wat "-O3 -fexact-trap" "-O3"
check same test/cases/gosp.wat "-O2 -fexact-trap -fgo-sp" "-O2 -fexact-trap"
check same test/cases/trampoline.wat "-O2 -fexact-trap -ftrampoline" "-O2 -fexact-trap"

[ "$fail" = 0 ] && echo "levels: $n pairs of levels compile as they should"
[ "$fail" = 0 ]
