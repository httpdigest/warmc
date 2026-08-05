#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
. ./test/hostenv.sh

if ! command -v tinygo >/dev/null 2>&1; then
  echo "gowasi: SKIPPED (tinygo not on PATH)"
  exit 0
fi
if ! command -v wasmtime >/dev/null 2>&1; then
  echo "gowasi: SKIPPED (wasmtime not on PATH)"
  exit 0
fi

WASMTIME=$(command -v wasmtime)

WORK="$BUILD/gowasirun"
rm -rf "$WORK"; mkdir -p "$WORK"

printf 'secret\n' > "$WORK/escape.secret"

printf 'alpha\nbeta\ngamma\n' > "$WORK/stdin.txt"

LEVELS=${GOWASI_LEVELS:--O0 -O1 -O2}
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

  local tflags
  tflags=$(sed -n 's|^// tinygo: *||p' "$src" | head -1)
  [ -n "$tflags" ] || tflags="-scheduler=none"
  if ! tinygo build -o "$wasm" -target=wasip1 $tflags "$src" \
       2>"$WORK/$name.tinygo.err"; then
    echo "FAIL[gowasi/$name]: tinygo could not build it"
    sed 's/^/    /' "$WORK/$name.tinygo.err"
    fail=1; return
  fi

  local dr="$WORK/$name.dir.ref"
  seed_dir "$dr"
  env -i "$WASMTIME" --dir "$dr::/d" "$wasm" \
    >"$WORK/$name.ref.out" 2>"$WORK/$name.ref.err" <"$WORK/stdin.txt"
  local rc_ref=$?
  snapshot "$dr" > "$WORK/$name.ref.tree"

  for lvl in $LEVELS; do
    local tag="$name$lvl"
    local exe="$WORK/$tag"
    if ! ./$BUILD/warmc --wasi $lvl $WARMC_TARGET_FLAGS \
          -o "$exe" "$wasm" 2>"$WORK/$tag.link.err"; then
      echo "FAIL[gowasi/$tag]: warmc could not build it"
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
      echo "FAIL[gowasi/$tag]: exit $rc, wasmtime exits $rc_ref"
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.out" "$WORK/$name.ref.out"; then
      echo "FAIL[gowasi/$tag]: stdout differs from wasmtime"
      diff "$WORK/$name.ref.out" "$WORK/$tag.out" | head -10 | sed 's/^/    /'
      bad=1
    fi

    if [ "$rc" = 0 ] && [ "$rc_ref" = 0 ] &&
       ! cmp -s "$WORK/$tag.err" "$WORK/$name.ref.err"; then
      echo "FAIL[gowasi/$tag]: stderr differs from wasmtime"
      diff "$WORK/$name.ref.err" "$WORK/$tag.err" | head -10 | sed 's/^/    /'
      bad=1
    fi
    if ! cmp -s "$WORK/$tag.tree" "$WORK/$name.ref.tree"; then
      echo "FAIL[gowasi/$tag]: the directory it left differs from wasmtime's"
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

if [ "$fail" = 0 ]; then
  echo "gowasi: $n Go program(s) at $(echo $LEVELS | wc -w | tr -d ' ') level(s)" \
       "agree with wasmtime on output, exit status and the directory left behind"
else
  echo "gowasi: failures above"
fi
exit $fail
