#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
. ./test/hostenv.sh

if ! command -v wasmtime >/dev/null 2>&1; then
  echo "wasi: wasmtime not installed, skipping"
  exit 0
fi

WASMTIME=$(command -v wasmtime)

case "${WARMC_TARGET_FLAGS:-}" in
  *--target=windows*) WIN=1;;
  *) WIN=0;;
esac

FTAG=$(printf '%s' "${WARMC_FLAGS:-default}" | tr -c 'A-Za-z0-9' '_')
WORK="$BUILD/wasirun-$FTAG"
rm -rf "$WORK"; mkdir -p "$WORK"

fail=0
pass=0

printf 'ping\n' > "$WORK/stdin.txt"
ARGS=(alpha beta)

run_case() {
  local name="$1"; shift
  local src="test/wasi/$name.wat"
  local wasm="$WORK/$name.wasm"
  local exe="$WORK/$name"

  if ! ./$BUILD/warmc --emit-wasm -o "$wasm" "$src" 2>"$WORK/$name.asm.err"; then
    echo "FAIL[wasi/$name]: warmc could not assemble it"
    sed 's/^/    /' "$WORK/$name.asm.err"
    fail=1; return
  fi

  if ! ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS ${WARMC_FLAGS:-} \
        -o "$exe" "$wasm" 2>"$WORK/$name.link.err"; then
    echo "FAIL[wasi/$name]: warmc could not build it"
    sed 's/^/    /' "$WORK/$name.link.err"
    fail=1; return
  fi

  local dw="$WORK/$name.dir.warmc" dr="$WORK/$name.dir.ref"
  rm -rf "$dw" "$dr"; mkdir -p "$dw" "$dr"

  printf 'seed\n' > "$dw/seed.txt"; printf 'seed\n' > "$dr/seed.txt"
  printf 'secret\n' > "$WORK/$name.secret"

  env -i WARMC_WASI_PROBE=secret "$exe" --dir="$dw::/d" "${ARGS[@]}" \
    >"$WORK/$name.warmc.out" 2>"$WORK/$name.warmc.err" <"$WORK/stdin.txt"
  local rc_warmc=$?
  env -i WARMC_WASI_PROBE=secret "$WASMTIME" --dir "$dr::/d" "$wasm" "${ARGS[@]}" \
    >"$WORK/$name.ref.out" 2>"$WORK/$name.ref.err" <"$WORK/stdin.txt"
  local rc_ref=$?
  # wasmtime sleeps in whole Windows timer ticks, so it rounds nap's 100us up
  # and says `slow`; there warmc is held to what nap.wat promises instead.
  if [ "$WIN" = 1 ] && [ "$name" = nap ]; then printf 'ok\n' > "$WORK/$name.ref.out"; fi

  local bad=0
  if ! cmp -s "$WORK/$name.warmc.out" "$WORK/$name.ref.out"; then
    echo "FAIL[wasi/$name]: stdout differs from wasmtime"
    echo "  warmc:    $(head -c 200 "$WORK/$name.warmc.out" | tr '\n' '|')"
    echo "  wasmtime: $(head -c 200 "$WORK/$name.ref.out" | tr '\n' '|')"
    bad=1
  fi

  if [ "$rc_warmc" = 0 ] && [ "$rc_ref" = 0 ] &&
     ! cmp -s "$WORK/$name.warmc.err" "$WORK/$name.ref.err"; then
    echo "FAIL[wasi/$name]: stderr differs from wasmtime"
    echo "  warmc:    $(head -c 200 "$WORK/$name.warmc.err" | tr '\n' '|')"
    echo "  wasmtime: $(head -c 200 "$WORK/$name.ref.err" | tr '\n' '|')"
    bad=1
  fi
  if [ "$rc_warmc" != "$rc_ref" ]; then
    echo "FAIL[wasi/$name]: exit $rc_warmc, wasmtime exit $rc_ref"
    bad=1
  fi

  ( cd "$dw" && find . | LC_ALL=C sort && echo -- && find . -type f -exec cat {} + ) \
    > "$WORK/$name.tree.warmc" 2>/dev/null
  ( cd "$dr" && find . | LC_ALL=C sort && echo -- && find . -type f -exec cat {} + ) \
    > "$WORK/$name.tree.ref" 2>/dev/null
  if ! cmp -s "$WORK/$name.tree.warmc" "$WORK/$name.tree.ref"; then
    echo "FAIL[wasi/$name]: the directory each engine left differs"
    diff "$WORK/$name.tree.ref" "$WORK/$name.tree.warmc" | head -12 | sed 's/^/    /'
    bad=1
  fi
  if [ "$bad" = 0 ]; then pass=$((pass + 1)); else fail=1; fi
}

