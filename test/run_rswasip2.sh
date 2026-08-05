#!/usr/bin/env bash

# A second guest, so that the host is not being told what to be by one of them.
#
# Every other wasip2 case here is built by tinygo, and a suite with one guest
# tests the host and that guest's idea of it together.  What that costs showed
# up twice.  tinygo's wasip2 symlink() resolves the link *target* as a path
# under a preopen and so can never succeed, which meant test/wasip2/escape.go
# passed while creating none of the links it walked -- both engines failed
# identically and the case tested nothing.  And tinygo asks for
# wasi:io/streams@0.2.0 where rustc asks for @0.2.12, which the host matched
# exactly: every call from a Rust guest reached the unimplemented trap.
#
# rustc's std also takes wholly different routes through the same interfaces.
# It writes through check-write/write/flush where tinygo calls
# blocking-write-and-flush, and reaches a file through read-via-stream and
# write-via-stream where tinygo calls descriptor.read and descriptor.write --
# so thirteen functions this host now implements were reached by nothing at
# all until this suite existed.

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
. ./test/hostenv.sh

RUST_TARGET=wasm32-wasip2

if ! command -v rustc >/dev/null 2>&1; then
  echo "rswasip2: SKIPPED (rustc not on PATH)"
  exit 0
fi
# Whether rustc can build for the target, not merely whether it exists: a
# rustc without this std reports itself as a wall of E0463 rather than as
# anything about targets, and it is the more likely state of a machine.
libdir=$(rustc --print target-libdir --target "$RUST_TARGET" 2>/dev/null)
if [ -z "$libdir" ] || [ ! -d "$libdir" ] ||
   ! ls "$libdir"/libstd-*.rlib >/dev/null 2>&1; then
  echo "rswasip2: SKIPPED (rustc has no $RUST_TARGET std;" \
       "rustup target add $RUST_TARGET)"
  exit 0
fi
if ! command -v wasmtime >/dev/null 2>&1; then
  echo "rswasip2: SKIPPED (wasmtime not on PATH)"
  exit 0
fi

WASMTIME=$(command -v wasmtime)

WORK="$BUILD/rswasip2run"
rm -rf "$WORK"; mkdir -p "$WORK"

printf 'one\ntwo\nthree\n' > "$WORK/stdin.txt"
ARGS=(alpha beta)
ENVS=(--env PROBE=secret --env OTHER=2)
LEVELS=${RSWASIP2_LEVELS:--O0 -O1 -O2}

fail=0
pass=0

# rustc's std cannot make a symlink under wasip2 for the same reason tinygo's
# cannot, so the links the sandbox case walks are made here.
seed_dir() {
  rm -rf "$1"; mkdir -p "$1"
  printf 'seed\n' > "$1/seed.txt"
  mkdir -p "$1/sub"
  printf 'nested\n' > "$1/sub/inner.txt"
  ln -s seed.txt              "$1/inside"
  ln -s /etc/hosts            "$1/abs-link"
  ln -s ../../../../etc/hosts "$1/rel-link"
  ln -s ../..                 "$1/up"
}
snapshot() {
  ( cd "$1" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) 2>/dev/null
}

run_case() {
  local name="$1"
  local src="test/rswasip2/$name.rs"
  local wasm="$WORK/$name.wasm"

  if ! rustc --target "$RUST_TARGET" -O -o "$wasm" "$src" \
       2>"$WORK/$name.rustc.err"; then
    echo "FAIL[rswasip2/$name]: rustc could not build it"
    sed 's/^/    /' "$WORK/$name.rustc.err"
    fail=1; return
  fi

  local dr="$WORK/$name.dir.ref"
  seed_dir "$dr"
  env -i "$WASMTIME" run --dir "$dr::/d" "${ENVS[@]}" "$wasm" "${ARGS[@]}" \
    >"$WORK/$name.ref.out" 2>"$WORK/$name.ref.err" <"$WORK/stdin.txt"
  local rc_ref=$?
  snapshot "$dr" > "$WORK/$name.ref.tree"

  for lvl in $LEVELS; do
    local tag="$name$lvl"
    local exe="$WORK/$tag"
    if ! ./$BUILD/warmc --wasi $lvl $WARMC_TARGET_FLAGS \
          -o "$exe" "$wasm" 2>"$WORK/$tag.link.err"; then
      echo "FAIL[rswasip2/$tag]: warmc could not build it"
      sed 's/^/    /' "$WORK/$tag.link.err"
      fail=1; continue
    fi
    local dw="$WORK/$tag.dir"
    seed_dir "$dw"
    env -i "$exe" --dir="$dw::/d" "${ENVS[@]}" -- "${ARGS[@]}" \
      >"$WORK/$tag.out" 2>"$WORK/$tag.err" <"$WORK/stdin.txt"
    local rc=$?
    snapshot "$dw" > "$WORK/$tag.tree"

    local bad=0
    if [ "$rc" != "$rc_ref" ]; then
      echo "FAIL[rswasip2/$tag]: exit $rc, wasmtime exits $rc_ref"
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.out" "$WORK/$name.ref.out"; then
      echo "FAIL[rswasip2/$tag]: stdout differs from wasmtime"
      diff "$WORK/$name.ref.out" "$WORK/$tag.out" | head -10 | sed 's/^/    /'
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.err" "$WORK/$name.ref.err"; then
      echo "FAIL[rswasip2/$tag]: stderr differs from wasmtime"
      diff "$WORK/$name.ref.err" "$WORK/$tag.err" | head -10 | sed 's/^/    /'
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.tree" "$WORK/$name.ref.tree"; then
      echo "FAIL[rswasip2/$tag]: the directory it left differs from wasmtime's"
      diff "$WORK/$name.ref.tree" "$WORK/$tag.tree" | head -12 | sed 's/^/    /'
      bad=1
    fi
    if [ "$bad" = 0 ]; then pass=$((pass + 1)); else fail=1; fi
  done
}

n=0
for src in test/rswasip2/*.rs; do
  run_case "$(basename "$src" .rs)"
  n=$((n + 1))
done

# What this guest asks for that the host does not answer.  The same check the
# tinygo suite makes, and the one that would have caught the version mismatch
# the moment a Rust component was built: rustc asks for @0.2.12 and the host's
# table is keyed without a version, so both sides lose it before comparing.
declared=$(for w in "$WORK"/*.wasm; do
             [ -f "$w" ] || continue
             ./$BUILD/warmc --dump-link "$w" 2>/dev/null |
               sed -nE 's/^canon [0-9]+: lower host "([^"]+)" "([^"]+)".*/\1#\2/p'
           done | sed -E 's/@[0-9.]+#/#/' | LC_ALL=C sort -u)
bound=$(sed -nE 's/.*\{ "(wasi:[^"]+)".*/\1/p' rt/wasip2.c |
        sed -E 's/"$//' | LC_ALL=C sort -u)
missing=$(comm -23 <(echo "$declared") <(echo "$bound"))
if [ -n "$missing" ]; then
  echo "FAIL[rswasip2/surface]: a Rust component imports these, bound nowhere:"
  echo "$missing" | sed 's/^/    /'
  fail=1
else
  pass=$((pass + 1))
fi

if [ "$fail" = 0 ]; then
  echo "rswasip2: $n Rust component(s) at" \
       "$(echo $LEVELS | wc -w | tr -d ' ') level(s) agree with wasmtime on" \
       "output, exit status and the directory left behind"
else
  echo "rswasip2: failures above"
fi
exit $fail
