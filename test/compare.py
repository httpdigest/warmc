"""Compare warmc output against wasm-interp's --run-all-exports transcript.

Both sides are normalised to unsigned hex per value so that the two
printers' formatting differences do not matter.

Importable as well as runnable: the case harness compares every case module
at several optimisation levels, and starting a Python for each of those
comparisons cost more than the comparing did.
"""
import os
import re
import subprocess
import sys

import hostbuild

MASK = {"i32": (1 << 32) - 1, "f32": (1 << 32) - 1,
        "i64": (1 << 64) - 1, "f64": (1 << 64) - 1}

V128_RE = re.compile(r"v128 (i8x16|i16x8|i32x4|i64x2|f32x4|f64x2):((?:\s*0x[0-9a-fA-F]+)+)")
LANE_BITS = {"i8x16": 8, "i16x8": 16, "i32x4": 32,
             "i64x2": 64, "f32x4": 32, "f64x2": 64}

def fold_v128(text):
    def one(m):
        w = LANE_BITS[m.group(1)]
        bits = 0
        for i, lane in enumerate(m.group(2).split()):
            bits |= (int(lane, 16) & ((1 << w) - 1)) << (i * w)
        return "v128:0x%032x" % bits
    return V128_RE.sub(one, text)

# The accepted x64 NaN policy (hostenv exports WARMC_X64_NAN on the native
# Windows row): freshly generated NaNs carry the sign bit on x64 and not on
# AArch64 or in wasm-interp.  Both are spec-conformant arithmetic NaNs, so
# exactly the negated canonical patterns fold to the canonical ones --
# including through i32/i64 reinterpretation, which is how the float cases
# observe exact bits.
X64_NAN = os.environ.get("WARMC_X64_NAN") == "1"
NAN_FOLD = {("i32", 0xffc00000): 0x7fc00000,
            ("f32", 0xffc00000): 0x7fc00000,
            ("i64", 0xfff8000000000000): 0x7ff8000000000000,
            ("f64", 0xfff8000000000000): 0x7ff8000000000000}

