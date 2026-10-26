#!/usr/bin/env bash
# Drives test/wasi-driven/echoserver.wat, built: starts EXE with a listener
# and a directory, makes its two connections, and checks what came back.
# The replies are written out here: wasmtime has no listener for preview 1.
# The module says "ready" on each connection before it reads: on the first,
# this then waits a while, so the guest's recv has to wait for it; the second
# it reads before.
#
# Usage: test/wasi-driven/echoserver.sh EXE WORKDIR
# Exits 0 when all of it is as it must be; prints FAIL lines otherwise.

exe=$1 work=$2
d="$work/echoserver.dir"; rm -rf "$d"; mkdir -p "$d"

# Nobody connects before the guest says "up": its first sock_accept has to
# find no one.  A port another program has makes it exit 74 at once.
port=0
for try in $(seq 0 9); do
  cand=$((34500 + RANDOM % 1000))
  "$exe" --tcplisten=127.0.0.1:$cand --dir="$d::/d" \
    >"$work/echo.out" 2>"$work/echo.err" &
  srv=$!
  for i in $(seq 1 200); do
    if grep -q up "$work/echo.out"; then port=$cand; break; fi
    kill -0 $srv 2>/dev/null || break
    perl -e 'select(undef,undef,undef,0.05)'
  done
  [ "$port" != 0 ] && break
  kill $srv 2>/dev/null; wait $srv 2>/dev/null
  grep -q 'cannot listen' "$work/echo.err" || break
done
if [ "$port" = 0 ] || ! exec 3<>/dev/tcp/127.0.0.1/$port; then
  echo "FAIL[wasi/echoserver]: the guest never came up on a port, or said EAGAIN"
  echo "    to no connection (exit 55, test/wasi-driven/echoserver.wat)"
  sed 's/^/    /' "$work/echo.err"
  exit 1
fi 2>/dev/null

# Each read gives up after 10s, so that a guest gone wrong fails the case
# rather than hanging the run, and each write is a subshell's, which a guest
# gone already kills with SIGPIPE rather than this script.
ready1= ready2= reply1= reply2=
read -r -t 10 -n 5 ready1 <&3
perl -e 'select(undef,undef,undef,0.3)'
( printf 'hi' >&3 ) 2>/dev/null
reply1=$(perl -e 'alarm 10; local $/; print <STDIN>' <&3)
exec 3<&-
if exec 3<>/dev/tcp/127.0.0.1/$port; then
  read -r -t 10 -n 5 ready2 <&3
  ( printf 'yo' >&3 ) 2>/dev/null
  reply2=$(perl -e 'alarm 10; local $/; print <STDIN>' <&3)
  exec 3<&-
fi 2>/dev/null
perl -e 'sleep 10; kill "TERM", shift' $srv &
watch=$!
wait $srv; rc=$?
kill $watch 2>/dev/null; wait $watch 2>/dev/null
said=$(tr -d '\n' < "$work/echo.out")
got="$ready1 [$reply1] $ready2 [$reply2] $said $rc"
want="ready [echo: hi] ready [echo: yo] up22 0"
if [ "$got" != "$want" ]; then
  echo "FAIL[wasi/echoserver]: got [$got]"
  echo "    want [$want]: each connection's greeting and reply, stdout and the exit"
  echo "    status, which names the step that failed (test/wasi-driven/echoserver.wat)"
  sed 's/^/    /' "$work/echo.err"
  exit 1
fi
exit 0
