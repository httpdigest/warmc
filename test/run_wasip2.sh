#!/usr/bin/env bash

# The parts of the wasip2 surface the shared Go programs do not reach.
#
# test/gowasi/*.go is built for both preview1 and preview2, so it can take
# neither arguments nor an environment: wasi:cli/environment, the exit status
# a run returns, and stdin as a wasi:io stream were all uncovered.  These
# three cases are wasip2-only for that reason, and each is still differential --
# wasmtime answers all of them.

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
. ./test/hostenv.sh

case "${WARMC_TARGET_FLAGS:-}" in
  *--target=windows*)
    echo "wasip2: SKIPPED (there is no warmwasip2-windows.a -- the preview2"
    echo "    host on Windows is a follow-up to the preview1 one in"
    echo "    rt/winposix.c)"
    exit 0;;
esac
if ! command -v tinygo >/dev/null 2>&1; then
  echo "wasip2: SKIPPED (tinygo not on PATH)"
  exit 0
fi
if ! command -v wasmtime >/dev/null 2>&1; then
  echo "wasip2: SKIPPED (wasmtime not on PATH)"
  exit 0
fi

WASMTIME=$(command -v wasmtime)

WORK="$BUILD/wasip2run"
rm -rf "$WORK"; mkdir -p "$WORK"

printf 'one\ntwo\nthree\n' > "$WORK/stdin.txt"
ARGS=(alpha beta)
ENVS=(--env PROBE=secret --env OTHER=2)

fail=0
pass=0

# TinyGo's wasip2 symlink() resolves the link *target* as if it were a path
# under a preopen, so os.Symlink can never succeed and a guest cannot make one.
# The links the sandbox cases have to walk are therefore made out here, where
# both engines see the same tree.
seed_dir() {
  rm -rf "$1"; mkdir -p "$1"
  printf 'seed\n' > "$1/seed.txt"
  mkdir -p "$1/sub"
  printf 'nested\n' > "$1/sub/inner.txt"
  ln -s seed.txt          "$1/inside"
  ln -s /etc/hosts        "$1/abs-link"
  ln -s ../../../../etc/hosts "$1/rel-link"
  ln -s ../..             "$1/up"
  ln -s absent            "$1/dangling"
  ln -s sub               "$1/subdir-link"
}
snapshot() {
  ( cd "$1" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) 2>/dev/null
}

run_case() {
  local name="$1"
  local src="test/wasip2/$name.go"
  local wasm="$WORK/$name.wasm"
  local exe="$WORK/$name"

  if ! tinygo build -o "$wasm" -target=wasip2 "$src" \
       2>"$WORK/$name.tinygo.err"; then
    echo "FAIL[wasip2/$name]: tinygo could not build it"
    sed 's/^/    /' "$WORK/$name.tinygo.err"
    fail=1; return
  fi
  if ! ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS ${WARMC_FLAGS:-} \
        -o "$exe" "$wasm" 2>"$WORK/$name.link.err"; then
    echo "FAIL[wasip2/$name]: warmc could not build it"
    sed 's/^/    /' "$WORK/$name.link.err"
    fail=1; return
  fi

  local dw="$WORK/$name.dir" dr="$WORK/$name.dir.ref"
  seed_dir "$dw"; seed_dir "$dr"
  env -i "$exe" --dir="$dw::/d" "${ENVS[@]}" -- "${ARGS[@]}" \
    >"$WORK/$name.out" 2>"$WORK/$name.err" <"$WORK/stdin.txt"
  local rc=$?
  env -i "$WASMTIME" run --dir "$dr::/d" "${ENVS[@]}" "$wasm" "${ARGS[@]}" \
    >"$WORK/$name.ref.out" 2>"$WORK/$name.ref.err" <"$WORK/stdin.txt"
  local rc_ref=$?
  snapshot "$dw" > "$WORK/$name.tree"
  snapshot "$dr" > "$WORK/$name.ref.tree"

  local bad=0
  if ! cmp -s "$WORK/$name.tree" "$WORK/$name.ref.tree"; then
    echo "FAIL[wasip2/$name]: the directory it left differs from wasmtime's"
    diff "$WORK/$name.ref.tree" "$WORK/$name.tree" | head -12 | sed 's/^/    /'
    bad=1
  fi
  if [ "$rc" != "$rc_ref" ]; then
    echo "FAIL[wasip2/$name]: exit $rc, wasmtime exits $rc_ref"
    bad=1
  fi
  if ! cmp -s "$WORK/$name.out" "$WORK/$name.ref.out"; then
    echo "FAIL[wasip2/$name]: stdout differs from wasmtime"
    diff "$WORK/$name.ref.out" "$WORK/$name.out" | head -10 | sed 's/^/    /'
    bad=1
  fi
  if ! cmp -s "$WORK/$name.err" "$WORK/$name.ref.err"; then
    echo "FAIL[wasip2/$name]: stderr differs from wasmtime"
    diff "$WORK/$name.ref.err" "$WORK/$name.err" | head -10 | sed 's/^/    /'
    bad=1
  fi
  if [ "$bad" = 0 ]; then pass=$((pass + 1)); else fail=1; fi
}

