#!/usr/bin/env bash

set -uo pipefail
BUILD=${1:-build}
mkdir -p "$BUILD/x64"
OURS="$BUILD/x64/ours.bin"
SRC="$BUILD/x64/expected.s"
OFFS="$BUILD/x64/offsets.txt"
"$BUILD/x64_test" "$OURS" "$SRC" "$OFFS" || exit 1

if ! command -v llvm-mc >/dev/null; then
  echo "x64 encoder: llvm-mc not available, skipping" >&2; exit 0
fi

llvm-mc -triple=x86_64-pc-windows-msvc -mcpu=x86-64-v3 -filetype=obj \
  -o "$BUILD/x64/expected.obj" "$SRC" || exit 1

# llvm-objcopy's COFF backend does not implement -O binary (it emits a
# COFF container), so pull .text's raw bytes out ourselves.
python3 - "$BUILD/x64/expected.obj" "$BUILD/x64/expected.bin" <<'PY' || exit 1
import struct, sys
data = open(sys.argv[1], 'rb').read()
nsec, = struct.unpack_from('<H', data, 2)
opt, = struct.unpack_from('<H', data, 16)
off = 20 + opt
for i in range(nsec):
    name, _, _, size, ptr = struct.unpack_from('<8sIIII', data, off + 40 * i)
    if name.rstrip(b'\0') == b'.text':
        open(sys.argv[2], 'wb').write(data[ptr:ptr + size])
        sys.exit(0)
sys.exit('no .text section in ' + sys.argv[1])
PY

filesize() { wc -c < "$1" | tr -d ' '; }

# Every exported emitter must appear in the test at least once, so the
# suite cannot silently fall behind the encoder.
python3 - src/x64.h test/x64_test.c <<'PY' || exit 1
import re, sys
hdr = open(sys.argv[1]).read()
tst = open(sys.argv[2]).read()
decls = set(re.findall(r'\bvoid\s+(x64_[a-z0-9_]+)\s*\(', hdr))
# called directly, or named in a table of emitters the sweeps drive
used = set(re.findall(r'\b(x64_[a-z0-9_]+)\s*[(,}]', tst))
missing = sorted(decls - used)
if missing:
    print("x64 encoder: emitters never exercised by x64_test.c:")
    for m in missing:
        print("  " + m)
    sys.exit(1)
PY

if cmp -s "$OURS" "$BUILD/x64/expected.bin"; then
  n=$(wc -l < "$OFFS" | tr -d ' ')
  echo "x64 encoder: OK ($n cases, $(filesize "$OURS") bytes match llvm-mc)"
  exit 0
fi

echo "x64 encoder: MISMATCH" >&2

python3 - "$OURS" "$BUILD/x64/expected.bin" "$OFFS" <<'PY'
import sys
ours = open(sys.argv[1], 'rb').read()
exp = open(sys.argv[2], 'rb').read()
cases = []
for line in open(sys.argv[3]):
    off, text = line.rstrip('\n').split('\t', 1)
    cases.append((int(off), text))
n = min(len(ours), len(exp))
div = next((i for i in range(n) if ours[i] != exp[i]), n)
if div == n and len(ours) == len(exp):
    sys.exit(0)
# The case owning the divergent byte; beyond it the streams may be
# length-shifted, so only the first divergence is authoritative.
lo = 0
for i, (off, text) in enumerate(cases):
    end = cases[i + 1][0] if i + 1 < len(cases) else len(ours)
    if off <= div < end:
        lo = i
        break
off, text = cases[lo]
end = cases[lo + 1][0] if lo + 1 < len(cases) else len(ours)
print(f"  first divergence at byte {div} in case {lo}: {text}")
print(f"    ours:     {ours[off:end].hex()}")
print(f"    expected: {exp[off:min(end, len(exp))].hex()} (same-offset slice)")
print(f"  lengths: ours={len(ours)} expected={len(exp)}")
PY
exit 1
