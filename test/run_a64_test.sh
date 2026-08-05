#!/usr/bin/env bash

set -uo pipefail
BUILD=${1:-build}
mkdir -p "$BUILD/a64"
OURS="$BUILD/a64/ours.bin"
SRC="$BUILD/a64/expected.s"
"$BUILD/a64_test" "$OURS" "$SRC" || exit 1

if command -v aarch64-linux-gnu-as >/dev/null; then
  AS="aarch64-linux-gnu-as"; ORACLE="GNU as"
elif command -v llvm-mc >/dev/null; then
  AS="llvm-mc -triple=aarch64-linux-gnu -filetype=obj -o"; ORACLE="llvm-mc"
else
  AS="clang --target=aarch64-linux-gnu -c -o"; ORACLE="clang -integrated-as"
fi
if command -v aarch64-linux-gnu-objcopy >/dev/null; then
  OBJCOPY="aarch64-linux-gnu-objcopy"
elif command -v llvm-objcopy >/dev/null; then
  OBJCOPY="llvm-objcopy"
else
  echo "a64 encoder: no objcopy available, skipping" >&2; exit 0
fi

case "$AS" in
  *" -o") $AS "$BUILD/a64/expected.o" "$SRC" || exit 1 ;;
  *)      $AS -o "$BUILD/a64/expected.o" "$SRC" || exit 1 ;;
esac
$OBJCOPY -O binary --only-section=.text \
  "$BUILD/a64/expected.o" "$BUILD/a64/expected.bin" || exit 1

filesize() { wc -c < "$1" | tr -d ' '; }

if cmp -s "$OURS" "$BUILD/a64/expected.bin"; then
  echo "a64 encoder: OK ($(( $(filesize "$OURS") / 4 )) instructions match $ORACLE)"
  exit 0
fi

echo "a64 encoder: MISMATCH" >&2

python3 - "$OURS" "$BUILD/a64/expected.bin" "$SRC" <<'PY'
import sys
ours = open(sys.argv[1],'rb').read()
exp  = open(sys.argv[2],'rb').read()
lines = [l.strip() for l in open(sys.argv[3]) if l.strip() and not l.strip().startswith('.')]
n = min(len(ours), len(exp)) // 4
bad = 0
for i in range(n):
    a = int.from_bytes(ours[i*4:i*4+4], 'little')
    b = int.from_bytes(exp[i*4:i*4+4], 'little')
    if a != b:
        bad += 1
        src = lines[i] if i < len(lines) else '?'
        print(f"  [{i:3d}] {src:34s} ours=0x{a:08x} expected=0x{b:08x} xor=0x{a^b:08x}")
if len(ours) != len(exp):
    print(f"  length differs: ours={len(ours)} expected={len(exp)}")
print(f"  {bad} mismatched instruction(s) of {n}")
PY
exit 1