for src in test/wasip2/*.go; do
  run_case "$(basename "$src" .go)"
done

# The hand-written probe.  warmc's text format assembles core wasm and not
# component text, so this goes through the pinned wasm-tools the same way
# test/components/calc does -- embedded against the WIT tree tinygo itself
# builds against, so the two are asking for the same interfaces.
WASM_TOOLS="$BUILD/tools/bin/wasm-tools"
WIT_DIR="$(tinygo env TINYGOROOT 2>/dev/null)/lib/wasi-cli/wit"
if [ ! -x "$WASM_TOOLS" ]; then
  echo "wasip2: $WASM_TOOLS not found (run \`make wasm-tools\`)"
  fail=1
elif [ ! -d "$WIT_DIR" ]; then
  echo "wasip2: no wasi WIT beside tinygo at $WIT_DIR, skipping the probe"
else
  P="$WORK/probe"
  if ! "$WASM_TOOLS" parse test/wasip2/probe/core.wat -o "$P.core.wasm" \
         2>"$P.err" ||
     ! "$WASM_TOOLS" component embed --world wasi:cli/command "$WIT_DIR" \
         "$P.core.wasm" -o "$P.emb.wasm" 2>>"$P.err" ||
     ! "$WASM_TOOLS" component new "$P.emb.wasm" -o "$P.wasm" 2>>"$P.err"; then
    echo "FAIL[wasip2/probe]: wasm-tools could not build the component"
    sed 's/^/    /' "$P.err"
    fail=1
  elif ! ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS -o "$P.exe" "$P.wasm" \
          2>"$P.link.err"; then
    echo "FAIL[wasip2/probe]: warmc could not build it"
    sed 's/^/    /' "$P.link.err"
    fail=1
  else
    dw="$WORK/probe.dir"; dr="$WORK/probe.dir.ref"
    rm -rf "$dw" "$dr"; mkdir -p "$dw" "$dr"
    env -i "$P.exe" --dir="$dw::/d" >"$P.out" 2>"$P.stderr"
    rcw=$?
    env -i "$WASMTIME" run --dir "$dr::/d" "$P.wasm" >"$P.ref.out" 2>"$P.ref.stderr"
    rcr=$?
    want=$(printf '1%.0s' $(seq 1 17))
    got=$(tr -d '\n' < "$P.out")
    if [ "$rcw" != "$rcr" ] || ! cmp -s "$P.out" "$P.ref.out"; then
      echo "FAIL[wasip2/probe]: exit $rcw/$rcr, warmc [$got]," \
           "wasmtime [$(tr -d '\n' < "$P.ref.out")]"
      sed 's/^/    /' "$P.stderr"
      fail=1
    elif [ "$got" != "$want" ]; then
      echo "FAIL[wasip2/probe]: both engines answer [$got], want [$want]"
      echo "    (one digit per check, in the order core.wat makes them)"
      fail=1
    else
      pass=$((pass + 1))
    fi
  fi
fi

# A core module is not a component and has no wasi:cli/run to call, so --wasi
# has to reach the preview1 host for it and the preview2 host for a component.
# Getting that backwards links a program against a host that cannot start it.
probe="$WORK/hello.wat"
cat > "$probe" <<'WAT'
(module
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (func (export "_start") (call $exit (i32.const 0))))
WAT
if ./$BUILD/warmc --emit-wasm -o "$WORK/hello.wasm" "$probe" 2>/dev/null &&
   ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS -o "$WORK/hello" "$WORK/hello.wasm" \
     2>"$WORK/hello.err" && "$WORK/hello"; then
  pass=$((pass + 1))
else
  echo "FAIL[wasip2/hosts]: --wasi no longer builds a preview1 core module"
  sed 's/^/    /' "$WORK/hello.err"
  fail=1
fi

# The two ways of asking for the wrong host.  Both have to be refused before
# anything is written, and to name the flag: a command linked without --wasi
# would build a program whose every wasi call traps, and --wasi on a component
# with no run has nothing to start.
refuses() {
  local what="$1" want="$2"; shift 2
  rm -f "$WORK/refused"
  if "$@" -o "$WORK/refused" 2>"$WORK/refused.err"; then
    echo "FAIL[wasip2/$what]: warmc built it instead of refusing"
    fail=1
  elif ! grep -q "$want" "$WORK/refused.err"; then
    echo "FAIL[wasip2/$what]: refused without saying \"$want\":"
    sed 's/^/    /' "$WORK/refused.err"
    fail=1
  elif [ -e "$WORK/refused" ]; then
    echo "FAIL[wasip2/$what]: refused and left $WORK/refused behind"
    fail=1
  else
    pass=$((pass + 1))
  fi
}

if [ -f "$WORK/probe.wasm" ]; then
  refuses "command-needs-wasi" "pass --wasi" \
    ./$BUILD/warmc $WARMC_TARGET_FLAGS "$WORK/probe.wasm"
fi
if [ -f "$BUILD/components/restable.wasm" ]; then
  refuses "wasi-needs-command" "does not export" \
    ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS "$BUILD/components/restable.wasm"
fi

# Coverage, in the direction the gowasip2 surface check does not go: every
# wasi:* function rt/wasip2.c binds has to be one some component here asks
# for, or it is host code no test reaches.  All 43 are asked for.
#
# Asked for is not the same as entered.  A trace of every host call across the
# three wasip2 suites enters 53 of the 57, and the four it does not need
# something a suite cannot arrange:
#
#   [resource-drop]error            an `error` resource exists only after a
#                                   stream operation fails with
#                                   last-operation-failed, and every failure a
#                                   test can arrange portably -- a closed
#                                   pipe, a read past the end -- is
#                                   stream-error::closed, which carries none.
#   [resource-drop]terminal-input   a terminal handle exists only when the
#   [resource-drop]terminal-output  descriptor is a tty, and the harness
#                                   redirects all three to files so that its
#                                   output can be compared at all.
#   descriptor.get-flags            rustc's std imports it and these four
#                                   programs never reach the path that calls
#                                   it.
#
# These seven are the route Rust's std takes -- the non-blocking read, and a
# file reached through a stream -- and nothing
# a tinygo guest can be made to ask for: it reaches a file with
# descriptor.read and descriptor.write, and Rust opens a stream over the
# descriptor instead.  The eighth, exit-with-code, is not in the WIT tinygo
# builds against at all -- the probe is embedded against that WIT too -- and
# is how rustc's std exits with anything but 0 or 1.  test/run_rswasip2.sh
# covers all eight (exitcode.rs for this one) and makes the same
# check against the same table, so they are named here rather than left to
# pass by being invisible to this one.
RS_ONLY="wasi:cli/exit#exit-with-code
         wasi:io/streams#[method]input-stream.read
         wasi:filesystem/types#[method]descriptor.append-via-stream
         wasi:filesystem/types#[method]descriptor.get-flags
         wasi:filesystem/types#[method]descriptor.metadata-hash
         wasi:filesystem/types#[method]descriptor.metadata-hash-at
         wasi:filesystem/types#[method]descriptor.read-via-stream
         wasi:filesystem/types#[method]descriptor.write-via-stream"
bound=$(sed -nE 's/.*\{ "(wasi:[^"]+)".*/\1/p' rt/wasip2.c |
        grep -vxF "$(echo "$RS_ONLY" | sed 's/^ *//')" | LC_ALL=C sort -u)
imported=$(for w in "$WORK"/*.wasm; do
             [ -f "$w" ] || continue
             ./$BUILD/warmc --dump-link "$w" 2>/dev/null |
               sed -nE 's/^canon [0-9]+: lower host "([^"]+)" "([^"]+)".*/\1#\2/p'
           done | sed -E 's/@[0-9.]+#/#/' | LC_ALL=C sort -u)
unasked=$(comm -23 <(echo "$bound") <(echo "$imported"))
if [ -n "$unasked" ]; then
  echo "FAIL[wasip2/coverage]: rt/wasip2.c binds these and no case asks for them:"
  echo "$unasked" | sed 's/^/    /'
  fail=1
else
  pass=$((pass + 1))
fi

if [ "$fail" = 0 ]; then
  echo "wasip2: $pass case(s) pass by agreeing with wasmtime on stdout," \
       "stderr and exit status"
else
  echo "wasip2: failures above"
fi
exit $fail
