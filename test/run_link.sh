#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/linkrun"
mkdir -p "$WORK"

if ! command -v wat2wasm >/dev/null; then
  echo "run_link: wabt not found on PATH" >&2
  exit 2
fi

fail=0

python3 test/gen_link_wide.py "$WORK/c.wat" || exit 1
for m in a b; do
  wat2wasm $WABT_FLAGS "test/link/$m.wat" -o "$WORK/$m.wasm" || exit 1
  wtimeout ./$BUILD/warmc ${WARMC_FLAGS:--O2} $WARMC_TARGET_FLAGS --symbol-prefix=$m \
      -c -o "$WORK/$m.o" "$WORK/$m.wasm" || { echo "FAIL[link]: compiling $m"; exit 1; }
done
wat2wasm $WABT_FLAGS "$WORK/c.wat" -o "$WORK/c.wasm" || exit 1
wtimeout ./$BUILD/warmc ${WARMC_FLAGS:--O2} $WARMC_TARGET_FLAGS --symbol-prefix=c \
    -c -o "$WORK/c.o" "$WORK/c.wasm" || { echo "FAIL[link]: compiling c"; exit 1; }

# Sets LINKED to the program it produced -- on Windows the linker adds .exe.
link_prog() {
  local prog="$1" drv="$2"; shift 2
  python3 test/hostbuild.py cc "$drv" "$WORK/$prog.drv.o" \
    || { echo "FAIL[link]: driver $drv"; return 1; }
  BUILD="$BUILD" python3 test/hostbuild.py link "$WORK/$prog" \
      "$WORK/$prog.drv.o" "$@" \
    || { echo "FAIL[link]: ld $prog"; return 1; }
  LINKED=$(python3 test/hostbuild.py exe "$WORK/$prog")
}

link_prog prog test/link/driver.c "$WORK/a.o" "$WORK/b.o" "$WORK/c.o" || exit 1

got=$(wruntimeout $WARMC_RUN "$LINKED" 2>&1)
want='reexported_secret=4243
a_reads_own_secret=4243
call_import=42
call_indirect_import=40
tail_call_import=22
tail_call_indirect_import=24
tail_secret=4243
read_shared_mem=104
read_answer=42
read_mine=7
read_derived=42
read_at_answer=88
read_counter=100
bump_twice=102
read_counter_after_bump=102
a_sees_set=6
size_before_grow=1
grow=1
size_after_grow=2
still_reads_data=104
wide_call_import=84
wide_tail_import=44
wide_indirect_import=64
wide_imported_global=6
wide_last_global=4242
wide_first_global=1
catch_across=1007
catch_across_deep=2008
catch_across_wrong_tag=4007
catch_own=5007
catch_across_all=6007
catch_across_ref=7007
catch_across_shielded=8007
catch_across_tail=9007
catch_across_alt=11077'

if [ "$got" = "$want" ]; then
  echo "link: 35 cross-module checks passed"
else
  echo "FAIL[link]: output differs"
  diff <(echo "$want") <(echo "$got") | head -20
  fail=1
fi

# The embedder's view: a trap partway through a loop, then a look at the
# memory the loop was writing.  Run at both levels, because the answer must
# not depend on which passes ran.
trapwant='trapped=1
cell_after_trap=42
cell_after_ok=778'

wat2wasm $WABT_FLAGS "test/link/trap.wat" -o "$WORK/t.wasm" || exit 1
for lv in -O0 -O1 -O2; do
  wtimeout ./$BUILD/warmc $lv $WARMC_TARGET_FLAGS --symbol-prefix=t \
      -c -o "$WORK/t.o" "$WORK/t.wasm" \
    || { echo "FAIL[link]: compiling trap.wat at $lv"; exit 1; }
  link_prog trapprog test/link/trapdriver.c "$WORK/t.o" || exit 1
  got=$(wruntimeout $WARMC_RUN "$LINKED" 2>&1)
  if [ "$got" = "$trapwant" ]; then continue; fi
  echo "FAIL[link/trap]: $lv output differs"
  diff <(echo "$trapwant") <(echo "$got") | head -10
  fail=1
done
[ "$fail" = 0 ] && echo "link: a trap seen from an embedder, 3 optimisation levels"

WT="$BUILD/tools/bin/wasm-tools"
if [ ! -x "$WT" ]; then
  echo "link: $WT not found, skipping the collection pair (run \`make wasm-tools\`)"
  exit $fail
fi
for m in a b; do
  "$WT" parse "test/link/gc_$m.wat" -o "$WORK/gc$m.wasm" \
    || { echo "FAIL[link]: assembling gc_$m.wat"; exit 1; }
  wtimeout ./$BUILD/warmc ${WARMC_FLAGS:--O2} $WARMC_TARGET_FLAGS --symbol-prefix=gc$m \
      -c -o "$WORK/gc$m.o" "$WORK/gc$m.wasm" \
    || { echo "FAIL[link]: compiling gc$m"; exit 1; }
done
link_prog gcprog test/link/gcdriver.c "$WORK/gca.o" "$WORK/gcb.o" || exit 1

gcwant='gclink_small=4330
gclink_large=4330
gclink_cast=111
gclink_lent=5'

gc_run() {
  local label="$1"; shift
  local got
  got=$( for v in "$@"; do export "$v"; done
         wruntimeout $WARMC_RUN "$LINKED" 2>&1 )
  if [ "$got" = "$gcwant" ]; then return 0; fi
  echo "FAIL[link/gc]: $label output differs"
  diff <(echo "$gcwant") <(echo "$got") | head -10
  fail=1
}
gc_run default
gc_run stress WARMC_GC_STRESS=1 WARMC_GC_RELOC=8 WARMC_GC_VERIFY=1