for src in test/wasi/*.wat; do
  run_case "$(basename "$src" .wat)"
done

# The cases under test/wasi-driven need something the loop above cannot do:
# a reader on the other end of a pipe, a peer on a socket, or an answer
# wasmtime does not have.  Each brings its own driver.
build_driven() {
  local name="$1"
  local src="test/wasi-driven/$name.wat"
  DRIVEN_WASM="$WORK/$name.wasm"
  DRIVEN_EXE="$WORK/$name"
  if ! ./$BUILD/warmc --emit-wasm -o "$DRIVEN_WASM" "$src" 2>"$WORK/$name.asm.err"; then
    echo "FAIL[wasi/$name]: warmc could not assemble it"
    sed 's/^/    /' "$WORK/$name.asm.err"
    return 1
  fi
  if ! ./$BUILD/warmc --wasi $WARMC_TARGET_FLAGS ${WARMC_FLAGS:-} \
        -o "$DRIVEN_EXE" "$DRIVEN_WASM" 2>"$WORK/$name.link.err"; then
    echo "FAIL[wasi/$name]: warmc could not build it"
    sed 's/^/    /' "$WORK/$name.link.err"
    return 1
  fi
  return 0
}

# A write to a pipe whose reader has gone has to surface as EPIPE rather than
# as a full write that never happened.  It cannot be run with stdout on a file.
#
# The kernel says EPIPE (Win32 says ERROR_NO_DATA, which is EPIPE too) -- 64,
# the exit this module documents as the healthy one.  wasmtime 47 reported
# EIO for it on Windows, and since 49 its preview1 layer reports EIO on every
# host, so the status is asserted rather than diffed; that both engines fail
# the write at all is still differential.
if build_driven brokenpipe; then
  "$DRIVEN_EXE" 2>/dev/null | head -1 >/dev/null
  rc_warmc=${PIPESTATUS[0]}
  "$WASMTIME" "$DRIVEN_WASM" 2>/dev/null | head -1 >/dev/null
  rc_ref=${PIPESTATUS[0]}
  if [ "$rc_warmc" != 64 ] || [ "$rc_ref" = 0 ]; then
    echo "FAIL[wasi/brokenpipe]: exit $rc_warmc (want 64)," \
         "wasmtime exit $rc_ref (want non-zero)"
    fail=1
  else
    pass=$((pass + 1))
  fi
else
  fail=1
fi

# All four preview1 clocks answer.  wasmtime returns EBADF for ids 2 and 3, so
# the expected output is written out here rather than diffed against it.
if build_driven cpuclock; then
  got=$("$DRIVEN_EXE" 2>/dev/null | tr '\n' ' ')
  want="0 0 0 0 0 0 0 0 1 "
  if [ "$got" != "$want" ]; then
    echo "FAIL[wasi/cpuclock]: got [$got], want [$want]"
    echo "    (eight rcs for res/time over clocks 0..3, then whether"
    echo "     process_cputime_id advanced across a spin)"
    fail=1
  else
    pass=$((pass + 1))
  fi
else
  fail=1
fi

# environ_get, which needs an environment: the plain runner gives none, on
# purpose, because counts.wat is there to say that a variable set in the
# process does not reach a guest that did not ask for it.  Still differential
# -- both engines are handed the same two variables and the same arguments.
if build_driven environ; then
  env -i "$DRIVEN_EXE" --env ONE=first --env TWO=second -- alpha beta \
    >"$WORK/environ.out" 2>/dev/null
  env -i "$WASMTIME" --env ONE=first --env TWO=second "$DRIVEN_WASM" alpha beta \
    >"$WORK/environ.ref" 2>/dev/null
  if ! cmp -s "$WORK/environ.out" "$WORK/environ.ref"; then
    echo "FAIL[wasi/environ]: what the guest was handed differs from wasmtime"
    diff "$WORK/environ.ref" "$WORK/environ.out" | head -12 | sed 's/^/    /'
    fail=1
  else
    pass=$((pass + 1))
  fi
else
  fail=1
fi

# fd_allocate, fd_renumber and an advice outside the enum: preview1 defines
# all three and wasmtime 47 serves none of them, so what is expected is written
# out here rather than diffed against it.  The header on the module says why
# for each.
if build_driven fdextra; then
  d="$WORK/fdextra.dir"; rm -rf "$d"; mkdir -p "$d"
  got=$(env -i "$DRIVEN_EXE" --dir="$d::/d" 2>/dev/null)
  want="0 0 32 28 0 0 4 48 8 8 0 "
  if [ "$got" != "$want" ]; then
    echo "FAIL[wasi/fdextra]: got [$got], want [$want]"
    echo "    (allocate, filestat_get, the size it left, advise with a seventh"
    echo "     value, renumber, a read through the new number, its length, the"
    echo "     byte it read, tell on the old number, renumber past the ceiling,"
    echo "     close)"
    fail=1
  else
    pass=$((pass + 1))
  fi
else
  fail=1
fi

# A path component that is a separator to Win32 and a filename character
# everywhere else.  What warmc answers is the same on every host, so it is
# written out rather than diffed -- see the header on the module.
if build_driven backslash; then
  d="$WORK/backslash.dir"; rm -rf "$d"; mkdir -p "$d"
  printf 'outside
' > "$WORK/backslash.secret"
  got=$(env -i "$DRIVEN_EXE" --dir="$d::/d" 2>/dev/null)
  want="44 63 "
  if [ "$got" != "$want" ]; then
    echo "FAIL[wasi/backslash]: got [$got], want [$want]"
    echo "    (a leaf carrying a backslash is not there; the same escape"
    echo "     spelled with a slash is refused by the sandbox)"
    fail=1
  else
    pass=$((pass + 1))
  fi
else
  fail=1
fi

# A preopened listening socket, which is the only way a guest can ever reach
# the four sock_* calls.  wasmtime 47 refuses -S tcplisten for wasip1, so the
# reply is written out rather than diffed.
if [ "$WIN" = 1 ]; then
  echo "wasi/echoserver: SKIPPED (sockets are not part of the Windows WASI"
  echo "    host -- rt/winposix.c answers the socket calls ENOSYS, so"
  echo "    --tcplisten has nothing to preopen; Winsock is a follow-up)"
elif build_driven echoserver; then
  port=0
  for try in $(seq 0 9); do
    cand=$((34500 + RANDOM % 1000))
    "$DRIVEN_EXE" --tcplisten=127.0.0.1:$cand >"$WORK/echo.out" 2>"$WORK/echo.err" &
    srv=$!
    for i in $(seq 1 60); do
      if exec 3<>/dev/tcp/127.0.0.1/$cand; then port=$cand; break; fi
      perl -e 'select(undef,undef,undef,0.05)'
    done 2>/dev/null
    [ "$port" != 0 ] && break
    kill $srv 2>/dev/null; wait $srv 2>/dev/null
  done
  if [ "$port" = 0 ]; then
    echo "FAIL[wasi/echoserver]: the guest never came up on a port"
    sed 's/^/    /' "$WORK/echo.err"
    fail=1
  else
    printf 'hi' >&3
    reply=$(cat <&3)
    exec 3<&-
    wait $srv; rc=$?
    said=$(tr -d '\n' < "$WORK/echo.out")
    if [ "$reply" != "echo: hi" ] || [ "$said" != 2 ] || [ "$rc" != 0 ]; then
      echo "FAIL[wasi/echoserver]: reply [$reply], stdout [$said], exit $rc"
      echo "    (want reply [echo: hi], stdout [2], exit 0)"
      sed 's/^/    /' "$WORK/echo.err"
      fail=1
    else
      pass=$((pass + 1))
    fi
  fi
else
  fail=1
fi

declared=$(grep -oE '"[a-z0-9_]+"' rt/wasi.wat | tr -d '"' | LC_ALL=C sort -u)
bound=$(grep -oE '\{ "[a-z0-9_]+"' rt/wasi.c | sed 's/{ "//' | tr -d '"' | LC_ALL=C sort -u)
missing=$(comm -23 <(echo "$declared") <(echo "$bound"))
if [ -n "$missing" ]; then
  echo "FAIL[wasi/surface]: declared in rt/wasi.wat but bound nowhere:"
  echo "$missing" | sed 's/^/    /'
  fail=1
else
  pass=$((pass + 1))
fi

# And the other direction: a function the host binds that no module asks for
# is host code no test reaches.  A trace of every call across both preview1
# suites enters all 46; this is the part of that a suite can check by itself.
#
# These eight are reached only through the Go programs in run_gowasi.sh, which
# builds its own modules in its own directory -- so they are named here rather
# than left to make the check pass by being absent from it.
GOWASI_ONLY="fd_filestat_set_size fd_pread fd_pwrite fd_sync
             path_link path_readlink path_remove_directory path_rename"
asked=$( (grep -ho '"wasi_snapshot_preview1" "[a-z0-9_]*"' \
            test/wasi/*.wat test/wasi-driven/*.wat |
            sed -E 's/.*" "([a-z0-9_]*)"/\1/'
          echo $GOWASI_ONLY | tr ' ' '\n') | LC_ALL=C sort -u)
unasked=$(comm -23 <(echo "$bound") <(echo "$asked"))
if [ -n "$unasked" ]; then
  echo "FAIL[wasi/reach]: rt/wasi.c binds these and no module asks for them:"
  echo "$unasked" | sed 's/^/    /'
  fail=1
else
  pass=$((pass + 1))
fi

if [ "$fail" = 0 ]; then
  echo "wasi: $pass case(s) pass, most of them by agreeing with wasmtime on" \
       "stdout, stderr and exit status"
else
  echo "wasi: failures above"
fi
exit $fail
