#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
. ./test/hostenv.sh

if ! command -v tinygo >/dev/null 2>&1; then
  echo "gowasip2: SKIPPED (tinygo not on PATH)"
  exit 0
fi
if ! command -v wasmtime >/dev/null 2>&1; then
  echo "gowasip2: SKIPPED (wasmtime not on PATH)"
  exit 0
fi

WASMTIME=$(command -v wasmtime)

WORK="$BUILD/gowasip2run"
rm -rf "$WORK"; mkdir -p "$WORK"

printf 'alpha\nbeta\ngamma\n' > "$WORK/stdin.txt"

LEVELS=${GOWASIP2_LEVELS:--O0 -O1 -O2}
fail=0
pass=0

seed_dir() {
  rm -rf "$1"; mkdir -p "$1"
  printf 'seed\n' > "$1/seed.txt"
}
snapshot() {
  ( cd "$1" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) 2>/dev/null
}

run_case() {
  local name="$1"
  local src="test/gowasi/$name.go"
  local wasm="$WORK/$name.wasm"

  # The wasip2 target is a component, so it decides its own scheduler; the
  # `// tinygo:` line the wasip1 suite reads is a wasip1 flag set.
  if ! tinygo build -o "$wasm" -target=wasip2 "$src" \
       2>"$WORK/$name.tinygo.err"; then
    echo "FAIL[gowasip2/$name]: tinygo could not build it"
    sed 's/^/    /' "$WORK/$name.tinygo.err"
    fail=1; return
  fi

  local dr="$WORK/$name.dir.ref"
  seed_dir "$dr"
  env -i "$WASMTIME" run --dir "$dr::/d" "$wasm" \
    >"$WORK/$name.ref.out" 2>"$WORK/$name.ref.err" <"$WORK/stdin.txt"
  local rc_ref=$?
  snapshot "$dr" > "$WORK/$name.ref.tree"

  for lvl in $LEVELS; do
    local tag="$name$lvl"
    local exe="$WORK/$tag"
    if ! ./$BUILD/warmc --wasi $lvl $WARMC_TARGET_FLAGS \
          -o "$exe" "$wasm" 2>"$WORK/$tag.link.err"; then
      echo "FAIL[gowasip2/$tag]: warmc could not build it"
      sed 's/^/    /' "$WORK/$tag.link.err"
      fail=1; continue
    fi
    local dw="$WORK/$tag.dir"
    seed_dir "$dw"
    env -i "$exe" --dir="$dw::/d" \
      >"$WORK/$tag.out" 2>"$WORK/$tag.err" <"$WORK/stdin.txt"
    local rc=$?
    snapshot "$dw" > "$WORK/$tag.tree"

    local bad=0
    if [ "$rc" != "$rc_ref" ]; then
      echo "FAIL[gowasip2/$tag]: exit $rc, wasmtime exits $rc_ref"
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.out" "$WORK/$name.ref.out"; then
      echo "FAIL[gowasip2/$tag]: stdout differs from wasmtime"
      diff "$WORK/$name.ref.out" "$WORK/$tag.out" | head -10 | sed 's/^/    /'
      bad=1
    fi
    if [ "$rc" = 0 ] && [ "$rc_ref" = 0 ] &&
       ! cmp -s "$WORK/$tag.err" "$WORK/$name.ref.err"; then
      echo "FAIL[gowasip2/$tag]: stderr differs from wasmtime"
      diff "$WORK/$name.ref.err" "$WORK/$tag.err" | head -10 | sed 's/^/    /'
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.tree" "$WORK/$name.ref.tree"; then
      echo "FAIL[gowasip2/$tag]: the directory it left differs from wasmtime's"
      diff "$WORK/$name.ref.tree" "$WORK/$tag.tree" | head -12 | sed 's/^/    /'
      bad=1
    fi
    if [ "$bad" = 0 ]; then pass=$((pass + 1)); else fail=1; fi
  done
}

n=0
for src in test/gowasi/*.go; do
  run_case "$(basename "$src" .go)"
  n=$((n + 1))
done

# Every wasi:* function these components import has to be one the host binds,
# or the program traps the first time it is reached rather than at the link.
# The host's table is keyed without a version, because the interface a guest
# asks for carries one and it is not the same one for every guest: tinygo says
# wasi:io/streams@0.2.0 and rustc says @0.2.12.  So the version comes off both
# sides before they are compared.
declared=$(for w in "$WORK"/*.wasm; do
             ./$BUILD/warmc --dump-link "$w" 2>/dev/null |
               sed -nE 's/^canon [0-9]+: lower host "([^"]+)" "([^"]+)".*/\1#\2/p'
           done | sed -E 's/@[0-9.]+#/#/' | LC_ALL=C sort -u)
bound=$(sed -nE 's/.*\{ "(wasi:[^"]+)".*/\1/p' rt/wasip2.c |
        sed -E 's/"$//' | LC_ALL=C sort -u)
missing=$(comm -23 <(echo "$declared") <(echo "$bound"))
if [ -n "$missing" ]; then
  echo "FAIL[gowasip2/surface]: imported by a test component, bound nowhere:"
  echo "$missing" | sed 's/^/    /'
  fail=1
else
  pass=$((pass + 1))
fi

if [ "$fail" = 0 ]; then
  echo "gowasip2: $n Go component(s) at $(echo $LEVELS | wc -w | tr -d ' ') level(s)" \
       "agree with wasmtime on output, exit status and the directory left behind"
else
  echo "gowasip2: failures above"
fi
exit $fail