# A round handler that fails while the embedder is catching.  The round is
# consumed before the handler runs, so unless the trap path answers it the
# collector waits for it for ever -- with a cycle still marked running, which
# is the one thing the mutator's allocation assist waits on.  What this asks
# is therefore not that the failure is reported but that the *next* call
# finishes at all.
dieargs='gclink_caught=1
gclink_after=4330'
for cfg in "WARMC_GC_MIN_KB=4" "WARMC_GC_MIN_KB=4 WARMC_GC_RELOC=8"; do
  got=$( for v in $cfg WARMC_GC_DIE_ROUND=1; do export "$v"; done
         wruntimeout $WARMC_RUN "$LINKED" r 2>/dev/null | tail -2 )
  if [ "$got" = "$dieargs" ]; then continue; fi
  echo "FAIL[link/gc]: a failed round wedged the collector ($cfg)"
  diff <(echo "$dieargs") <(echo "$got") | head -6
  fail=1
done
[ "$fail" = 0 ] && echo "link: a round handler that fails does not wedge the collector"

[ "$fail" = 0 ] && echo "link: a collection across the module boundary, 2 configurations"

# A System V archive stores any member name of sixteen characters or more as
# `/<offset>` into a `//` string table, and warmc's reader skipped every one of
# them: the members were never returned, so their symbols went undefined with
# nothing said about a member being dropped.  Today's runtime objects all have
# short names, which is the only reason it did not show.  Build the same
# archive twice, once with a name over the threshold, and require both to link
# to the same thing.
GNUAR=""
for c in llvm-ar ar gar; do
  if command -v "$c" >/dev/null && "$c" --format=gnu rc "$WORK/probe.a" 2>/dev/null; then
    GNUAR="$c"; rm -f "$WORK/probe.a"; break
  fi
done
if [ -z "$GNUAR" ]; then
  echo "link: no ar that writes System V archives, skipping the long-name check"
else
  AW="$WORK/arname"; rm -rf "$AW"; mkdir -p "$AW"
  printf '(module (func (export "f") (result i32) (i32.const 5)))\n' > "$AW/t.wat"
  cp "$BUILD"/rt/rt.o "$AW/a_sixteen_char.o"
  cp "$BUILD"/rt/rt.o "$AW/short.o"
  others="$BUILD/rt/start.o $BUILD/rt/hook.o $BUILD/rt/gcthook.o \
          $BUILD/rt/modules.o $BUILD/rt/component.o"
  "$GNUAR" --format=gnu rcs "$AW/long.a"  "$AW/a_sixteen_char.o" $others 2>/dev/null
  "$GNUAR" --format=gnu rcs "$AW/short.a" "$AW/short.o" $others 2>/dev/null
  for v in long short; do
    if ! wtimeout ./$BUILD/warmc --target linux --rt "$AW/$v.a" \
           -o "$AW/$v.out" "$AW/t.wat" 2>"$AW/$v.err"; then
      echo "FAIL[link/ar]: $v member name did not link: $(head -1 "$AW/$v.err")"
      fail=1
    fi
  done
  if [ "$fail" = 0 ] && ! cmp -s "$AW/long.out" "$AW/short.out"; then
    echo "FAIL[link/ar]: the two archives linked to different binaries"
    fail=1
  fi
  [ "$fail" = 0 ] && echo "link: a member name past the inline limit is still read"
fi

# Nothing provides wasi_snapshot_preview1 unless --wasi links the host that
# does: the CLI harness registers no module of that name, and instantiation
# resolves every import before _start runs, so the program cannot start however
# little of wasi it goes on to call.  warmc used to build it anyway and leave
# the failure to run time, reading as
# `link error: unknown module "wasi_snapshot_preview1"."sched_yield"` -- which
# names neither the flag that fixes it nor the fact that a flag would.  The
# same module still has to compile to an *object*, because linking a host of
# one's own is what that is for: test/run_tinygo.sh compiles warmc's own stub
# module beside the app exactly this way.
WI="$WORK/wasiimport"; rm -rf "$WI"; mkdir -p "$WI"
cat > "$WI/w.wat" <<'EOF'
(module
  (import "wasi_snapshot_preview1" "sched_yield" (func $y (result i32)))
  (func (export "_start") (drop (call $y))))
EOF
if wtimeout ./$BUILD/warmc $WARMC_TARGET_FLAGS -o "$WI/w" "$WI/w.wat" 2>"$WI/w.err"; then
  echo "FAIL[link/wasi]: built a program with nothing to serve its wasi imports"
  fail=1
elif ! grep -q -- '--wasi' "$WI/w.err"; then
  echo "FAIL[link/wasi]: the refusal never names --wasi: $(head -1 "$WI/w.err")"
  fail=1
elif ! wtimeout ./$BUILD/warmc $WARMC_TARGET_FLAGS -c -o "$WI/w.o" "$WI/w.wat" \
       2>"$WI/wc.err"; then
  echo "FAIL[link/wasi]: -c refused it too: $(head -1 "$WI/wc.err")"
  fail=1
elif ! wtimeout ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS -o "$WI/w" "$WI/w.wat" \
       2>"$WI/ww.err"; then
  echo "FAIL[link/wasi]: --wasi could not build it: $(head -1 "$WI/ww.err")"
  fail=1
elif ! wruntimeout $WARMC_RUN "$WI/w" >/dev/null 2>"$WI/wr.err"; then
  echo "FAIL[link/wasi]: the --wasi build did not run: $(head -1 "$WI/wr.err")"
  fail=1
else
  echo "link: a wasi import with no host is refused; --wasi and -c still build it"
fi

exit $fail