def fold_v128_nan(bits):
    """The same fold inside a vector, whose lane type is no longer in hand.

    A folded v128 is one 128-bit integer, so each rule is applied at its own
    lane width.  The two patterns cannot be confused for one another: the
    64-bit one splits into 0xfff80000 and 0, neither of which is the 32-bit
    one.
    """
    for w, pat, canon in ((32, 0xffc00000, 0x7fc00000),
                          (64, 0xfff8000000000000, 0x7ff8000000000000)):
        for i in range(128 // w):
            if (bits >> (i * w)) & ((1 << w) - 1) == pat:
                bits ^= (pat ^ canon) << (i * w)
    return bits

def norm(tok):
    tok = tok.strip().rstrip(",")
    if ":" not in tok:
        return tok
    ty, val = tok.split(":", 1)
    ty = ty.strip()
    val = val.strip()
    if ty in ("i32", "i64"):
        v = int(val, 0) & MASK[ty]
        if X64_NAN:
            v = NAN_FOLD.get((ty, v), v)
        return "%s:%x" % (ty, v)
    if ty in ("f32", "f64"):
        if val.startswith("0x"):
            v = int(val, 16) & MASK[ty]
            if X64_NAN:
                v = NAN_FOLD.get((ty, v), v)
            return "%s:0x%x" % (ty, v)
        return "%s:%s" % (ty, val)
    if ty == "v128" and val.startswith("0x"):
        v = int(val, 16) & ((1 << 128) - 1)
        if X64_NAN:
            v = fold_v128_nan(v)
        return "v128:0x%032x" % v
    return "%s:%s" % (ty, val)

def read_reference(ref_path):
    """The interpreter's transcript as {export: normalised results}."""
    expected = {}
    trapped = {}
    cur = None
    for line in open(ref_path):
        line = fold_v128(line.rstrip("\n"))
        m = re.match(r"^(\S+?)\(\) => (.*)$", line)
        if m:
            cur = m.group(1)
            rest = m.group(2).strip()
            expected[cur] = " ".join(norm(t) for t in rest.split(",")) if rest else ""
            continue
        m = re.match(r"^(\S+?)\(\) => $", line)
        if m:
            expected[m.group(1)] = ""
            continue
        m = re.match(r"^error(?: in|:)?\s*(.*)$", line)
        if m and cur:
            trapped[cur] = m.group(1)
    return expected

def to_decimal(tok):
    """Render an f32/f64 bit pattern the way wasm-interp prints it."""
    if ":" not in tok:
        return tok
    ty, val = tok.split(":", 1)
    if ty not in ("f32", "f64") or not val.startswith("0x"):
        return tok
    bits = int(val, 16)
    import struct as _s
    if ty == "f32":
        f = _s.unpack("<f", _s.pack("<I", bits & 0xFFFFFFFF))[0]
    else:
        f = _s.unpack("<d", _s.pack("<Q", bits))[0]
    if f != f:
        return "%s:nan" % ty
    if f in (float("inf"), float("-inf")):
        return "%s:%s" % (ty, "inf" if f > 0 else "-inf")
    return "%s:%f" % (ty, f)

def reconcile(want, got):
    """wasm-interp prints floats in decimal; we print exact bits.  When the
    reference side is decimal, render ours the same way before comparing."""
    if want == got:
        return True
    ws, gs = want.split(), got.split()
    if len(ws) != len(gs):
        return False
    for w, g in zip(ws, gs):
        if w == g:
            continue
        if to_decimal(g) == w:
            continue
        return False
    return True

CHUNK = 400

def compare(ref_path, binary, label, run=None, timeout=None, prefix=()):
    """Run every export of `binary` and check it against the transcript.

    `prefix` goes in front of the export names -- a binary linked from several
    modules takes the module's name there.

    Returns (comparisons, failures, [messages]).
    """

    if run is None:
        run = os.environ.get("WARMC_RUN", "qemu-aarch64").split()

    if timeout is None:

        timeout = float(os.environ.get("WARMC_RUN_TIMEOUT", "30"))

    def invoke(names):
        """Call these exports in one launch; one output line each, or None.

        The runtime takes several exports separated by `--` and prints a line
        per call, so a module with a thousand exports costs one process
        instead of a thousand.  A call that traps ends the launch, so a short
        answer means the rest were never run and the caller has to fall back.
        """
        argv = []
        for n in names:
            if argv:
                argv.append("--")
            argv.append(n)
        p = subprocess.Popen(run + [binary] + list(prefix) + argv,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            out, err = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            where = hostbuild.stack_of(p.pid, binary + ".sample.txt")
            p.kill()
            p.communicate()
            return None, "TIMEOUT after %gs%s" % (timeout, where)
        lines = out.decode().split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        if len(lines) != len(names):
            return None, err.decode().strip().replace("\n", " ")
        return lines, ""

    expected = read_reference(ref_path)
    fails = 0
    msgs = []

    names = list(expected)
    count = len(names)
    results = {}
    for i in range(0, len(names), CHUNK):
        group = names[i:i + CHUNK]
        lines, err = invoke(group)
        if lines is not None:
            results.update(zip(group, lines))
            continue

        if err.startswith("TIMEOUT"):
            msgs.append("FAIL[%s]: %s -- exports %d..%d never answered"
                        % (label, err, i, i + len(group) - 1))
            return count, count - len(results), msgs

        for n in group:
            one, err1 = invoke([n])
            results[n] = one[0] if one else None
            if one is None:
                msgs.append("FAIL[%s] %s: no result (%s)" % (label, n, err1 or err))

    for name in names:
        line = results.get(name)
        if line is None:
            fails += 1
            continue
        got = " ".join(norm(t) for t in line.strip().split())
        want = expected[name]
        if not reconcile(want, got):
            fails += 1
            if fails <= 12:
                msgs.append("FAIL[%s] %s: want %r got %r"
                            % (label, name, want, got))

    if fails > 12:
        msgs.append("FAIL[%s]: ... and %d more" % (label, fails - 12))
    return count, fails, msgs

if __name__ == "__main__":
    ref_path, binary, label = sys.argv[1], sys.argv[2], sys.argv[3]
    count, fails, msgs = compare(ref_path, binary, label)
    print("\n".join(msgs))
    print("%d %d" % (count, fails))
