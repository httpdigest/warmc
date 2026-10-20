#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/elfrun"
mkdir -p "$WORK"

one() {
  local wasm="$1" name
  name=$(basename "$wasm" .wasm)
  if ! python3 test/check_elf.py "./$BUILD/warmc" "$wasm" "$BUILD/warmrt.a" \
       >"$WORK/$name.chk" 2>&1; then
    { echo "FAIL[elf] $name:"; sed 's/^/    /' "$WORK/$name.chk"; } >"$WORK/$name.fail"
    return 1
  fi
  return 0
}
export -f one
export BUILD WORK

rm -f "$WORK"/*.fail
total=0
for wasm in "$BUILD"/testrun/*.wasm; do total=$((total+1)); done
jobs=${ELF_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
ls "$BUILD"/testrun/*.wasm | xargs -P "$jobs" -n1 -I{} bash -c 'one "$@"' _ {}

fail=0
for f in "$WORK"/*.fail; do
  [ -e "$f" ] || continue
  cat "$f"
  fail=$((fail+1))
done

# The WASI hosts answer their POSIX calls with rt/linuxposix.c on Linux, so a
# --wasi program links into one static image as well: no libc, and nothing
# for a loader to resolve.  Running one needs Linux (make check-linux runs the
# whole tinygo corpus that way); this says the link closes, and that the image
# is an aarch64 executable asking for no interpreter and no dynamic section.
static_image() {
  python3 - "$1" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
if d[:4] != b"\x7fELF": sys.exit("not an ELF file")
etype, emach = struct.unpack_from("<HH", d, 16)
if etype != 2 or emach != 183: sys.exit("e_type %d e_machine %d" % (etype, emach))
phoff, = struct.unpack_from("<Q", d, 32)
phentsize, phnum = struct.unpack_from("<HH", d, 54)
for i in range(phnum):
    ptype, = struct.unpack_from("<I", d, phoff + i * phentsize)
    if ptype in (2, 3): sys.exit("program header %d is PT_%s" % (i, "DYNAMIC" if ptype == 2 else "INTERP"))
PY
}
wasi_one() {
  local tag="$1" src="$2"
  if ! ./$BUILD/warmc --wasi -O2 --target=linux --rt "$BUILD/warmrt.a" \
       -o "$WORK/$tag" "$src" >"$WORK/$tag.err" 2>&1; then
    echo "FAIL[elf] $tag: the --wasi link did not close"
    sed 's/^/    /' "$WORK/$tag.err"
    fail=$((fail+1))
  elif ! why=$(static_image "$WORK/$tag" 2>&1); then
    echo "FAIL[elf] $tag: $why"
    fail=$((fail+1))
  fi
  total=$((total+1))
}
wasi_one wasi-p1 test/wasi/hello.wat
if command -v tinygo >/dev/null 2>&1 &&
   tinygo build -o "$WORK/wasi-p2.wasm" -target=wasip2 test/wasip2/args.go \
     >"$WORK/wasi-p2.tinygo.err" 2>&1; then
  wasi_one wasi-p2 "$WORK/wasi-p2.wasm"
fi
pass=$((total - fail))

echo "elf: $pass module(s) cross-built, linked and verified, $fail failure(s)"
[ "$fail" -eq 0 ]
