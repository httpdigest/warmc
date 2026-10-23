"""Generate differential test modules.

Every exported function takes no arguments so that `wasm-interp
--run-all-exports` and our compiled binary can be compared directly.  Float
results are reinterpreted to integers so the comparison stays bit-exact
instead of going through decimal printing.
"""
import os
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "test/cases"
os.makedirs(OUT, exist_ok=True)

def write(name, body):
    path = os.path.join(OUT, name + ".wat")
    text = "(module\n" + body + ")\n"

    try:
        with open(path) as f:
            if f.read() == text:
                return path
    except OSError:
        pass
    tmp = "%s.%d.tmp" % (path, os.getpid())
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    return path

I32 = [0, 1, 2, 7, 31, 32, 33, 255, 256, 65535, 0x7FFFFFFF, 0x80000000,
       0xFFFFFFFF, 0xFFFFFFFE, 0xDEADBEEF, 0x12345678, 3, 0xFFFFFFF9]
I64 = [0, 1, 63, 64, 65, 0xFF, 0x7FFFFFFFFFFFFFFF, 0x8000000000000000,
       0xFFFFFFFFFFFFFFFF, 0x123456789ABCDEF, 0xFFFFFFFF, 0x100000000, 5]

F32 = [0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.14159265, 1e-30, 1e30,
       float("inf"), float("-inf"), float("nan"), 2147483648.0,
       -2147483648.0, 1e9, -1e9, 0.1]
F64 = [0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.141592653589793, 1e-300, 1e300,
       float("inf"), float("-inf"), float("nan"), 9223372036854775808.0,
       -9223372036854775808.0, 1e18, -1e18, 0.1, 4294967296.0]

def f32hex(v):
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", v))[0]

def f64hex(v):
    return "0x%016x" % struct.unpack("<Q", struct.pack("<d", v))[0]

def f32lit(v):

    b = struct.unpack("<I", struct.pack("<f", v))[0]
    if b == 0x7FC00000:
        return "nan"
    if b == 0xFFC00000:
        return "-nan"
    if v == float("inf"):
        return "inf"
    if v == float("-inf"):
        return "-inf"
    return float(struct.unpack("<f", struct.pack("<I", b))[0]).hex()

def f64lit(v):
    b = struct.unpack("<Q", struct.pack("<d", v))[0]
    if b == 0x7FF8000000000000:
        return "nan"
    if b == 0xFFF8000000000000:
        return "-nan"
    if v == float("inf"):
        return "inf"
    if v == float("-inf"):
        return "-inf"
    return v.hex()

def gen_i32():
    ops_bin = ["add", "sub", "mul", "and", "or", "xor", "shl", "shr_s",
               "shr_u", "rotl", "rotr", "eq", "ne", "lt_s", "lt_u", "gt_s",
               "gt_u", "le_s", "le_u", "ge_s", "ge_u"]
    ops_un = ["clz", "ctz", "popcnt", "eqz", "extend8_s", "extend16_s"]
    out = []
    n = 0
    for op in ops_bin:
        for a in I32:
            for b in I32[:8]:
                out.append(
                    '(func (export "i32_%s_%d") (result i32) '
                    "(i32.%s (i32.const 0x%x) (i32.const 0x%x)))\n" % (op, n, op, a, b))
                n += 1
    for op in ops_un:
        for a in I32:
            out.append('(func (export "i32_%s_%d") (result i32) '
                       "(i32.%s (i32.const 0x%x)))\n" % (op, n, op, a))
            n += 1

    for op in ["div_s", "div_u", "rem_s", "rem_u"]:
        for a in I32:
            for b in I32:
                if b == 0:
                    continue
                if op == "div_s" and a == 0x80000000 and b == 0xFFFFFFFF:
                    continue
                out.append('(func (export "i32_%s_%d") (result i32) '
                           "(i32.%s (i32.const 0x%x) (i32.const 0x%x)))\n" % (op, n, op, a, b))
                n += 1
    return "".join(out)

def gen_i64():
    ops_bin = ["add", "sub", "mul", "and", "or", "xor", "shl", "shr_s",
               "shr_u", "rotl", "rotr"]
    cmps = ["eq", "ne", "lt_s", "lt_u", "gt_s", "gt_u", "le_s", "le_u",
            "ge_s", "ge_u"]
    out = []
    n = 0
    for op in ops_bin:
        for a in I64:
            for b in I64[:7]:
                out.append('(func (export "i64_%s_%d") (result i64) '
                           "(i64.%s (i64.const 0x%x) (i64.const 0x%x)))\n" % (op, n, op, a, b))
                n += 1
    for op in cmps:
        for a in I64:
            for b in I64[:7]:
                out.append('(func (export "i64_%s_%d") (result i32) '
                           "(i64.%s (i64.const 0x%x) (i64.const 0x%x)))\n" % (op, n, op, a, b))
                n += 1
    for op in ["clz", "ctz", "popcnt", "extend8_s", "extend16_s", "extend32_s"]:
        for a in I64:
            out.append('(func (export "i64_%s_%d") (result i64) '
                       "(i64.%s (i64.const 0x%x)))\n" % (op, n, op, a))
            n += 1
    for a in I64:
        out.append('(func (export "i64_eqz_%d") (result i32) '
                   "(i64.eqz (i64.const 0x%x)))\n" % (n, a))
        n += 1
    for op in ["div_s", "div_u", "rem_s", "rem_u"]:
        for a in I64:
            for b in I64:
                if b == 0:
                    continue
                if op == "div_s" and a == 0x8000000000000000 and b == 0xFFFFFFFFFFFFFFFF:
                    continue
                out.append('(func (export "i64_%s_%d") (result i64) '
                           "(i64.%s (i64.const 0x%x) (i64.const 0x%x)))\n" % (op, n, op, a, b))
                n += 1

    for a in I32:
        out.append('(func (export "i64_ext_s_%d") (result i64) '
                   "(i64.extend_i32_s (i32.const 0x%x)))\n" % (n, a)); n += 1
        out.append('(func (export "i64_ext_u_%d") (result i64) '
                   "(i64.extend_i32_u (i32.const 0x%x)))\n" % (n, a)); n += 1
    for a in I64:
        out.append('(func (export "i32_wrap_%d") (result i32) '
                   "(i32.wrap_i64 (i64.const 0x%x)))\n" % (n, a)); n += 1
    return "".join(out)

def gen_float(bits):
    ty = "f32" if bits == 32 else "f64"
    ity = "i32" if bits == 32 else "i64"
    vals = F32 if bits == 32 else F64
    lit = f32lit if bits == 32 else f64lit
    out = []
    n = 0
    for op in ["add", "sub", "mul", "div", "min", "max", "copysign"]:
        for a in vals:
            for b in vals[:9]:
                out.append('(func (export "%s_%s_%d") (result %s) '
                           "(%s.reinterpret_%s (%s.%s (%s.const %s) (%s.const %s))))\n"
                           % (ty, op, n, ity, ity, ty, ty, op, ty, lit(a), ty, lit(b)))
                n += 1
    for op in ["abs", "neg", "sqrt", "ceil", "floor", "trunc", "nearest"]:
        for a in vals:
            out.append('(func (export "%s_%s_%d") (result %s) '
                       "(%s.reinterpret_%s (%s.%s (%s.const %s))))\n"
                       % (ty, op, n, ity, ity, ty, ty, op, ty, lit(a)))
            n += 1
    for op in ["eq", "ne", "lt", "gt", "le", "ge"]:
        for a in vals:
            for b in vals[:9]:
                out.append('(func (export "%s_%s_%d") (result i32) '
                           "(%s.%s (%s.const %s) (%s.const %s)))\n"
                           % (ty, op, n, ty, op, ty, lit(a), ty, lit(b)))
                n += 1
    return "".join(out)

def gen_conv():
    out = []
    n = 0

    for src, vals, lit in (("f32", F32, f32lit), ("f64", F64, f64lit)):
        for dst in ("i32", "i64"):
            for sign in ("s", "u"):
                for a in vals:
                    out.append('(func (export "sat_%d") (result %s) '
                               "(%s.trunc_sat_%s_%s (%s.const %s)))\n"
                               % (n, dst, dst, src, sign, src, lit(a)))
                    n += 1

    for a in I32:
        for dst in ("f32", "f64"):
            for sign in ("s", "u"):
                ity = "i64" if dst == "f64" else "i32"
                out.append('(func (export "conv_%d") (result %s) '
                           "(%s.reinterpret_%s (%s.convert_i32_%s (i32.const 0x%x))))\n"
                           % (n, "i32" if dst == "f32" else "i64",
                              "i32" if dst == "f32" else "i64", dst, dst, sign, a))
                n += 1
                del ity
    for a in I64:
        for dst in ("f32", "f64"):
            for sign in ("s", "u"):
                out.append('(func (export "conv_%d") (result %s) '
                           "(%s.reinterpret_%s (%s.convert_i64_%s (i64.const 0x%x))))\n"
                           % (n, "i32" if dst == "f32" else "i64",
                              "i32" if dst == "f32" else "i64", dst, dst, sign, a))
                n += 1
    for a in F32:
        out.append('(func (export "promote_%d") (result i64) '
                   "(i64.reinterpret_f64 (f64.promote_f32 (f32.const %s))))\n" % (n, f32lit(a)))
        n += 1
    for a in F64:
        out.append('(func (export "demote_%d") (result i32) '
                   "(i32.reinterpret_f32 (f32.demote_f64 (f64.const %s))))\n" % (n, f64lit(a)))
        n += 1

    in_range = [0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.14159, 1e9, -1e9]
    for src, lit in (("f32", f32lit), ("f64", f64lit)):
        for a in in_range:
            out.append('(func (export "trunc_%d") (result i32) '
                       "(i32.trunc_%s_s (%s.const %s)))\n" % (n, src, src, lit(a)))
            n += 1
            out.append('(func (export "trunc_%d") (result i64) '
                       "(i64.trunc_%s_s (%s.const %s)))\n" % (n, src, src, lit(a)))
            n += 1
            if a >= 0:
                out.append('(func (export "trunc_%d") (result i32) '
                           "(i32.trunc_%s_u (%s.const %s)))\n" % (n, src, src, lit(a)))
                n += 1
    return "".join(out)

def gen_bits():
    """Masking against what a value's bits can be.

    The optimiser removes `x & c` when no bit of x can fall outside c.  Getting
    that wrong is invisible to every other case here, because it needs an x
    whose *magnitude* is bounded more tightly than its *bits* are -- a
    remainder, a count of leading zeros, a division, the arms of a select.
    Reading x out of linear memory keeps it opaque to constant folding, so the
    fold is the thing under test rather than the folder.
    """
    out = ['(memory 1)\n',
           '(data (i32.const 0) "\\03\\00\\00\\00\\01\\00\\00\\00'
           '\\06\\00\\00\\00\\01\\00\\00\\00")\n']
    n = 0

    cases = [
        ("(i32.rem_u (i32.load (i32.const 0)) (i32.const 5))", 4),
        ("(i32.rem_u (i32.load (i32.const 0)) (i32.const 7))", 4),
        ("(i32.clz (i32.load (i32.const 4)))", 32),
        ("(i32.ctz (i32.load (i32.const 4)))", 32),
        ("(i32.popcnt (i32.load (i32.const 0)))", 32),
        ("(i32.div_u (i32.load (i32.const 8)) (i32.const 3))", 0x55555555),
        ("(select (i32.const 3) (i32.const 4) (i32.load (i32.const 12)))", 4),
        ("(i32.or (i32.load (i32.const 0)) (i32.const 8))", 12),
        ("(i32.shr_u (i32.load (i32.const 0)) (i32.const 1))", 2),
    ]
    for expr, mask in cases:
        out.append('(func (export "mask_%d") (result i32) '
                   "(i32.and %s (i32.const 0x%x)))\n" % (n, expr, mask))
        n += 1

    keep = [
        ("(i32.lt_u (i32.load (i32.const 0)) (i32.const 9))", 1),
        ("(i32.shl (i32.lt_u (i32.load (i32.const 0)) (i32.const 9)) (i32.const 2))", 4),
        ("(i32.load8_u (i32.const 0))", 0xff),
        ("(i32.and (i32.load (i32.const 0)) (i32.const 7))", 7),
    ]
    for expr, mask in keep:
        out.append('(func (export "mask_%d") (result i32) '
                   "(i32.and %s (i32.const 0x%x)))\n" % (n, expr, mask))
        n += 1
    return "".join(out)

def gen_vector():
    """Counted loops for the vectoriser, at every trip count near a boundary.

    The vector loop only ever runs whole iterations and leaves the rest to the
    scalar one, so what has to be exercised is every remainder: a wrong
    trip-count formula shows up at exactly one `n` and nowhere else.  Two of
    the kernels must *not* be vectorised -- a dependence at distance one in
    each direction -- and they are here so that a pass which simply gives up
    cannot pass by doing nothing, and one which does not give up cannot pass
    at all.

    Layout: A at 0, B at 8192, C at 16384, which holds 1024 i32 or 512 i64
    with room to spare at the largest `n` below.
    """
    A, Bs, C = 0, 8192, 16384

    out = ["(memory 2)\n(type $a3 (func (param i32 i32 i32)))\n"
           "(table 2 funcref)\n(elem (i32.const 0) $kalias $kmivp)\n"]

    out.append("""(func $fill (param $n i32) (local $i i32) (local $x i32)
  (local.set $x (i32.const 12345))
  (block $d (loop $L
    (br_if $d (i32.gt_u (local.get $i) (i32.add (local.get $n) (i32.const 8))))
    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 13))))
    (local.set $x (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 17))))
    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 5))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) (local.get $x))
    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 13))))
    (local.set $x (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 17))))
    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 5))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) (local.get $x))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) (i32.const 0))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) (i32.const 0))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) (i32.const 0))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $L))))
""" % (A, Bs, C, C + 4096, C + 8192))

    out.append("""(func $fillf (param $n i32) (local $i i32)
  (block $d (loop $L
    (br_if $d (i32.gt_u (local.get $i) (i32.add (local.get $n) (i32.const 8))))
    (f64.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (f64.mul (f64.convert_i32_s (i32.sub (local.get $i) (i32.const 7)))
               (f64.const 0.5)))
    (f64.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (f64.add (f64.convert_i32_s (local.get $i)) (f64.const 1.25)))
    (f64.store offset=%d (i32.shl (local.get $i) (i32.const 3)) (f64.const 0))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $L))))
""" % (A, Bs, C))

    out.append("""(func $chk (param $n i32) (param $base i32) (result i32)
  (local $i i32) (local $h i32)
  (local.set $h (i32.const 0x811c9dc5))
  (block $d (loop $L
    (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
    (local.set $h (i32.mul (i32.xor (local.get $h)
      (i32.load (i32.add (local.get $base) (i32.shl (local.get $i) (i32.const 2)))))
      (i32.const 16777619)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $L)))
  (local.get $h))
(func $chkb (param $n i32) (param $base i32) (result i32)
  (local $i i32) (local $h i32)
  (local.set $h (i32.const 0x811c9dc5))
  (block $d (loop $L
    (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
    (local.set $h (i32.mul (i32.xor (local.get $h)
      (i32.load8_u (i32.add (local.get $base) (local.get $i))))
      (i32.const 16777619)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $L)))
  (local.get $h))
(func $chk8 (param $n i32) (param $base i32) (result i64)
  (local $i i32) (local $h i64)
  (local.set $h (i64.const 0xcbf29ce484222325))
  (block $d (loop $L
    (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
    (local.set $h (i64.mul (i64.xor (local.get $h)
      (i64.load (i32.add (local.get $base) (i32.shl (local.get $i) (i32.const 3)))))
      (i64.const 0x100000001b3)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $L)))
  (local.get $h))
""")

    kern = {}
    kern["add"] = """(func $kadd (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs)

    kern["mix"] = """(func $kmix (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.and
        (i32.add
          (i32.mul (i32.xor (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                            (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2))))
                   (i32.const 3))
          (i32.shr_s (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                     (i32.const 3)))
        (i32.xor (i32.shl (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                          (i32.const 2))
                 (i32.const -1))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs, A, Bs)
    kern["inplace"] = """(func $kinplace (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                        (i32.const 5)) (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, A)

    kern["dep"] = """(func $kdep (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
               (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A + 4, A)

    kern["anti"] = """(func $kanti (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
               (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, A + 4)
    kern["sum"] = """(func $ksum (param $n i32) (result i32) (local $i i32) (local $s i32)
  (if (i32.eqz (local.get $n)) (then (return (i32.const 0))))
  (loop $L
    (local.set $s (i32.add (local.get $s)
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))
  (local.get $s))
""" % (A,)
    kern["xor"] = """(func $kxor (param $n i32) (result i32) (local $i i32) (local $s i32)
  (local.set $s (i32.const 0x12345678))
  (if (i32.eqz (local.get $n)) (then (return (local.get $s))))
  (loop $L
    (local.set $s (i32.xor (local.get $s)
      (i32.and (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2))))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))
  (local.get $s))
""" % (A, Bs)
    kern["and"] = """(func $kand (param $n i32) (result i32) (local $i i32) (local $s i32)
  (local.set $s (i32.const -1))
  (if (i32.eqz (local.get $n)) (then (return (local.get $s))))
  (loop $L
    (local.set $s (i32.and (local.get $s)
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))
  (local.get $s))
""" % (A,)
    kern["add64"] = """(func $kadd64 (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i64.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (i64.add (i64.xor (i64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))
                        (i64.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
               (i64.and (i64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))
                        (i64.load offset=%d (i32.shl (local.get $i) (i32.const 3))))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs, A, Bs)
    kern["sum64"] = """(func $ksum64 (param $n i32) (result i64) (local $i i32) (local $s i64)
  (if (i32.eqz (local.get $n)) (then (return (i64.const 0))))
  (loop $L
    (local.set $s (i64.add (local.get $s)
      (i64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))
  (local.get $s))
""" % (A,)
    kern["fmul"] = """(func $kfmul (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (f64.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (f64.sub (f64.mul (f64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))
                        (f64.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
               (f64.div (f64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))
                        (f64.const 4))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs, A)
    kern["fsqrt"] = """(func $kfsqrt (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (f64.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (f64.add (f64.sqrt (f64.abs (f64.load offset=%d (i32.shl (local.get $i) (i32.const 3)))))
               (f64.neg (f64.load offset=%d (i32.shl (local.get $i) (i32.const 3))))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs)

    kern["step"] = """(func $kstep (param $n i32) (local $i i32) (local $m i32)
  (local.set $m (i32.mul (local.get $n) (i32.const 2)))
  (if (i32.eqz (local.get $m)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                        (i32.const 3))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                        (i32.const 3))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (local.set $i (i32.add (local.get $i) (i32.const 2)))
    (br_if $L (i32.ne (local.get $i) (local.get $m)))))
""" % (C, A, Bs, C + 4, A + 4, Bs + 4)

    def cond(name, body, res=""):
        return ("(func $k%s (param $n i32)%s (local $i i32)%s\n"
                "  (if (i32.eqz (local.get $n)) (then (return%s)))\n"
                "  (loop $L\n%s"
                "    (local.set $i (i32.add (local.get $i) (i32.const 1)))\n"
                "    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))%s)\n"
                % (name, " (result i32)" if res else "", " (local $s i32)" if res else "",
                   " (i32.const 0)" if res else "", body,
                   "\n  (local.get $s)" if res else ""))
    ix = "(i32.shl (local.get $i) (i32.const 2))"
    kern["ifsel"] = cond("ifsel",
        "    (i32.store offset=%d %s\n"
        "      (if (result i32) (i32.gt_s (i32.load offset=%d %s) (i32.const 0))\n"
        "        (then (i32.load offset=%d %s))\n"
        "        (else (i32.load offset=%d %s))))\n"
        % (C, ix, A, ix, A, ix, Bs, ix))

    kern["ifsel2"] = cond("ifsel2",
        "    (i32.store offset=%d %s\n"
        "      (if (result i32) (i32.lt_s (i32.load offset=%d %s) (i32.const 0))\n"
        "        (then (i32.load offset=%d %s))\n"
        "        (else (i32.load offset=%d %s))))\n"
        % (C, ix, A, ix, Bs, ix, A, ix))

    kern["ifne"] = cond("ifne",
        "    (i32.store offset=%d %s\n"
        "      (if (result i32) (i32.ne (i32.load offset=%d %s) (i32.const 0))\n"
        "        (then (i32.load offset=%d %s))\n"
        "        (else (i32.const 12345)))) \n"
        % (C, ix, A, ix, Bs, ix))

    kern["ifstore"] = cond("ifstore",
        "    (if (i32.gt_s (i32.load offset=%d %s) (i32.const 0))\n"
        "      (then (i32.store offset=%d %s\n"
        "              (i32.load offset=%d %s))))\n"
        % (A, ix, C, ix, A, ix))
    kern["ifstore2"] = cond("ifstore2",
        "    (if (i32.lt_s (i32.load offset=%d %s) (i32.const 0))\n"
        "      (then (i32.store offset=%d %s\n"
        "              (i32.mul (i32.load offset=%d %s) (i32.const 3)))))\n"
        % (A, ix, C, ix, Bs, ix))

    kern["alt"] = """(func $kalt (param $n i32) (local $i i32) (local $a i32) (local $b i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $a (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
    (local.set $b (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (i32.sub (local.get $a) (local.get $b)))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (i32.add (local.get $a) (local.get $b)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, A + 4, C, C + 4)
    kern["altl"] = """(func $kaltl (param $n i32) (local $i i32) (local $a i64) (local $b i64)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $a (i64.load offset=%d (i32.shl (local.get $i) (i32.const 4))))
    (local.set $b (i64.load offset=%d (i32.shl (local.get $i) (i32.const 4))))
    (i64.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i64.xor (local.get $a) (local.get $b)))
    (i64.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i64.and (local.get $a) (local.get $b)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, A + 8, C, C + 8)
    kern["alt3"] = """(func $kalt3 (param $n i32) (local $i i32) (local $a i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $a (i32.load offset=%d (i32.mul (local.get $i) (i32.const 12))))
    (i32.store offset=%d (i32.mul (local.get $i) (i32.const 12))
      (i32.add (local.get $a) (i32.const 1)))
    (i32.store offset=%d (i32.mul (local.get $i) (i32.const 12))
      (i32.sub (local.get $a) (i32.const 1)))
    (i32.store offset=%d (i32.mul (local.get $i) (i32.const 12))
      (i32.mul (local.get $a) (i32.const 3)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, C, C + 4, C + 8)

    kern["alt4"] = """(func $kalt4 (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i32.add (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i32.sub (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i32.xor (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 4))
      (i32.and (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 4)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs, C + 4, A + 4, Bs + 4, C + 8, A + 8, Bs + 8,
       C + 12, A + 12, Bs + 12)

    kern["ilvd"] = """(func $kilvd (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, C + 4096, A + 4)
    kern["ilvi"] = """(func $kilvi (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
               (i32.const 3)))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 3))
      (i32.sub (i32.const 0)
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, C + 4, Bs)
    kern["ilvhalf"] = """(func $kilvhalf (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)

    kern["down"] = """(func $kdown (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $i (local.get $n))
  (loop $L
    (local.set $i (i32.sub (local.get $i) (i32.const 1)))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                        (i32.const 3))
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (br_if $L (local.get $i))))
""" % (C, A, Bs)
    kern["downr"] = """(func $kdownr (param $n i32) (result i32)
  (local $i i32) (local $s i32)
  (if (i32.eqz (local.get $n)) (then (return (i32.const 0))))
  (local.set $i (local.get $n))
  (loop $L
    (local.set $i (i32.sub (local.get $i) (i32.const 1)))
    (local.set $s (i32.add (local.get $s)
      (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))))
    (br_if $L (local.get $i)))
  (local.get $s))
""" % (A,)
    kern["downb"] = """(func $kdownb (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $i (local.get $n))
  (loop $L
    (local.set $i (i32.sub (local.get $i) (i32.const 1)))
    (i32.store8 offset=%d (local.get $i)
      (i32.xor (i32.load8_u offset=%d (local.get $i))
               (i32.load8_u offset=%d (local.get $i))))
    (br_if $L (local.get $i))))
""" % (C, A, Bs)
    kern["downin"] = """(func $kdownin (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $i (local.get $n))
  (loop $L
    (local.set $i (i32.sub (local.get $i) (i32.const 1)))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.add (i32.mul (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))
                        (i32.const 5)) (i32.const 1)))
    (br_if $L (local.get $i))))
""" % (A, A)
    kern["revmix"] = """(func $krevmix (param $n i32) (local $i i32) (local $j i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $j (i32.sub (local.get $n) (i32.const 1)))
  (loop $L
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (i32.load offset=%d (i32.shl (local.get $j) (i32.const 2))))
    (local.set $j (i32.sub (local.get $j) (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)

    def unrolled(name, u, step, body, red=False):
        res = " (result i32)" if red else ""
        loc = " (local $s i32)" if red else ""
        zero = " (i32.const 0)" if red else ""
        tail = "\n  (local.get $s)" if red else ""
        return ("(func $k%s (param $n i32)%s (local $i i32)%s\n"
                "  (if (i32.eqz (local.get $n)) (then (return%s)))\n"
                "  (loop $L\n%s"
                "    (local.set $i (i32.add (local.get $i) (i32.const %d)))\n"
                "    (br_if $L (i32.lt_u (local.get $i) (local.get $n))))%s)\n"
                % (name, res, loc, zero,
                   "".join(body(j) for j in range(u)), step, tail))

    kern["u8"] = unrolled("u8", 8, 32, lambda j:
        "    (i32.store offset=%d (local.get $i)\n"
        "      (i32.add (i32.mul (i32.load offset=%d (local.get $i)) (i32.const 3))\n"
        "               (i32.load offset=%d (local.get $i))))\n"
        % (C + 4 * j, A + 4 * j, Bs + 4 * j))
    kern["u8r"] = unrolled("u8r", 8, 32, lambda j:
        "    (local.set $s (i32.add (local.get $s)\n"
        "      (i32.xor (i32.load offset=%d (local.get $i))\n"
        "               (i32.load offset=%d (local.get $i)))))\n"
        % (A + 4 * j, Bs + 4 * j), red=True)

    kern["u8dep"] = unrolled("u8dep", 8, 32, lambda j:
        "    (i32.store offset=%d (local.get $i)\n"
        "      (i32.add (i32.load offset=%d (local.get $i)) (i32.const 1)))\n"
        % (A + 4 * j, A + 16 + 4 * j))
    kern["u8dep2"] = unrolled("u8dep2", 8, 32, lambda j:
        "    (i32.store offset=%d (local.get $i)\n"
        "      (i32.add (i32.load offset=%d (local.get $i)) (i32.const 1)))\n"
        % (A + 16 + 4 * j, A + 4 * j))
    kern["b32"] = unrolled("b32", 32, 32, lambda j:
        "    (i32.store8 offset=%d (local.get $i)\n"
        "      (i32.xor (i32.load8_u offset=%d (local.get $i))\n"
        "               (i32.load8_u offset=%d (local.get $i))))\n"
        % (C + j, A + j, Bs + j))
    kern["u4l"] = unrolled("u4l", 4, 32, lambda j:
        "    (i64.store offset=%d (local.get $i)\n"
        "      (i64.add (i64.load offset=%d (local.get $i))\n"
        "               (i64.load offset=%d (local.get $i))))\n"
        % (C + 8 * j, A + 8 * j, Bs + 8 * j))

    kern["miv"] = """(func $kmiv (param $n i32)
  (local $i i32) (local $pa i32) (local $pb i32) (local $pc i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $pa (i32.const %d))
  (local.set $pb (i32.const %d))
  (local.set $pc (i32.const %d))
  (loop $L
    (i32.store (local.get $pc)
      (i32.add (i32.mul (i32.load (local.get $pa)) (i32.const 3))
               (i32.load (local.get $pb))))
    (local.set $pa (i32.add (local.get $pa) (i32.const 4)))
    (local.set $pb (i32.add (local.get $pb) (i32.const 4)))
    (local.set $pc (i32.add (local.get $pc) (i32.const 4)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, Bs, C)
    kern["mivs"] = """(func $kmivs (param $n i32)
  (local $i i32) (local $pa i32) (local $pc i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $pa (i32.const %d))
  (local.set $pc (i32.const %d))
  (loop $L
    (i32.store (local.get $pc) (i32.load (local.get $pa)))
    (local.set $pa (i32.add (local.get $pa) (i32.const 8)))
    (local.set $pc (i32.add (local.get $pc) (i32.const 4)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, C)

    kern["mivs2"] = """(func $kmivs2 (param $n i32)
  (local $i i32) (local $pa i32) (local $pc i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $pa (i32.const %d))
  (local.set $pc (i32.const %d))
  (loop $L
    (i32.store (local.get $pc) (i32.load (local.get $pa)))
    (local.set $pa (i32.add (local.get $pa) (i32.const 4)))
    (local.set $pc (i32.add (local.get $pc) (i32.const 8)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, C)

    kern["miv2"] = """(func $kmiv2 (param $n i32)
  (local $i i32) (local $pa i32) (local $pb i32) (local $pc i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $pa (i32.const %d))
  (local.set $pb (i32.const %d))
  (local.set $pc (i32.const %d))
  (loop $L
    (i32.store (local.get $pc)
      (i32.sub (i32.load (local.get $pa)) (i32.load (local.get $pb))))
    (i32.store offset=4 (local.get $pc)
      (i32.sub (i32.load offset=4 (local.get $pa))
               (i32.load offset=4 (local.get $pb))))
    (local.set $pa (i32.add (local.get $pa) (i32.const 8)))
    (local.set $pb (i32.add (local.get $pb) (i32.const 8)))
    (local.set $pc (i32.add (local.get $pc) (i32.const 8)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, Bs, C)

    kern["miv3"] = """(func $kmiv3 (param $n i32)
  (local $i i32) (local $pa i32) (local $pb i32) (local $pc i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (local.set $pa (i32.const %d))
  (local.set $pb (i32.const %d))
  (local.set $pc (i32.const %d))
  (loop $L
    (i32.store (local.get $pc)
      (i32.sub (i32.load (local.get $pa)) (i32.load (local.get $pb))))
    (i32.store offset=4 (local.get $pc)
      (i32.sub (i32.load offset=4 (local.get $pb))
               (i32.load offset=4 (local.get $pa))))
    (local.set $pa (i32.add (local.get $pa) (i32.const 8)))
    (local.set $pb (i32.add (local.get $pb) (i32.const 8)))
    (local.set $pc (i32.add (local.get $pc) (i32.const 8)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (A, Bs, C)

    kern["mivp"] = """(func $kmivp (param $d i32) (param $s i32) (param $n i32)
  (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store (local.get $d)
      (i32.add (i32.mul (i32.load (local.get $s)) (i32.const 3)) (i32.const 1)))
    (local.set $s (i32.add (local.get $s) (i32.const 4)))
    (local.set $d (i32.add (local.get $d) (i32.const 4)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
"""

    kern["alias"] = """(func $kalias (param $d i32) (param $s i32) (param $n i32)
  (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store (i32.add (local.get $d) (i32.shl (local.get $i) (i32.const 2)))
      (i32.add (i32.mul (i32.load (i32.add (local.get $s)
                                           (i32.shl (local.get $i) (i32.const 2))))
                        (i32.const 3))
               (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
"""

    def sel(rel, arms, cmpargs):
        return "(select %s (i32.%s %s))" % (arms, rel, cmpargs)
    ld = lambda off: ("(i32.load offset=%d (i32.shl (local.get $i) (i32.const 2)))" % off)
    for nm, rel, swap in (("smax", "gt_s", 0), ("smin", "lt_s", 0),
                          ("umax", "gt_u", 0), ("smax2", "ge_s", 0),
                          ("smin2", "gt_s", 1)):
        arms = "(local.get $x) (local.get $y)"
        cargs = "(local.get $y) (local.get $x)" if swap else "(local.get $x) (local.get $y)"
        kern[nm] = """(func $k%s (param $n i32) (local $i i32) (local $x i32) (local $y i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $x %s)
    (local.set $y %s)
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2)) %s)
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (nm, ld(A), ld(Bs), C, sel(rel, arms, cargs))
    kern["clamp"] = """(func $kclamp (param $n i32) (local $i i32) (local $x i32) (local $v i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $x %s)
    (local.set $v (select (local.get $x) (i32.const 0)
                          (i32.gt_s (local.get $x) (i32.const 0))))
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (select (local.get $v) (i32.const 255)
              (i32.lt_s (local.get $v) (i32.const 255))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (ld(A), C)
    kern["selx"] = """(func $kselx (param $n i32) (local $i i32) (local $x i32) (local $y i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (local.set $x %s)
    (local.set $y %s)
    (i32.store offset=%d (i32.shl (local.get $i) (i32.const 2))
      (select (local.get $y) (local.get $x)
              (i32.gt_s (local.get $x) (i32.const 0))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (ld(A), ld(Bs), C)

    kern["b_xor"] = """(func $kb_xor (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.xor (i32.load8_u offset=%d (local.get $i))
               (i32.load8_u offset=%d (local.get $i))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs)
    kern["b_mul"] = """(func $kb_mul (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.add (i32.mul (i32.load8_s offset=%d (local.get $i)) (i32.const 31))
               (i32.const 7)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)
    kern["b_shl"] = """(func $kb_shl (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.shl (i32.load8_u offset=%d (local.get $i)) (i32.const 3)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)

    kern["b_shlwide"] = """(func $kb_shlwide (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.shl (i32.load8_u offset=%d (local.get $i)) (i32.const 9)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)
    kern["b_shr"] = """(func $kb_shr (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.shr_u (i32.add (i32.load8_u offset=%d (local.get $i))
                          (i32.load8_u offset=%d (local.get $i)))
                 (i32.const 1)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs)
    kern["h_add"] = """(func $kh_add (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store16 offset=%d (i32.shl (local.get $i) (i32.const 1))
      (i32.mul (i32.add (i32.load16_u offset=%d (i32.shl (local.get $i) (i32.const 1)))
                        (i32.load16_s offset=%d (i32.shl (local.get $i) (i32.const 1))))
               (i32.const 5)))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs)

    kern["b_mix"] = """(func $kb_mix (param $n i32) (local $i i32)
  (if (i32.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 offset=%d (local.get $i)
      (i32.load16_u offset=%d (i32.shl (local.get $i) (i32.const 1))))
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
""" % (C, A)

    kern["acc"] = """(func $kacc (param $n i32) (result i32)
  (local $i i32) (local $m i32) (local $s i32)
  (local.set $m (i32.mul (local.get $n) (i32.const 2)))
  (if (i32.eqz (local.get $m)) (then (return (i32.const 0))))
  (loop $L
    (local.set $s (i32.add (local.get $s)
      (i32.xor (local.get $s)
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2))))))
    (local.set $s (i32.add (local.get $s)
      (i32.xor (local.get $s)
               (i32.load offset=%d (i32.shl (local.get $i) (i32.const 2))))))
    (local.set $i (i32.add (local.get $i) (i32.const 2)))
    (br_if $L (i32.ne (local.get $i) (local.get $m))))
  (local.get $s))
""" % (A, A + 4)
    for k in ("add", "mix", "inplace", "dep", "anti", "sum", "xor", "and",
              "add64", "sum64", "fmul", "fsqrt", "step", "acc",
              "b_xor", "b_mul", "b_shl", "b_shlwide", "b_shr", "h_add", "b_mix",
              "smax", "smin", "umax", "smax2", "smin2", "clamp", "selx",
              "alias", "miv", "mivs", "mivs2", "miv2", "miv3", "mivp",
              "u8", "u8r", "u8dep", "u8dep2", "b32", "u4l",
              "down", "downr", "downb", "downin", "revmix",
              "ilvd", "ilvi", "ilvhalf", "alt", "altl", "alt3", "alt4",
              "ifsel", "ifsel2", "ifne", "ifstore", "ifstore2"):
        out.append(kern[k])

    NS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18,
          31, 32, 33, 63, 64, 65, 127, 128, 129, 200]
    i32_out = {"add": C, "mix": C, "inplace": A, "dep": A, "anti": A}
    for k, base in i32_out.items():
        cnt = "(i32.add (local.get $n) (i32.const 1))" if k == "dep" else "(local.get $n)"
        for n in NS:
            out.append('(func (export "v_%s_%d") (result i32) (local $n i32)\n'
                       "  (local.set $n (i32.const %d))\n"
                       "  (call $fill (local.get $n)) (call $k%s (local.get $n))\n"
                       "  (call $chk %s (i32.const %d)))\n"
                       % (k, n, n, k, cnt, base))
    for k in ("sum", "xor", "and"):
        for n in NS:
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d)))\n"
                       % (k, n, n, k, n))
    for n in NS:
        out.append('(func (export "v_add64_%d") (result i64)\n'
                   "  (call $fill (i32.const %d)) (call $kadd64 (i32.const %d))\n"
                   "  (call $chk8 (i32.const %d) (i32.const %d)))\n"
                   % (n, 2 * n + 8, n, n, C))
        out.append('(func (export "v_sum64_%d") (result i64)\n'
                   "  (call $fill (i32.const %d)) (call $ksum64 (i32.const %d)))\n"
                   % (n, 2 * n + 8, n))
        for k in ("fmul", "fsqrt"):
            out.append('(func (export "v_%s_%d") (result i64)\n'
                       "  (call $fillf (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk8 (i32.const %d) (i32.const %d)))\n"
                       % (k, n, n, k, n, n, C))
        out.append('(func (export "v_step_%d") (result i32)\n'
                   "  (call $fill (i32.const %d)) (call $kstep (i32.const %d))\n"
                   "  (call $chk (i32.const %d) (i32.const %d)))\n"
                   % (n, 2 * n + 8, n, 2 * n, C))
        out.append('(func (export "v_acc_%d") (result i32)\n'
                   "  (call $fill (i32.const %d)) (call $kacc (i32.const %d)))\n"
                   % (n, 2 * n + 8, n))
        for k, wide, nw in (("u8", 32, 8), ("u8dep", 32, 8), ("u8dep2", 32, 8),
                            ("b32", 32, 8), ("u4l", 32, 8)):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk (i32.const %d) (i32.const %d)))\n"
                       % (k, n, 8 * n + 64, k, n * wide,
                          n * nw + 8, C if k not in ("u8dep", "u8dep2") else A))
        out.append('(func (export "v_u8r_%d") (result i32)\n'
                   "  (call $fill (i32.const %d)) (call $ku8r (i32.const %d)))\n"
                   % (n, 8 * n + 64, n * 32))

        for k, base, w in (("ilvd", C, 1), ("ilvi", C, 2), ("ilvhalf", C, 1),
                           ("alt", C, 2), ("altl", C, 4), ("alt3", C, 3), ("alt4", C, 4),
                           ("ifsel", C, 1), ("ifsel2", C, 1), ("ifne", C, 1),
                           ("ifstore", C, 1), ("ifstore2", C, 1)):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk (i32.const %d) (i32.const %d)))\n"
                       % (k, n, (w if w > 2 else 2) * n + 40, k,
                          n, n * w + 8, base))
        for k, base in (("down", C), ("downb", C), ("downin", A), ("revmix", C)):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk (i32.const %d) (i32.const %d)))\n"
                       % (k, n, n + 32, k, n, n + 8, base))
        out.append('(func (export "v_downr_%d") (result i32)\n'
                   "  (call $fill (i32.const %d)) (call $kdownr (i32.const %d)))\n"
                   % (n, n + 32, n))
        for k in ("miv", "mivs", "mivs2", "miv2", "miv3"):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk (i32.const %d) (i32.const %d)))\n"
                       % (k, n, 4 * n + 32, k, n, 2 * n, C))
        for tag, doff in (("dis", 4096), ("f1", 4), ("f2", 8), ("f3", 12),
                          ("f4", 16), ("f15", 60), ("f16", 64), ("f17", 68),
                          ("same", 0), ("adj", 4 * n), ("ov1", max(0, 4 * n - 4)),
                          ("ovh", max(0, 2 * n))):
            out.append('(func (export "v_al%s_%d") (result i32)\n'
                       "  (call $fill (i32.const 1600))\n"
                       "  (i32.const %d) (i32.const 0) (i32.const %d) (i32.const 0)\n"
                       "  (call_indirect (type $a3))\n"
                       "  (call $chk (i32.const 1200) (i32.const 0)))\n"
                       % (tag, n, doff, n))
            out.append('(func (export "v_mp%s_%d") (result i32)\n'
                       "  (call $fill (i32.const 1600))\n"
                       "  (i32.const %d) (i32.const 0) (i32.const %d) (i32.const 1)\n"
                       "  (call_indirect (type $a3))\n"
                       "  (call $chk (i32.const 1200) (i32.const 0)))\n"
                       % (tag, n, doff, n))
            out.append('(func (export "v_alr%s_%d") (result i32)\n'
                       "  (call $fill (i32.const 1600))\n"
                       "  (i32.const 0) (i32.const %d) (i32.const %d) (i32.const 0)\n"
                       "  (call_indirect (type $a3))\n"
                       "  (call $chk (i32.const 1200) (i32.const 0)))\n"
                       % (tag, n, doff, n))
        for k in ("smax", "smin", "umax", "smax2", "smin2", "clamp", "selx"):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chk (i32.const %d) (i32.const %d)))\n"
                       % (k, n, n, k, n, n, C))
        for k, bytes_ in (("b_xor", 1), ("b_mul", 1), ("b_shl", 1),
                          ("b_shlwide", 1), ("b_shr", 1), ("h_add", 2),
                          ("b_mix", 1)):
            out.append('(func (export "v_%s_%d") (result i32)\n'
                       "  (call $fill (i32.const %d)) (call $k%s (i32.const %d))\n"
                       "  (call $chkb (i32.const %d) (i32.const %d)))\n"
                       % (k, n, n + 64, k, n, n * bytes_, C))
    return "".join(out)

def gen_fuse():
    """Two instruction-selection fusions competing for one logical operation.

    `y & ~(x << 2)` can be a `bic` whose second source is the shift, or an
    `and` with a shifted operand -- but not both, and the complement pass runs
    first.  When the shift pass then reached into the already-claimed xor it
    also *reordered* its operands, and the `bic` went on reading position zero
    and found the all-ones constant instead of the shift.  The answer was
    wrong rather than slow.

    It only opens when the `-1` is not foldable into an immediate everywhere,
    which needs a second use that wants a register -- hence the multiply.  The
    vectoriser reaches the same state by splatting the constant into a lane,
    which is how this was found; this module reproduces it without one.
    """
    out = ['(memory 1)\n(data (i32.const 0) '
           '"\\21\\43\\65\\87\\09\\ba\\dc\\fe\\11\\22\\33\\44\\55\\66\\77\\88")\n']
    n = 0
    for op, sh, amt in (("and", "shl", 2), ("or", "shl", 3), ("xor", "shr_u", 4),
                        ("and", "shr_s", 5), ("or", "shr_u", 1), ("xor", "shl", 7)):
        for swap in (0, 1):
            comp = ("(i32.xor (i32.%s (local.get $x) (i32.const %d)) "
                    "(i32.const -1))" % (sh, amt))
            args = ("%s (local.get $y)" % comp) if swap else ("(local.get $y) %s" % comp)
            out.append('(func (export "fuse_%d") (result i32)\n'
                       "  (local $x i32) (local $y i32)\n"
                       "  (local.set $x (i32.load (i32.const 0)))\n"
                       "  (local.set $y (i32.load (i32.const 4)))\n"
                       "  (i32.add (i32.%s %s)\n"
                       "           (i32.mul (local.get $y) (i32.const -1))))\n"
                       % (n, op, args))
            n += 1
    for op, sh, amt in (("and", "shl", 9), ("or", "shr_u", 33), ("xor", "shr_s", 3)):
        for swap in (0, 1):
            comp = ("(i64.xor (i64.%s (local.get $x) (i64.const %d)) "
                    "(i64.const -1))" % (sh, amt))
            args = ("%s (local.get $y)" % comp) if swap else ("(local.get $y) %s" % comp)
            out.append('(func (export "fuse_%d") (result i64)\n'
                       "  (local $x i64) (local $y i64)\n"
                       "  (local.set $x (i64.load (i32.const 0)))\n"
                       "  (local.set $y (i64.load (i32.const 8)))\n"
                       "  (i64.add (i64.%s %s)\n"
                       "           (i64.mul (local.get $y) (i64.const -1))))\n"
                       % (n, op, args))
            n += 1
    return "".join(out)

def gen_wideframe():
    """Spill slots past the reach of a four-byte scaled offset.

    A slot is eight bytes, but a load or store scales its immediate by the
    *access* width -- so a four-byte access reaches 16380 bytes up the frame
    where an eight-byte one reaches 32760.  Nothing checked that, and the field
    wraps rather than failing to encode, so an i32 or f32 spilled past sixteen
    kilobytes quietly went into somebody else's slot: `wf_i32` below returned
    the wrong sum at -O0 and looped for ever at -O1 and -O2.

    What it takes to get there is more simultaneously live values than two
    thousand slots' worth, which is what the rotating loop is for -- every one
    is read by another after it is written, so none of them dies before the
    loop does.  The `f64` case is here because the reduction that follows the
    loop is i32 whatever the loop held: the wide values were never the ones at
    risk, the narrow temporaries above them were.  `wf_i32_low` is the control
    and stays under the boundary; the frame limit the back end enforces is a
    little under 32760, so going much past 1100 is refused outright rather
    than compiled wrongly.
    """
    out = ['(memory 1)\n']
    for name, k, ty in (("wf_i32_low", 300, "i32"),
                        ("wf_i32", 1100, "i32"),
                        ("wf_f32", 1000, "f32"),
                        ("wf_f64", 700, "f64")):
        L = ['(func (export "%s") (result i32)' % name]
        L.append("  (local " + " ".join([ty] * k) + " i32)")
        n = k
        for i in range(k):
            if ty == "i32":
                L.append("  (local.set %d (i32.const %d))" % (i, i * 7 + 1))
            else:
                L.append("  (local.set %d (%s.const %d.5))" % (i, ty, i % 97))
        L.append("  (local.set %d (i32.const 3))" % n)
        L.append("  (block $done (loop $l")
        L.append("    (br_if $done (i32.eqz (local.get %d)))" % n)
        for i in range(k):
            j = (i + 3) % k
            if ty == "i32":
                L.append("    (local.set %d (i32.add (local.get %d) "
                         "(i32.mul (local.get %d) (i32.const 3))))" % (i, i, j))
            else:
                L.append("    (local.set %d (%s.add (local.get %d) "
                         "(%s.mul (local.get %d) (%s.const 1.25))))"
                         % (i, ty, i, ty, j, ty))
        L.append("    (local.set %d (i32.sub (local.get %d) (i32.const 1)))"
                 % (n, n))
        L.append("    (br $l)))")
        acc = "(i32.const 0)"
        for i in range(k):
            v = ("(local.get %d)" % i if ty == "i32"
                 else "(i32.trunc_sat_%s_s (%s.trunc (local.get %d)))"
                      % (ty, ty, i))
            acc = "(i32.add %s %s)" % (acc, v)
        L.append("  " + acc + ")")
        out.append("\n".join(L) + "\n")
    return "".join(out)

def gen_brtable():
    """`br_table` at the sizes where its lowering changes shape.

    The bound the dispatch compares against is the number of table entries,
    and the module decides that: `br_table` takes a vector, and a switch over
    a few thousand cases is ordinary compiler output.  A compare against an
    immediate has twelve bits, so at 4096 entries the bound stops fitting --
    and warmc died with "internal: cannot encode 12-bit immediate" rather
    than compiling it, at every optimisation level.  Nothing in the corpus or
    in these cases had ever written a table that large, so the ceiling was
    invisible.

    Each size is probed at the first entry, the last, one in the middle and
    an index past the end, which is the only one that reaches the default
    label -- the four together pin the dispatch as well as the bound.  4095
    and 4096 bracket the boundary itself; 8000 is well past it, where the
    materialised bound needs two `movz`/`movk` words rather than one.
    """
    out = []
    for n in (3, 4095, 4096):
        L = ["(func $sw%d (param $x i32) (result i32)" % n]
        L += ["  (block"] * n
        L.append("    (br_table " + " ".join(str(i) for i in range(n)) +
                 " 0 (local.get $x))")
        for i in range(n - 1, -1, -1):
            L.append("  ) (return (i32.const %d))" % (i * 3 + 1))
        L.append("    (i32.const -1))")
        out.append("\n".join(L) + "\n")
        for probe in (0, n // 2, n - 1, n, n + 1000, 0xFFFFFFFF):
            out.append('(func (export "bt%d_%d") (result i32) '
                       "(call $sw%d (i32.const %d)))\n" % (n, probe, n, probe))
    return "".join(out)

def gen_vmctx():
    """Every per-instance array, read at an offset past what a load reaches.

    Globals, tables, memories and funcref cells all live behind one register
    in arrays laid end to end, and a scaled load immediate reaches 4095 times
    its access width into that -- 32,760 bytes for a doubleword and only
    16,380 for the word an i32 global is read with.  Every one of those counts
    was therefore bounded, by a shared budget wearing four names: a module of
    a thousand functions could not address a second memory, and 1,021 i32
    globals was the real ceiling where the guard announced 2,045.

    The offset is what matters and not the count, so the globals do the work:
    two thousand one hundred of them put every later array past the boundary
    while the module still has four memories, which keeps it inside the
    runtime's guarded reservations.  Each array is read at its far end *and*
    at index 0, because the near end is the one the header mirrors and the two
    take different paths.  Every value is written and read back, so a slot
    computed wrongly is a wrong answer rather than a compile that happened to
    succeed.
    """
    NG, NM, NT, NF = 2100, 4, 8, 40
    TYS = ("i32", "i64", "f64", "v128")
    L = []
    for i in range(NG):
        ty = TYS[i % 4]
        lit = {"i32": "(i32.const %d)" % i,
               "i64": "(i64.const %d)" % (i * 1000),
               "f64": "(f64.const %d.5)" % i,
               "v128": "(v128.const i32x4 %d %d %d %d)" % (i, i + 1, i + 2, i + 3)}[ty]
        L.append("(global $g%d (mut %s) %s)" % (i, ty, lit))
    for _ in range(NM):
        L.append("(memory 1)")
    for _ in range(NT):
        L.append("(table 4 funcref)")
    L.append("(type $v_i32 (func (result i32)))")
    for i in range(NF):
        L.append("(func $f%d (result i32) (i32.const %d))" % (i, i * 7 + 3))
    L.append("(elem declare func $f%d $f0)" % (NF - 1,))

    for k in range(4):
        i = NG - 4 + k
        ty = TYS[i % 4]
        if ty == "v128":
            L.append('(func (export "g_%d") (result i32) '
                     "(i32x4.extract_lane 2 (global.get $g%d)))" % (i, i))
            L.append('(func (export "gs_%d") (result i32) '
                     "(global.set $g%d (v128.const i32x4 9 8 7 6)) "
                     "(i32x4.extract_lane 1 (global.get $g%d)))" % (i, i, i))
        else:
            lit = {"i32": "(i32.const 4242)", "i64": "(i64.const 424242)",
                   "f64": "(f64.const 42.25)"}[ty]
            L.append('(func (export "g_%d") (result %s) (global.get $g%d))' % (i, ty, i))
            L.append('(func (export "gs_%d") (result %s) (global.set $g%d %s) '
                     "(global.get $g%d))" % (i, ty, i, lit, i))
    L.append('(func (export "g_first") (result i32) (global.get $g0))')
    L.append('(func (export "gs_first") (result i32) '
             "(global.set $g0 (i32.const 77)) (global.get $g0))")

    L.append('(func (export "mem_last") (result i64) '
             "(i64.store %d (i32.const 8) (i64.const 0x0123456789abcdef)) "
             "(i64.load %d (i32.const 8)))" % (NM - 1, NM - 1))
    L.append('(func (export "mem_last_size") (result i32) (memory.size %d))' % (NM - 1,))
    L.append('(func (export "mem_last_grow") (result i32) '
             "(i32.add (i32.mul (memory.grow %d (i32.const 1)) (i32.const 10)) "
             "(memory.size %d)))" % (NM - 1, NM - 1))
    L.append('(func (export "mem_first") (result i32) '
             "(i32.store (i32.const 4) (i32.const 777)) (i32.load (i32.const 4)))")

    L.append('(func (export "tab_last") (result i32) '
             "(table.set %d (i32.const 2) (ref.func $f%d)) "
             "(call_indirect %d (type $v_i32) (i32.const 2)))" % (NT - 1, NF - 1, NT - 1))
    L.append('(func (export "tab_last_size") (result i32) (table.size %d))' % (NT - 1,))
    L.append('(func (export "tab_first") (result i32) '
             "(table.set 0 (i32.const 1) (ref.func $f0)) "
             "(call_indirect 0 (type $v_i32) (i32.const 1)))")

    L.append('(func (export "ref_last") (result i32) '
             "(table.set %d (i32.const 3) (ref.func $f%d)) "
             "(call_indirect %d (type $v_i32) (i32.const 3)))" % (NT - 1, NF - 1, NT - 1))
    L.append('(func (export "ref_is_null") (result i32) '
             "(ref.is_null (ref.func $f%d)))" % (NF - 1,))
    return "".join("  " + x + "\n" for x in L)

def gen_typedref():
    """Calls through a typed reference, at the frame shapes that are ours.

    The upstream corpus covers what `call_ref` *means* several dozen times
    over; what it does not cover is the arrangement warmc puts it in.  A call
    through a reference reads the callee's code and instance out of a cell
    whose address is itself an operand, so the register holding that cell has
    to survive the argument shuffle -- and the shuffle is only interesting once
    there are more arguments than registers.  `cr_stackargs` has eleven, four
    of them on the stack.

    `cr_tail` is the tail form, which additionally has to keep the cell alive
    across the frame teardown; a million iterations is the same depth
    `tailcall.wat` uses, and it is the number that tells a tail call from an
    ordinary one rather than any answer either could produce.
    """
    L = ['(type $ii (func (param i32) (result i32)))']
    L.append('(type $wide (func (param i32 i32 i32 i32 i32 i32 i32 i32 '
             'i32 i32 i32) (result i32)))')
    L.append('(type $acc (func (param i32 i32) (result i32)))')
    L.append('(func $sq (type $ii) (i32.mul (local.get 0) (local.get 0)))')
    L.append('(func $neg (type $ii) (i32.sub (i32.const 0) (local.get 0)))')
    L.append('(func $sum (type $wide) ' +
             "(i32.add (i32.add (i32.add (i32.add (i32.add (i32.add "
             "(i32.add (i32.add (i32.add (i32.add "
             "(local.get 0) (local.get 1)) (local.get 2)) (local.get 3)) "
             "(local.get 4)) (local.get 5)) (local.get 6)) (local.get 7)) "
             "(local.get 8)) (local.get 9)) (local.get 10)))")

    L.append('(func $loop (type $acc) '
             "(if (result i32) (i32.eqz (local.get 0)) "
             "(then (local.get 1)) "
             "(else (return_call_ref $acc "
             "  (i32.sub (local.get 0) (i32.const 1)) "
             "  (i32.add (local.get 1) (local.get 0)) "
             "  (ref.func $loop)))))")
    L.append("(elem declare func $sq $neg $sum $loop)")

    L.append('(func (export "cr_chain") (result i32) (local $r (ref null $ii))'
             " (local.set $r (ref.func $sq))"
             " (call_ref $ii (call_ref $ii (i32.const 3) (local.get $r))"
             "   (ref.func $neg)))")
    L.append('(func (export "cr_stackargs") (result i32) '
             "(call_ref $wide " +
             " ".join("(i32.const %d)" % (i + 1) for i in range(11)) +
             " (ref.func $sum)))")
    L.append('(func (export "cr_tail") (result i32) '
             "(call_ref $acc (i32.const 1000000) (i32.const 0) "
             "(ref.func $loop)))")

    for name, have in (("cr_br_on_null_hit", 1), ("cr_br_on_null_miss", 0)):
        L.append('(func (export "%s") (result i32) '
                 "(block $miss (result i32) "
                 "  (call_ref $ii (i32.const 6) "
                 "    (br_on_null $miss "
                 "      (if (result (ref null $ii)) (i32.const %d) "
                 "        (then (ref.func $sq)) (else (ref.null $ii)))))) "
                 ")" % (name, have))
    L.append('(func (export "cr_non_null") (result i32) '
             "(call_ref $ii (i32.const 5) "
             "  (ref.as_non_null (ref.func $sq))))")
    return "".join("  " + x + "\n" for x in L)

def gen_exceptions():
    """Exception handling at the frame shapes that are ours.

    The upstream corpus says what `throw` and `try_table` *mean*, in functions
    of two or three instructions.  What it never reaches is the thing that
    actually decides whether a handler works here: an exception does not
    return, so a value the handler still wants has to have been put somewhere
    the unwinder can restore -- a callee-saved register, or a frame slot -- and
    which of those it is depends on register pressure, on how many frames the
    exception passes, and on what those frames themselves saved.

      exn_live_*      values live across the throw, in numbers that force the
                      allocator to use callee-saved registers and then to spill
      exn_deep_*      a throw many frames down, so the walk has to step over
                      frames of several shapes and rebuild the registers each
                      one saved
      exn_fp*         a frame in the middle that saves d8-d15, which are
                      restored from a different place in the frame than the
                      general registers and were the last thing to be wired up
      exn_pay_*       one of every payload type, since a payload slot is
                      sixteen bytes whatever is in it and the load that reads
                      one back is the only thing that knows the width
      exn_loop_*      a handler reached from many call sites, so the pad is a
                      real merge with parameters rather than a straight line

    Every export catches: an exception that escapes is a trap as far as this
    harness is concerned, and those are what the spec suite's throw.wast
    asserts.  Here the question is what the value is afterwards.
    """
    L = []
    L.append('(tag $e0)')
    L.append('(tag $ei (param i32))')
    L.append('(tag $el (param i64))')
    L.append('(tag $ef (param f32))')
    L.append('(tag $ed (param f64))')
    L.append('(tag $ev (param v128))')
    L.append('(tag $emix (param i32 i64 f32 f64 i32))')

    L.append('(tag $ewide (param %s))' % " ".join(["i32"] * 12))
    L.append('(tag $eother (param i32))')

    L.append('(func $t0 (throw $e0))')
    L.append('(func $ti (param i32) (local.get 0) (throw $ei))')
    L.append('(func $tl (param i64) (local.get 0) (throw $el))')
    L.append('(func $tf (param f32) (local.get 0) (throw $ef))')
    L.append('(func $td (param f64) (local.get 0) (throw $ed))')
    L.append('(func $tv (param v128) (local.get 0) (throw $ev))')
    L.append('(func $tmix (i32.const 11) (i64.const 22) (f32.const 33) '
             '(f64.const 44) (i32.const 55) (throw $emix))')
    L.append('(func $twide %s (throw $ewide))'
             % " ".join("(i32.const %d)" % (i + 1) for i in range(12)))
    L.append('(func $tother (i32.const 9) (throw $eother))')

    L.append('(func (export "exn_pay_i32") (result i32) (block $h (result i32)'
             ' (try_table (catch $ei $h) (call $ti (i32.const 0x7fedcba9)))'
             ' (i32.const -1)))')
    L.append('(func (export "exn_pay_i64") (result i64) (block $h (result i64)'
             ' (try_table (catch $el $h) (call $tl (i64.const 0x0123456789abcdef)))'
             ' (i64.const -1)))')
    L.append('(func (export "exn_pay_f32") (result i32) (i32.reinterpret_f32'
             ' (block $h (result f32)'
             ' (try_table (catch $ef $h) (call $tf (f32.const %s)))'
             ' (f32.const 0))))' % f32lit(-2.5))
    L.append('(func (export "exn_pay_f64") (result i64) (i64.reinterpret_f64'
             ' (block $h (result f64)'
             ' (try_table (catch $ed $h) (call $td (f64.const %s)))'
             ' (f64.const 0))))' % f64lit(3.141592653589793))
    L.append('(func (export "exn_pay_v128") (result v128) (block $h (result v128)'
             ' (try_table (catch $ev $h)'
             '   (call $tv (v128.const i32x4 0x11223344 0x55667788'
             '                              0x99aabbcc 0xddeeff00)))'
             ' (v128.const i32x4 0 0 0 0)))')

    L.append('(func (export "exn_pay_mix") (result i64)'
             ' (local $a i32) (local $b i64) (local $c f32) (local $d f64)'
             ' (local $e i32)'
             ' (block $h (result i32 i64 f32 f64 i32)'
             '   (try_table (catch $emix $h) (call $tmix))'
             '   (unreachable))'
             ' (local.set $e) (local.set $d) (local.set $c)'
             ' (local.set $b) (local.set $a)'
             ' (i64.add (i64.extend_i32_u (i32.add (local.get $a) (local.get $e)))'
             '   (i64.add (local.get $b)'
             '     (i64.add (i64.trunc_f32_s (local.get $c))'
             '              (i64.trunc_f64_s (local.get $d))))))')
    L.append('(func (export "exn_pay_wide") (result i32)'
             ' (block $h (result %s)'
             '   (try_table (catch $ewide $h) (call $twide))'
             '   (unreachable))'
             ' %s)' % (" ".join(["i32"] * 12), " ".join(["(i32.add)"] * 11)))

    def fold(op, terms):
        """Nested folded applications of a binary operator over `terms`."""
        out = terms[0]
        for t in terms[1:]:
            out = "(%s %s %s)" % (op, out, t)
        return out

    n = 12
    live = " ".join("(local.set $v%d (i32.const %d))" % (i, 100 + i * 7)
                    for i in range(n))
    L.append('(func (export "exn_live_i32") (result i32) %s'
             ' %s'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 5)))'
             '   (i32.const -1))'
             ' (i32.add %s))'
             % (" ".join("(local $v%d i32)" % i for i in range(n)), live,
                fold("i32.add", ["(local.get $v%d)" % i for i in range(n)])))
    liveL = " ".join("(local.set $w%d (i64.const %d))" % (i, 1 << (i + 20))
                     for i in range(n))
    L.append('(func (export "exn_live_i64") (result i64) %s'
             ' %s'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 5)))'
             '   (i32.const -1))'
             ' (drop) %s)'
             % (" ".join("(local $w%d i64)" % i for i in range(n)), liveL,
                fold("i64.add", ["(local.get $w%d)" % i for i in range(n)])))

    nf = 10
    liveF = " ".join("(local.set $f%d (f64.const %s))" % (i, f64lit(i * 1.5 + 0.25))
                     for i in range(nf))
    L.append('(func (export "exn_fp_live") (result i64) %s'
             ' %s'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 5)))'
             '   (i32.const -1))'
             ' (drop) (i64.reinterpret_f64 %s))'
             % (" ".join("(local $f%d f64)" % i for i in range(nf)), liveF,
                fold("f64.add", ["(local.get $f%d)" % i for i in range(nf)])))

    midi = " ".join("(local.set $m%d (i64.mul (local.get $seed) (i64.const %d)))"
                    % (i, i + 3) for i in range(10))
    midf = " ".join("(local.set $g%d (f64.const %s))" % (i, f64lit(i + 0.5))
                    for i in range(8))
    L.append('(func $mid (param $seed i64) (param $n i32) (result i64) %s %s'
             ' %s %s'
             ' (if (i32.eqz (local.get $n)) (then (call $ti (i32.const 77))))'
             ' (if (i32.gt_s (local.get $n) (i32.const 0))'
             '   (then (drop (call $mid (local.get $seed)'
             '                (i32.sub (local.get $n) (i32.const 1))))))'
             ' (i64.add %s (i64.trunc_f64_s %s)))'
             % (" ".join("(local $m%d i64)" % i for i in range(10)),
                " ".join("(local $g%d f64)" % i for i in range(8)),
                midi, midf,
                fold("i64.add", ["(local.get $m%d)" % i for i in range(10)]),
                fold("f64.add", ["(local.get $g%d)" % i for i in range(8)])))
    for depth in (1, 4, 12):
        L.append('(func (export "exn_deep_%d") (result i64) (local $k i64)'
                 ' (local.set $k (i64.const 0x0fedcba987654321))'
                 ' (block $h (result i32)'
                 '   (try_table (catch $ei $h) (drop (call $mid (i64.const 3)'
                 '                                    (i32.const %d))))'
                 '   (i32.const -1))'
                 ' (drop) (local.get $k))' % (depth, depth))

    for name, callee in (("exn_nested", "$ti (i32.const 4)"),
                         ("exn_nested_inner", "$tother")):
        L.append('(func (export "%s") (result i32)'
                 ' (block $out (result i32)'
                 '   (try_table (result i32) (catch $ei $out)'
                 '     (block $in (result i32)'
                 '       (try_table (result i32) (catch $eother $in)'
                 '         (call %s) (i32.const -1))))))' % (name, callee))
    L.append('(func (export "exn_catch_all") (result i32)'
             ' (block $h'
             '   (try_table (catch_all $h) (call $t0))'
             '   (return (i32.const -1)))'
             ' (i32.const 4242))')

    L.append('(func (export "exn_rethrow") (result i32) (local $x exnref)'
             ' (block $h (result exnref)'
             '   (try_table (result exnref) (catch_all_ref $h) (call $ti (i32.const 63))'
             '     (unreachable)))'
             ' (local.set $x)'
             ' (block $h2 (result i32)'
             '   (try_table (catch $ei $h2) (throw_ref (local.get $x)))'
             '   (i32.const -1)))')

    body = "".join(
        " (local.set $acc (i32.add (local.get $acc) (i32.const %d)))"
        " (call $ti (local.get $n))" % (i + 1) for i in range(10))
    L.append('(func $chain (param $n i32) (result i32) (local $acc i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) %s (local.get $acc))'
             '   (return))'
             ' (i32.add (local.get $acc)))' % body)
    L.append('(func (export "exn_many_sites") (result i32) '
             '(call $chain (i32.const 5)))')

    L.append('(func (export "exn_in_loop") (result i32) (local $i i32) (local $s i32)'
             ' (loop $again'
             '   (local.set $s (i32.add (local.get $s)'
             '     (block $h (result i32)'
             '       (try_table (result i32) (catch $ei $h)'
             '         (call $ti (local.get $i)) (i32.const 0))'
             '       (return))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $again (i32.lt_s (local.get $i) (i32.const 8))))'
             ' (local.get $s))')

    L.append('(func (export "exn_to_loop") (result i32) (local $n i32) (local $acc i32)'
             ' (i32.const 0)'
             ' (loop $l (param i32) (result i32)'
             '   (local.set $acc (i32.add (local.get $acc)))'
             '   (local.set $n (i32.add (local.get $n) (i32.const 1)))'
             '   (if (result i32) (i32.ge_s (local.get $n) (i32.const 4))'
             '     (then (local.get $acc))'
             '     (else (try_table (result i32) (catch $ei $l)'
             '             (call $ti (local.get $n)) (i32.const -1))))))')

    nres = 1000
    L.append('(type $huge (func (result %s)))' % " ".join(["i32"] * nres))
    L.append('(type $pair (func (result i32 i32)))')
    L.append('(func $unused (type $huge) (unreachable))')
    L.append('(func $two (type $pair) (i32.const 1) (i32.const 2))')
    L.append('(func $tall (result i32 i32)'
             ' (call_indirect (type $pair) (i32.const 2)) (drop) (drop)'
             ' (i32.const 41) (i32.const 1) (throw $emix2))')
    L.append('(tag $emix2 (param i32 i32))')
    L.append('(func (export "exn_high_payload") (result i32)'
             ' (block $h (result i32 i32)'
             '   (try_table (result i32 i32) (catch $emix2 $h) (call $tall))'
             '   (unreachable))'
             ' (i32.add))')

    L.append('(table 3 funcref) (elem (i32.const 0) $ti $t0 $two)')
    L.append('(func (export "exn_indirect") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (call_indirect (param i32) (i32.const 88) (i32.const 0)))'
             '   (i32.const -1)))')

    L.append('(type $iv (func (param i32)))')
    L.append('(func (export "exn_call_ref") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (call_ref $iv (i32.const 89) (ref.func $ti)))'
             '   (i32.const -1)))')

    nw = 12
    L.append('(func $wide_i (param %s) (result i32) (local.get %d) (throw $ei))'
             % (" ".join(["i32"] * nw), nw - 1))
    L.append('(func (export "exn_stack_args") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $wide_i %s))'
             '   (return))'
             ' (i32.add (i32.const 1000)))'
             % " ".join("(i32.const %d)" % (i + 1) for i in range(nw)))
    L.append('(func $wide_f (param %s) (result f64) (local.get %d) (throw $ed))'
             % (" ".join(["f64"] * nw), nw - 1))
    L.append('(func (export "exn_stack_args_fp") (result i64)'
             ' (i64.reinterpret_f64'
             '   (block $h (result f64)'
             '     (try_table (result f64) (catch $ed $h) (call $wide_f %s))'
             '     (return (i64.const 0)))))'
             % " ".join("(f64.const %s)" % f64lit(i + 1.25) for i in range(nw)))

    mixp = " ".join(["i32", "f64", "v128"] * 4)
    mixa = " ".join(("(i32.const %d)" % (i + 1),
                     "(f64.const %s)" % f64lit(i + 0.5),
                     "(v128.const i32x4 %d %d %d %d)" % (i, i, i, i))[j]
                    for i in range(4) for j in range(3))
    L.append('(func $wide_mix (param %s) (result i32) (local.get 9) (throw $ei))'
             % mixp)
    L.append('(func (export "exn_stack_args_mix") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $wide_mix %s))'
             '   (return))'
             ' (i32.add (i32.const 2000)))' % mixa)

    nv = 6
    L.append('(func (export "exn_v128_live") (result v128) %s %s'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 5)))'
             '   (i32.const -1))'
             ' (drop) %s)'
             % (" ".join("(local $q%d v128)" % i for i in range(nv)),
                " ".join('(local.set $q%d (v128.const i32x4 %d %d %d %d))'
                         % (i, i + 1, i + 21, i + 41, i + 61) for i in range(nv)),
                fold("i32x4.add", ["(local.get $q%d)" % i for i in range(nv)])))

    L.append('(func $ping (param $n i32) (result i64) %s %s'
             ' (if (i32.le_s (local.get $n) (i32.const 0))'
             '   (then (call $ti (i32.const 91))))'
             ' (drop (call $pong (i32.sub (local.get $n) (i32.const 1))))'
             ' %s)'
             % (" ".join("(local $p%d i64)" % i for i in range(10)),
                " ".join('(local.set $p%d (i64.mul (i64.extend_i32_s '
                         '(local.get $n)) (i64.const %d)))' % (i, i + 3)
                         for i in range(10)),
                fold("i64.add", ["(local.get $p%d)" % i for i in range(10)])))
    L.append('(func $pong (param $n i32) (result i64) %s %s'
             ' (if (i32.le_s (local.get $n) (i32.const 0))'
             '   (then (call $ti (i32.const 92))))'
             ' (drop (call $ping (i32.sub (local.get $n) (i32.const 1))))'
             ' (i64.trunc_f64_s %s))'
             % (" ".join("(local $r%d f64)" % i for i in range(8)),
                " ".join('(local.set $r%d (f64.add (f64.convert_i32_s '
                         '(local.get $n)) (f64.const %s)))' % (i, f64lit(i + 0.25))
                         for i in range(8)),
                fold("f64.add", ["(local.get $r%d)" % i for i in range(8)])))
    for depth in (2, 3, 40):
        L.append('(func (export "exn_alt_%d") (result i64) (local $k i64)'
                 ' (local.set $k (i64.const 0x1234abcd5678ef90))'
                 ' (block $h (result i32)'
                 '   (try_table (catch $ei $h)'
                 '     (drop (call $ping (i32.const %d))))'
                 '   (i32.const -1))'
                 ' (i64.add (i64.extend_i32_u) (local.get $k)))' % (depth, depth))

    L.append('(func (export "exn_deep_200") (result i64) (local $k i64)'
             ' (local.set $k (i64.const 0x0fedcba987654321))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (drop (call $mid (i64.const 3)'
             '                                    (i32.const 200))))'
             '   (i32.const -1))'
             ' (drop) (local.get $k))')

    L.append('(func $shield (param $n i32) (result i64) %s %s'
             ' (block $hh (result i32)'
             '   (try_table (result i32) (catch $eother $hh)'
             '     (call $ti (local.get $n)) (i32.const 0))'
             '   (return (i64.const -1)))'
             ' (drop) %s)'
             % (" ".join("(local $s%d i64)" % i for i in range(9)),
                " ".join('(local.set $s%d (i64.mul (i64.extend_i32_s '
                         '(local.get $n)) (i64.const %d)))' % (i, i + 2)
                         for i in range(9)),
                fold("i64.add", ["(local.get $s%d)" % i for i in range(9)])))
    L.append('(func (export "exn_past_other_handler") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (drop (call $shield (i32.const 17))))'
             '   (i32.const -1)))')
    L.append('(func $after (param $n i32) (result i32) (local $r i32)'
             ' (local.set $r (block $hh (result i32)'
             '   (try_table (result i32) (catch $ei $hh) (i32.const 7))))'
             ' (call $ti (local.get $n))'
             ' (local.get $r))')
    L.append('(func (export "exn_past_ended_region") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $after (i32.const 33)))'
             '   (return))'
             ' (i32.add (i32.const 3000)))')

    npad = 20
    regions = "".join(
        ' (local.set $acc (i32.add (local.get $acc)'
        '   (block $h%d (result i32)'
        '     (try_table (result i32) (catch $ei $h%d) (call $ti (i32.const %d))'
        '       (i32.const -1)))))' % (i, i, i + 1) for i in range(npad))
    L.append('(func (export "exn_many_pads") (result i32) (local $acc i32)%s'
             ' (local.get $acc))' % regions)

    L.append('(memory 1)')
    L.append('(global $g (mut i32) (i32.const 0))')
    L.append('(func (export "exn_mem_across") (result i32)'
             ' (i32.store (i32.const 0) (i32.const 0x1234))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (i32.store (i32.const 4) (i32.const 0x5678))'
             '     (call $ti (i32.const 15))'
             '     (i32.store (i32.const 4) (i32.const 0x9999)))'
             '   (return (i32.const -1)))'
             ' (drop)'
             ' (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))')
    L.append('(func (export "exn_mem_dyn") (result i32) (local $p i32)'
             ' (local.set $p (i32.and (i32.load (i32.const 0)) (i32.const 0xff)))'
             ' (i32.store (local.get $p) (i32.const 7))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 3)))'
             '   (return (i32.const -1)))'
             ' (i32.add (i32.load (local.get $p))))')
    L.append('(func (export "exn_global_across") (result i32)'
             ' (global.set $g (i32.const 11))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (global.set $g (i32.const 22))'
             '     (call $ti (i32.const 33))'
             '     (global.set $g (i32.const 44)))'
             '   (return (i32.const -1)))'
             ' (i32.add (global.get $g)))')

    nl = 1800
    setl = " ".join("(local.set %d (i64.const %d))" % (i, i * 7 + 1)
                    for i in range(nl))

    getl = "(local.get 0) " + " ".join("(local.get %d) (i64.add)" % i
                                       for i in range(1, nl))
    L.append('(func $maybe (param $n i32) (if (i32.eqz (local.get $n))'
             ' (then (call $ti (i32.const 5)))))')
    L.append('(func (export "exn_wide_pad") (result i64)'
             ' (local %s) (local $a i32) (local $b f32) (local $c f64)'
             ' (local $d v128) %s'
             ' (local.set $a (i32.const 11)) (local.set $b (f32.const %s))'
             ' (local.set $c (f64.const %s))'
             ' (local.set $d (v128.const i32x4 1 2 3 4))'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (call $maybe (i32.const 1))'
             '     (local.set $a (i32.const 22)) (local.set $b (f32.const %s))'
             '     (local.set $c (f64.const %s))'
             '     (local.set $d (v128.const i32x4 5 6 7 8))'
             '     (call $maybe (i32.const 0))'
             '     (local.set $a (i32.const 33))))'
             ' %s'
             ' (i64.add (i64.extend_i32_s (local.get $a))'
             '   (i64.add (i64.trunc_f32_s (local.get $b))'
             '     (i64.add (i64.trunc_f64_s (local.get $c))'
             '       (i64.extend_i32_s (i32x4.extract_lane 3 (local.get $d))))))'
             ' (i64.add))'
             % (" ".join(["i64"] * nl), setl,
                f32lit(2.5), f64lit(4.5), f32lit(6.5), f64lit(8.5), getl))
    return "".join("  " + x + "\n" for x in L)

def gen_exn_clauses():
    """Which clause takes an exception, and where it goes when one does.

    `exceptions` asks what the machine state is after a handler runs.  This
    asks the question before that one: given a stack of regions, which of them
    is entered at all.  The answer is a search -- outward through frames, and
    within a frame outward through regions, taking the *first* clause of the
    innermost region that names the tag -- and every step of it has a way of
    being wrong that leaves the module running and silently catching the wrong
    thing:

      cl_pick_*        three clauses on one region, one tag each
      cl_all_first     `catch_all` ahead of a `catch` that also matches
      cl_dup           the same tag named twice, so only clause order decides
      cl_nest_*        regions inside regions, matching at different depths
      cl_past_*        a region that is not entered: wrong tag, already ended,
                       not yet begun, or no clauses at all
      cl_label_*       where a clause branches to -- several blocks out, the
                       function itself, a loop, a multi-value block
      cl_normal_*      the paths through a region that raise nothing, since a
                       region also has to be an ordinary block

    Every export catches, so a wrong answer is a wrong number rather than a
    crash: `wasm-interp` says what the number should be.
    """
    L = []
    L.append('(tag $a (param i32))')
    L.append('(tag $b (param i32))')
    L.append('(tag $c)')
    L.append('(tag $five (param i32 i64 f32 f64 i32))')
    L.append('(func $ta (param i32) (local.get 0) (throw $a))')
    L.append('(func $tb (param i32) (local.get 0) (throw $b))')
    L.append('(func $tc (throw $c))')
    L.append('(func $t5 (i32.const 1) (i64.const 2) (f32.const 3) '
             '(f64.const 4) (i32.const 5) (throw $five))')

    L.append('(table 4 funcref) (elem (i32.const 0) $ta $tb $tc $ta)')
    L.append('(type $iv (func (param i32)))')
    L.append('(type $vv (func))')

    three = ('(block $ha (result i32)'
             '  (block $hb (result i32)'
             '    (block $hc'
             '      (try_table (catch $a $ha) (catch $b $hb) (catch $c $hc) %s)'
             '      (return (i32.const -1)))'
             '    (return (i32.const 300)))'
             '  (return (i32.add (i32.const 200))))'
             '(i32.add (i32.const 100))')
    for name, call in (("cl_pick_a", "(call $ta (i32.const 1))"),
                       ("cl_pick_b", "(call $tb (i32.const 2))"),
                       ("cl_pick_c", "(call $tc)")):
        L.append('(func (export "%s") (result i32) %s)' % (name, three % call))

    L.append('(func (export "cl_pick_dyn") (result i32) %s)'
             % (three % '(call_indirect (type $iv) (i32.const 4) (i32.const 1))'))

    L.append('(func (export "cl_all_first") (result i32)'
             ' (block $all'
             '   (block $ha (result i32)'
             '     (try_table (catch_all $all) (catch $a $ha) (call $ta (i32.const 7)))'
             '     (return (i32.const -1)))'
             '   (return (i32.const -2)))'
             ' (i32.const 777))')
    L.append('(func (export "cl_dup") (result i32)'
             ' (block $h1 (result i32)'
             '   (block $h2 (result i32)'
             '     (try_table (catch $a $h1) (catch $a $h2) (call $ta (i32.const 3)))'
             '     (return (i32.const -1)))'
             '   (return (i32.add (i32.const 200))))'
             ' (i32.add (i32.const 100)))')

    L.append('(func (export "cl_catch_ref") (result i32)'
             ' (block $h (result i32 exnref)'
             '   (try_table (result i32) (catch_ref $a $h) (call $ta (i32.const 41))'
             '     (i32.const -1))'
             '   (return))'
             ' (ref.is_null) (i32.eqz) (i32.mul (i32.const 2)) (i32.add))')
    L.append('(func (export "cl_catch_all_ref") (result i32)'
             ' (block $h (result exnref)'
             '   (try_table (catch_all_ref $h) (call $tc))'
             '   (return (i32.const -1)))'
             ' (ref.is_null) (i32.eqz) (i32.add (i32.const 50)))')

    nest = ('(block $out (result i32)'
            '  (block $in (result i32)'
            '    (try_table (result i32) (catch $b $out)'
            '      (try_table (result i32) (catch $a $in) %s (i32.const -1)))'
            '    (return))'
            '  (return (i32.add (i32.const 10))))'
            '(i32.add (i32.const 20))')
    L.append('(func (export "cl_nest_inner") (result i32) %s)'
             % (nest % "(call $ta (i32.const 1))"))
    L.append('(func (export "cl_nest_outer") (result i32) %s)'
             % (nest % "(call $tb (i32.const 2))"))

    L.append('(func (export "cl_nest_same_tag") (result i32)'
             ' (block $out (result i32)'
             '   (block $in (result i32)'
             '     (try_table (result i32) (catch $a $out)'
             '       (try_table (result i32) (catch $a $in) (call $ta (i32.const 4))'
             '         (i32.const -1)))'
             '     (return))'
             '   (return (i32.add (i32.const 10))))'
             ' (i32.add (i32.const 20)))')

    L.append('(func (export "cl_throw_in_handler") (result i32)'
             ' (block $out (result i32)'
             '   (try_table (result i32) (catch $b $out)'
             '     (block $in (result i32)'
             '       (try_table (result i32) (catch $a $in) (call $ta (i32.const 4))'
             '         (i32.const -1)))'
             '     (call $tb (i32.add (i32.const 900))) (i32.const -3))))')

    L.append('(func (export "cl_region_after_pad") (result i32)'
             ' (block $out (result i32)'
             '   (try_table (result i32) (catch $a $out)'
             '     (call $ta (i32.const 6)) (i32.const -1))'
             '   (return))'
             ' (block $h2 (result i32)'
             '   (try_table (result i32) (catch $b $h2) (call $tb (i32.const 70))'
             '     (i32.const -2))'
             '   (return))'
             ' (i32.add))')

    L.append('(func (export "cl_no_clauses") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $a $h)'
             '     (try_table (call $ta (i32.const 21))))'
             '   (i32.const -1)))')
    L.append('(func (export "cl_empty_region") (result i32)'
             ' (try_table (catch $a 0)) (i32.const 22))')

    L.append('(func $between (param $n i32) (result i32) (local $r i32)'
             ' (local.set $r (block $h1 (result i32)'
             '   (try_table (result i32) (catch $a $h1) (i32.const 7))))'
             ' (call $ta (local.get $n))'
             ' (block $h2 (result i32)'
             '   (try_table (result i32) (catch $a $h2) (i32.const 8)))'
             ' (i32.add (local.get $r)))')
    L.append('(func (export "cl_past_ended") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $a $h) (call $between (i32.const 31)))'
             '   (return))'
             ' (i32.add (i32.const 400)))')

    L.append('(func $w1 (param $n i32) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $b $h) (call $ta (local.get $n))'
             '     (i32.const -1))'
             '   (return)))')
    L.append('(func $w2 (param $n i32) (result i32)'
             ' (block $h'
             '   (try_table (catch $c $h) (return (call $w1 (local.get $n))))'
             '   (return (i32.const -3))) (i32.const -4))')
    L.append('(func $w3 (param $n i32) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $b $h) (call $w2 (local.get $n)))'
             '   (return)))')
    L.append('(func (export "cl_past_three_frames") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $a $h) (call $w3 (i32.const 26)))'
             '   (return))'
             ' (i32.add (i32.const 500)))')

    L.append('(func (export "cl_label_far") (result i32)'
             ' (block $l3 (result i32)'
             '   (block $l2'
             '     (block $l1'
             '       (block $l0'
             '         (try_table (catch $a $l3) (call $ta (i32.const 61)))'
             '         (return (i32.const -1)))'
             '       (return (i32.const -2)))'
             '     (return (i32.const -3)))'
             '   (return (i32.const -4)))'
             ' (i32.add (i32.const 600)))')

    L.append('(func $ret_from_pad (param $n i32) (result i32)'
             ' (try_table (catch $a 0) (call $ta (local.get $n)))'
             ' (i32.const -1))')
    L.append('(func (export "cl_label_func") (result i32)'
             ' (i32.add (i32.const 700) (call $ret_from_pad (i32.const 71))))')
    L.append('(func (export "cl_label_five") (result i64)'
             ' (local $p0 i32) (local $p1 i64) (local $p2 f32) (local $p3 f64)'
             ' (local $p4 i32)'
             ' (block $h (result i32 i64 f32 f64 i32)'
             '   (try_table (catch $five $h) (call $t5))'
             '   (unreachable))'
             ' (local.set $p4) (local.set $p3) (local.set $p2)'
             ' (local.set $p1) (local.set $p0)'
             ' (i64.add (i64.extend_i32_s (i32.add (local.get $p0) (local.get $p4)))'
             '   (i64.add (local.get $p1)'
             '     (i64.add (i64.trunc_f32_s (local.get $p2))'
             '              (i64.trunc_f64_s (local.get $p3))))))')

    L.append('(func (export "cl_label_loop") (result i32)'
             ' (local $n i32) (local $acc i32)'
             ' (i32.const 0)'
             ' (loop $l (param i32) (result i32)'
             '   (local.set $acc (i32.add (local.get $acc)))'
             '   (local.set $n (i32.add (local.get $n) (i32.const 1)))'
             '   (if (result i32) (i32.ge_s (local.get $n) (i32.const 5))'
             '     (then (local.get $acc))'
             '     (else (try_table (result i32) (catch $a $l)'
             '             (call $ta (i32.mul (local.get $n) (i32.const 3)))'
             '             (i32.const -1))))))')

    L.append('(func (export "cl_normal_fall") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $a $h) (i32.const 81))'
             '   (return))'
             ' (i32.add (i32.const 800)))')
    L.append('(func (export "cl_normal_br") (result i32)'
             ' (block $done (result i32)'
             '   (block $h (result i32)'
             '     (try_table (result i32) (catch $a $h)'
             '       (br $done (i32.const 82)) (i32.const -1))'
             '     (return))'
             '   (i32.add (i32.const 800))))')
    L.append('(func (export "cl_normal_return") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $a $h)'
             '     (return (i32.const 83)) (i32.const -1))'
             '   (return))'
             ' (i32.add (i32.const 800)))')
    L.append('(func (export "cl_normal_brtable") (result i32)'
             ' (block $h (result i32)'
             '   (block $x'
             '     (try_table (catch $a $h)'
             '       (drop (call $sometimes (i32.const 1)))'
             '       (br_table $x $x $x (i32.const 2))))'
             '   (return (i32.const 84)))'
             ' (i32.add (i32.const 800)))')

    L.append('(func $sometimes (param $n i32) (result i32)'
             ' (if (i32.eqz (i32.and (local.get $n) (i32.const 1)))'
             '   (then (call $ta (local.get $n))))'
             ' (i32.mul (local.get $n) (i32.const 100)))')
    L.append('(func (export "cl_both_paths") (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (loop $again'
             '   (local.set $s (i32.add (local.get $s)'
             '     (block $h (result i32)'
             '       (try_table (result i32) (catch $a $h)'
             '         (call $sometimes (local.get $i))))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $again (i32.lt_s (local.get $i) (i32.const 8))))'
             ' (local.get $s))')

    L.append('(func (export "cl_two_pads_loop") (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (loop $again'
             '   (local.set $s (i32.add (local.get $s) (i32.mul (i32.const 10)'
             '     (block $h1 (result i32)'
             '       (try_table (result i32) (catch $a $h1)'
             '         (call $ta (local.get $i)) (i32.const -1))))))'
             '   (local.set $s (i32.add (local.get $s)'
             '     (block $h2 (result i32)'
             '       (try_table (result i32) (catch $b $h2)'
             '         (call $tb (i32.add (local.get $i) (i32.const 100)))'
             '         (i32.const -1)))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $again (i32.lt_s (local.get $i) (i32.const 4))))'
             ' (local.get $s))')

    L.append('(func (export "cl_second_call") (result i32) (local $s i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $a $h)'
             '     (local.set $s (call $sometimes (i32.const 3)))'
             '     (call $sometimes (i32.const 4)))'
             '   (return))'
             ' (i32.add (local.get $s)))')
    return "".join("  " + x + "\n" for x in L)

def gen_exnref():
    """The exception object once it is an ordinary value.

    A `catch_ref` clause pushes an `exnref`, and from there it is a value like
    any other: it goes into a local, a global, a table slot, a parameter and a
    result, it is selected between, it is tested for null, and it outlives by
    any amount the frame that caught it.  `exceptions` never reaches that,
    because there the object is caught and immediately re-thrown -- the one
    path on which it is never stored anywhere.

      xr_through_*   a local, a parameter, a result, a global, a table slot
      xr_outlives    caught in one invocation and thrown in the next, so the
                     frame that held it is long gone
      xr_null_*      `ref.null exn`, and what tells it apart from a caught one
      xr_twice       one object thrown twice: throwing must not consume it
      xr_many        more live objects than there are callee-saved registers
    """
    L = []
    L.append('(tag $ei (param i32))')
    L.append('(tag $e0)')
    L.append('(global $g (mut exnref) (ref.null exn))')
    L.append('(table $t 4 exnref)')
    L.append('(type $xx (func (param exnref) (result exnref)))')
    L.append('(func $ti (param i32) (local.get 0) (throw $ei))')

    L.append('(func $catch (param $n i32) (result exnref)'
             ' (block $h (result exnref)'
             '   (try_table (result exnref) (catch_all_ref $h)'
             '     (call $ti (local.get $n)) (unreachable))))')

    L.append('(func $payload (param $x exnref) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (throw_ref (local.get $x)))'
             '   (i32.const -1)))')

    L.append('(func $id (type $xx) (local.get 0))')
    L.append('(elem declare func $id $catch)')
    L.append('(func (export "xr_through_local") (result i32) (local $x exnref)'
             ' (local.set $x (call $catch (i32.const 11)))'
             ' (call $payload (local.get $x)))')
    L.append('(func (export "xr_through_param") (result i32)'
             ' (call $payload (call $id (call $catch (i32.const 12)))))')
    L.append('(func (export "xr_through_global") (result i32)'
             ' (global.set $g (call $catch (i32.const 13)))'
             ' (call $payload (global.get $g)))')
    L.append('(func (export "xr_through_table") (result i32)'
             ' (table.set $t (i32.const 2) (call $catch (i32.const 14)))'
             ' (call $payload (table.get $t (i32.const 2))))')
    L.append('(func (export "xr_through_call_ref") (result i32)'
             ' (call $payload (call_ref $xx (call $catch (i32.const 15))'
             '                              (ref.func $id))))')

    L.append('(func $down (param $x exnref) (param $n i32)'
             ' (if (i32.eqz (local.get $n)) (then (throw_ref (local.get $x))))'
             ' (call $down (local.get $x) (i32.sub (local.get $n) (i32.const 1))))')
    L.append('(func (export "xr_carried_down") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (call $down (call $catch (i32.const 16)) (i32.const 5)))'
             '   (i32.const -1)))')
    L.append('(func $up (param $n i32) (result exnref)'
             ' (if (i32.eqz (local.get $n)) (then (return (call $catch (i32.const 17)))))'
             ' (call $up (i32.sub (local.get $n) (i32.const 1))))')
    L.append('(func (export "xr_carried_up") (result i32)'
             ' (call $payload (call $up (i32.const 5))))')

    L.append('(func (export "xr_outlives_store") (result i32)'
             ' (global.set $g (call $catch (i32.const 18)))'
             ' (i32.eqz (ref.is_null (global.get $g))))')
    L.append('(func (export "xr_outlives_throw") (result i32)'
             ' (call $payload (global.get $g)))')

    L.append('(func (export "xr_null_is_null") (result i32)'
             ' (ref.is_null (ref.null exn)))')
    L.append('(func (export "xr_caught_not_null") (result i32)'
             ' (ref.is_null (call $catch (i32.const 19))))')
    L.append('(func (export "xr_null_select") (result i32)'
             ' (i32.add'
             '   (i32.mul (i32.const 10) (ref.is_null (select (result exnref)'
             '     (ref.null exn) (call $catch (i32.const 20)) (i32.const 1))))'
             '   (ref.is_null (select (result exnref)'
             '     (ref.null exn) (call $catch (i32.const 21)) (i32.const 0)))))')
    L.append('(func (export "xr_null_br_on_null") (result i32)'
             ' (block $n'
             '   (br_on_null $n (ref.null exn))'
             '   (drop) (return (i32.const -1)))'
             ' (block $n2'
             '   (br_on_null $n2 (call $catch (i32.const 22)))'
             '   (drop) (return (i32.const 23)))'
             ' (i32.const -2))')

    L.append('(func (export "xr_table_defaults") (result i32)'
             ' (table.set $t (i32.const 0) (call $catch (i32.const 24)))'
             ' (i32.add'
             '   (i32.mul (i32.const 100) (table.grow $t (ref.null exn) (i32.const 2)))'
             '   (i32.add (i32.mul (i32.const 10) (table.size $t))'
             '     (i32.add'
             '       (i32.mul (i32.const 2) (ref.is_null (table.get $t (i32.const 5))))'
             '       (ref.is_null (table.get $t (i32.const 0)))))))')
    L.append('(func (export "xr_table_fill") (result i32)'
             ' (table.fill $t (i32.const 0) (ref.null exn) (i32.const 4))'
             ' (i32.add (i32.mul (i32.const 2) (ref.is_null (table.get $t (i32.const 0))))'
             '   (ref.is_null (table.get $t (i32.const 3)))))')

    L.append('(func (export "xr_twice") (result i32) (local $x exnref)'
             ' (local.set $x (call $catch (i32.const 25)))'
             ' (i32.add (call $payload (local.get $x))'
             '          (call $payload (local.get $x))))')
    L.append('(func (export "xr_twice_nested") (result i32) (local $x exnref)'
             ' (local.set $x (call $catch (i32.const 26)))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (i32.add (call $payload (local.get $x)) (i32.const 1000))'
             '     (drop) (throw_ref (local.get $x)) (i32.const -1))'
             '   (return))'
             ' (i32.add (i32.const 2000)))')

    L.append('(func (export "xr_two_pads") (result i32)'
             ' (local $x exnref) (local $y exnref)'
             ' (block $h1 (result exnref)'
             '   (try_table (result exnref) (catch_all_ref $h1)'
             '     (call $ti (i32.const 61)) (unreachable)))'
             ' (local.set $x)'
             ' (block $h2 (result exnref)'
             '   (try_table (result exnref) (catch_all_ref $h2)'
             '     (call $ti (i32.const 62)) (unreachable)))'
             ' (local.set $y)'
             ' (i32.add (i32.mul (i32.const 100) (call $payload (local.get $x)))'
             '          (call $payload (local.get $y))))')

    nx = 12
    L.append('(func (export "xr_many") (result i32) %s %s %s %s)'
             % (" ".join("(local $x%d exnref)" % i for i in range(nx)),
                " ".join("(local.set $x%d (call $catch (i32.const %d)))"
                         % (i, 30 + i) for i in range(nx)),
                " ".join("(call $payload (local.get $x%d))" % i for i in range(nx)),
                " ".join(["(i32.add)"] * (nx - 1))))

    L.append('(func (export "xr_loop_carried") (result i32)'
             ' (local $i i32) (local $s i32) (local $x exnref) (local $y exnref)'
             ' (local.set $x (call $catch (i32.const 40)))'
             ' (loop $again'
             '   (local.set $y (local.get $x))'
             '   (local.set $x (call $catch (i32.add (local.get $i) (i32.const 50))))'
             '   (local.set $s (i32.add (local.get $s)'
             '     (call $payload (local.get $y))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $again (i32.lt_s (local.get $i) (i32.const 6))))'
             ' (i32.add (local.get $s) (call $payload (local.get $x))))')
    return "".join("  " + x + "\n" for x in L)

def gen_exn_walk():
    """How far an exception travels, and what it passes on the way.

    `exceptions` throws one or two frames down; `exnclause` asks which clause
    of one function takes it.  Neither reaches the case the unwinder actually
    exists for: a handler a long way up a stack of *different* functions, none
    of which runs another instruction of its own after the throw.  Every frame
    between the two is one the walk has to step over correctly -- restoring
    what that frame saved, skipping a handler list that does not match, and
    not stopping at a region that has already ended -- and getting any of it
    wrong leaves a module that still runs.

      wk_*      a chain of sixteen distinct functions of three rotating frame
                shapes, throwing at each depth in turn, so every walk length
                from one frame to sixteen is a case of its own
      mw_*      the same with handlers at three of the levels: the innermost
                *enclosing* one has to take it, not the first one the walk
                meets going the other way and not the outermost
      hop_*     caught, re-raised, caught again -- three times up one stack
      tail_*    a chain in which every other link is a tail call, so half the
                frames the calls suggest are not there to be walked
      kind_*    links that are direct calls, indirect calls and `call_ref`
      thick_*   one link with a frame of five hundred slots, and one with
                eight regions of its own that have all ended

    Every frame that survives contributes its locals to the answer, so a
    register or slot the walk failed to restore is a wrong number rather than
    a crash; `wasm-interp` says what the number should be.
    """
    L = []
    L.append('(tag $ti (param i32))')
    L.append('(tag $other (param i32))')
    L.append('(global $who (mut i32) (i32.const -1))')
    L.append('(func $raise (param i32) (local.get 0) (throw $ti))')
    L.append('(type $iv (func (param i32)))')
    L.append('(type $chain (func (param i32) (result i64)))')

    def fold(op, terms):
        out = terms[0]
        for t in terms[1:]:
            out = "(%s %s %s)" % (op, out, t)
        return out

    def frame(k, prefix):
        """Locals for one link, and the i64 they reduce to.

        Three shapes in rotation: integers, doubles and vectors.  Which
        callee-saved registers a frame holds -- and therefore which of them a
        walk past it must put back -- is decided by this and nothing else, so
        a chain of one shape would be walked correctly by an unwinder that
        restored the same set every time.
        """
        n = "%s%d_" % (prefix, k)
        if k % 3 == 0:
            d = " ".join("(local $%s%d i64)" % (n, i) for i in range(8))
            s = " ".join('(local.set $%s%d (i64.mul (i64.extend_i32_s '
                         '(local.get $n)) (i64.const %d)))' % (n, i, k * 8 + i + 3)
                         for i in range(8))
            r = fold("i64.add", ["(local.get $%s%d)" % (n, i) for i in range(8)])
        elif k % 3 == 1:
            d = " ".join("(local $%s%d f64)" % (n, i) for i in range(6))
            s = " ".join('(local.set $%s%d (f64.add (f64.convert_i32_s '
                         '(local.get $n)) (f64.const %s)))'
                         % (n, i, f64lit(k * 6 + i + 0.5)) for i in range(6))
            r = "(i64.trunc_f64_s %s)" % fold(
                "f64.add", ["(local.get $%s%d)" % (n, i) for i in range(6)])
        else:
            d = " ".join("(local $%s%d v128)" % (n, i) for i in range(4))
            s = " ".join('(local.set $%s%d (i32x4.splat (i32.add '
                         '(local.get $n) (i32.const %d))))' % (n, i, k * 4 + i)
                         for i in range(4))
            r = "(i64.extend_i32_s (i32x4.extract_lane 2 %s))" % fold(
                "i32x4.add", ["(local.get $%s%d)" % (n, i) for i in range(4)])
        return d, s, r

    NW = 16
    for k in range(NW):
        d, s, r = frame(k, "w")
        down = ("" if k == NW - 1 else
                " (drop (call $w%d (local.get $n)))" % (k + 1))
        L.append('(func $w%d (param $n i32) (result i64) %s %s'
                 ' (if (i32.eq (local.get $n) (i32.const %d))'
                 '   (then (call $raise (i32.const %d))))%s'
                 ' %s)' % (k, d, s, k, 1000 + k, down, r))
    for k in range(NW):
        L.append('(func (export "wk_%d") (result i64) (local $keep i64)'
                 ' (local.set $keep (i64.const 0x51e7%04x00c0ffee))'
                 ' (block $h (result i32)'
                 '   (try_table (catch $ti $h) (drop (call $w0 (i32.const %d))))'
                 '   (i32.const -1))'
                 ' (i64.add (i64.extend_i32_u) (local.get $keep)))'
                 % (k, k, k))

    L.append('(func (export "wk_all") (result i64) (local $keep i64)'
             ' (local.set $keep (i64.const 0x0123456789abcdef))'
             ' (block $h'
             '   (try_table (catch_all $h) (drop (call $w0 (i32.const 13))))'
             '   (return (i64.const -1)))'
             ' (local.get $keep))')

    NM, HANDLERS = 12, (3, 7, 10)
    for k in range(NM):
        d, s, r = frame(k, "m")
        if k == NM - 1:
            body = ' %s' % r
        elif k in HANDLERS:
            body = (' (local.set $got (block $hh (result i32)'
                    '   (try_table (result i32) (catch $ti $hh)'
                    '     (drop (call $m%d (local.get $n))) (i32.const 0))))'
                    ' (if (local.get $got)'
                    '   (then (global.set $who (i32.const %d))))'
                    ' (i64.add %s (i64.extend_i32_u (local.get $got)))'
                    % (k + 1, k, r))
        else:
            body = ' (i64.add %s (call $m%d (local.get $n)))' % (r, k + 1)
        extra = ' (local $got i32)' if k in HANDLERS and k != NM - 1 else ''
        L.append('(func $m%d (param $n i32) (result i64) %s%s %s'
                 ' (if (i32.eq (local.get $n) (i32.const %d))'
                 '   (then (call $raise (i32.const %d))))'
                 '%s)' % (k, d, extra, s, k, 2000 + k, body))
    for k in range(NM):

        L.append('(func (export "mw_%d") (result i64)'
                 ' (local $got i32) (local $sum i64)'
                 ' (global.set $who (i32.const 99))'
                 ' (local.set $got (block $h (result i32)'
                 '   (try_table (result i32) (catch $ti $h)'
                 '     (local.set $sum (call $m0 (i32.const %d))) (i32.const 0))))'
                 ' (if (local.get $got) (then (global.set $who (i32.const 90))))'
                 ' (i64.add (i64.mul (i64.extend_i32_s (global.get $who))'
                 '                   (i64.const 100000))'
                 '   (i64.add (i64.extend_i32_u (local.get $got))'
                 '            (local.get $sum))))' % (k, k))

    L.append('(global $acc (mut i64) (i64.const 0))')
    L.append('(func $hop0 (param $n i32) (result i64)'
             ' (call $raise (local.get $n)) (i64.const 0))')
    for k in range(1, 4):
        L.append('(func $hop%d (param $n i32) (result i64) (local $x exnref)'
                 ' (local $a i64) (local $b i64)'
                 ' (local.set $a (i64.const %d)) (local.set $b (i64.const %d))'
                 ' (local.set $x (block $c (result exnref)'
                 '   (try_table (result exnref) (catch_all_ref $c)'
                 '     (drop (call $hop%d (local.get $n))) (unreachable))))'
                 ' (global.set $acc (i64.add (global.get $acc)'
                 '   (i64.add (local.get $a) (local.get $b))))'
                 ' (throw_ref (local.get $x)) (i64.const -1))'
                 % (k, 7 << (k * 8), 11 << (k * 8), k - 1))
    L.append('(func (export "hop_three") (result i64) (local $keep i64)'
             ' (global.set $acc (i64.const 0))'
             ' (local.set $keep (i64.const 0x1122334455667788))'
             ' (block $h (result i32)'
             '   (try_table (catch $ti $h) (drop (call $hop3 (i32.const 77))))'
             '   (i32.const -1))'
             ' (i64.add (i64.extend_i32_u)'
             '   (i64.add (local.get $keep) (global.get $acc))))')

    NT = 12
    for k in range(NT):
        d, s, r = frame(k, "t")
        if k == NT - 1:
            body = ' %s' % r
        elif k % 2:
            body = ' (return_call $t%d (local.get $n))' % (k + 1)
        else:
            body = ' (i64.add %s (call $t%d (local.get $n)))' % (r, k + 1)
        L.append('(func $t%d (param $n i32) (result i64) %s %s'
                 ' (if (i32.eq (local.get $n) (i32.const %d))'
                 '   (then (call $raise (i32.const %d))))'
                 '%s)' % (k, d, s, k, 3000 + k, body))
    for k in (2, 5, 11):
        L.append('(func (export "tail_%d") (result i64) (local $keep i64)'
                 ' (local.set $keep (i64.const 0x7ea1%04x0000beef))'
                 ' (block $h (result i32)'
                 '   (try_table (catch $ti $h) (drop (call $t0 (i32.const %d))))'
                 '   (i32.const -1))'
                 ' (i64.add (i64.extend_i32_u) (local.get $keep)))' % (k, k, k))

    L.append('(table 6 funcref)')
    NK = 6
    L.append('(elem (i32.const 0) %s)'
             % " ".join("$k%d" % i for i in range(NK)))
    L.append('(elem declare func %s)' % " ".join("$k%d" % i for i in range(NK)))
    for k in range(NK):
        d, s, r = frame(k, "k")
        if k == NK - 1:
            body = ' %s' % r
        elif k % 3 == 0:
            body = ' (i64.add %s (call $k%d (local.get $n)))' % (r, k + 1)
        elif k % 3 == 1:
            body = (' (i64.add %s (call_indirect (type $chain)'
                    ' (local.get $n) (i32.const %d)))' % (r, k + 1))
        else:
            body = (' (i64.add %s (call_ref $chain (local.get $n)'
                    ' (ref.func $k%d)))' % (r, k + 1))
        L.append('(func $k%d (param $n i32) (result i64) %s %s'
                 ' (if (i32.eq (local.get $n) (i32.const %d))'
                 '   (then (call $raise (i32.const %d))))'
                 '%s)' % (k, d, s, k, 4000 + k, body))
    for k in (1, 3, 5):
        L.append('(func (export "kind_%d") (result i64) (local $keep i64)'
                 ' (local.set $keep (i64.const 0x%04x0000cafe0000))'
                 ' (block $h (result i32)'
                 '   (try_table (catch $ti $h) (drop (call $k0 (i32.const %d))))'
                 '   (i32.const -1))'
                 ' (i64.add (i64.extend_i32_u) (local.get $keep)))' % (k, k, k))

    nwide = 500
    L.append('(func $thick_loop (param $n i32) (result i64)'
             ' (local $i i32) (local $j i32) (local $acc i64)'
             ' (loop $a'
             '   (local.set $j (i32.const 0))'
             '   (loop $b'
             '     (loop $c'
             '       (if (i32.eq (i32.add (local.get $i) (local.get $j))'
             '                   (local.get $n))'
             '         (then (call $raise (i32.const 5555))))'
             '       (local.set $acc (i64.add (local.get $acc) (i64.const 1))))'
             '     (local.set $j (i32.add (local.get $j) (i32.const 1)))'
             '     (br_if $b (i32.lt_s (local.get $j) (i32.const 3))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $a (i32.lt_s (local.get $i) (i32.const 3))))'
             ' (local.get $acc))')
    ended = "".join(
        ' (drop (block $e%d (result i32)'
        '   (try_table (result i32) (catch $other $e%d) (i32.const %d))))'
        % (i, i, i) for i in range(8))
    L.append('(func $thick_pads (param $n i32) (result i64)%s'
             ' (drop (call $thick_loop (local.get $n)))'
             ' (i64.const 12345))' % ended)
    L.append('(func $thick_wide (param $n i32) (result i64) (local %s)'
             ' %s'
             ' (drop (call $thick_pads (local.get $n)))'
             ' %s)'
             % (" ".join(["i64"] * nwide),
                " ".join("(local.set %d (i64.const %d))" % (i + 1, i * 5 + 1)
                         for i in range(nwide - 1)),
                "(local.get 1) " + " ".join("(local.get %d) (i64.add)" % (i + 1)
                                            for i in range(1, nwide - 1))))
    L.append('(func (export "thick_walk") (result i64) (local $keep i64)'
             ' (local.set $keep (i64.const 0x00ff00ff00ff00ff))'
             ' (block $h (result i32)'
             '   (try_table (catch $ti $h) (drop (call $thick_wide (i32.const 4))))'
             '   (i32.const -1))'
             ' (i64.add (i64.extend_i32_u) (local.get $keep)))')

    L.append('(func (export "walk_twice") (result i64) (local $keep i64)'
             ' (local.set $keep (i64.const 0x00c0ffee0badcafe))'
             ' (i64.add (local.get $keep)'
             '   (i64.add'
             '     (i64.extend_i32_u (block $h1 (result i32)'
             '       (try_table (catch $ti $h1) (drop (call $w0 (i32.const 9))))'
             '       (i32.const -1)))'
             '     (i64.extend_i32_u (block $h2 (result i32)'
             '       (try_table (catch $ti $h2) (drop (call $w0 (i32.const 4))))'
             '       (i32.const -1))))))')
    return "".join("  " + x + "\n" for x in L)

def gen_exn_edge():
    """Everything else that meets an exceptional edge.

    The other four exception modules ask about the exception: its frame
    shapes, its clause, its object, its walk.  This one asks about the *rest
    of the compiler* at the point where one arrives -- the signature the frame
    was built for, the merge the pad performs, the passes that still run for a
    function with pads, and the parts of the instance a handler goes on using.
    Everything here was written by hunting rather than by feature, so the
    groups are the places a wrong answer could hide:

      sig_*     signatures that put things somewhere unusual while a pad is
                present: parameters past the eighth of their class, several
                results, references, a vector
      pad_*     a pad parameter of every type there is, merged at up to eight
                call sites, including one that is still its default and one
                that is dead in the handler
      near_*    what the reduced pipeline may do beside a region -- a bounds
                check proven on one side of the edge and used on the other, a
                constant folded across it, a region that is all dead code, a
                handler whose value nobody wants
      thru_*    the instance a handler goes on using: three memories, bulk
                operations, vectors through memory, a memory the region grew
      inl_*     the inliner, which must take a callee that throws and refuse
                one that catches
      unusual_* a throw where the edge is taken with operands half evaluated,
                and at the two ends of a function

    Two shapes are missing from here and cannot be added: `wasm-interp` 1.0.41
    aborts on a `return_call` into a function that catches, so that one is a
    self-checking case in `cases_traps`, and it cannot run a `try_table` that
    takes operands at all.
    """
    L = []
    L.append('(memory $m0 1 40)')
    L.append('(memory $m1 2)')
    L.append('(memory $w i64 1)')
    L.append('(data $d "abcdefgh")')
    L.append('(tag $ei (param i32))')
    L.append('(tag $eo (param i32))')
    L.append('(global $g (mut i32) (i32.const 0))')
    L.append('(type $u (func (param i32) (result i32)))')
    L.append('(func $ti (param i32) (local.get 0) (throw $ei))')
    L.append('(func $tiu (param i32) (result i32) (local.get 0) (throw $ei))')
    L.append('(func $sq (param i32) (result i32) (i32.mul (local.get 0) (local.get 0)))')
    L.append('(func $cu (param i32) (result i32) (i32.mul (local.get 0) (i32.const 3)))')
    L.append('(elem declare func $sq $cu $tiu)')
    L.append('(table 2 funcref)')
    L.append('(elem (i32.const 0) $tiu $sq)')

    L.append('(func $maybe (param $n i32)'
             ' (if (i32.eqz (local.get $n)) (then (call $ti (i32.const 5)))))')

    def fold(op, terms):
        out = terms[0]
        for t in terms[1:]:
            out = "(%s %s %s)" % (op, out, t)
        return out

    L.append('(func $many_i (param %s) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (local.get 11)))'
             '   (return (i32.const -1)))'
             ' (drop) %s)'
             % (" ".join(["i32"] * 12),
                fold("i32.add", ["(local.get %d)" % i for i in range(12)])))
    L.append('(func (export "sig_many_i") (result i32) (call $many_i %s))'
             % " ".join("(i32.const %d)" % (i + 1) for i in range(12)))
    L.append('(func $many_f (param %s) (result f64)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 3)))'
             '   (return (f64.const 0)))'
             ' (drop) %s)'
             % (" ".join(["f64"] * 10),
                fold("f64.add", ["(local.get %d)" % i for i in range(10)])))
    L.append('(func (export "sig_many_f") (result i64)'
             ' (i64.reinterpret_f64 (call $many_f %s)))'
             % " ".join("(f64.const %s)" % f64lit(i + 1.5) for i in range(10)))

    L.append('(func $five (param $n i32) (result i32 i32 i32 i32 i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (local.get $n)))'
             '   (return (i32.const -1) (i32.const -1) (i32.const -1)'
             '           (i32.const -1) (i32.const -1)))'
             ' (local.set $n) (local.get $n) %s)'
             % " ".join("(i32.add (local.get $n) (i32.const %d))" % i
                        for i in range(1, 5)))
    L.append('(func (export "sig_five_results") (result i32)'
             ' (call $five (i32.const 20)) (i32.add) (i32.add) (i32.add) (i32.add))')

    L.append('(func $mv (result i64 v128 f32) (call $ti (i32.const 8))'
             ' (i64.const 0) (v128.const i32x4 0 0 0 0) (f32.const 0))')
    L.append('(func (export "sig_mv_thrower") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $mv) (drop) (drop) (drop))'
             '   (return (i32.const -1)))'
             ' (i32.add (i32.const 500)))')
    L.append('(func $refs (param $f funcref) (param $x externref) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 9)))'
             '   (return (i32.const -1)))'
             ' (drop)'
             ' (i32.add (i32.mul (i32.const 10) (i32.eqz (ref.is_null (local.get $f))))'
             '          (ref.is_null (local.get $x))))')
    L.append('(func (export "sig_ref_params") (result i32)'
             ' (call $refs (ref.func $sq) (ref.null extern)))')
    L.append('(func (export "sig_v128_result") (result v128) (local $q v128)'
             ' (local.set $q (v128.const i32x4 9 8 7 6))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 4)))'
             '   (return (v128.const i32x4 0 0 0 0)))'
             ' (i32x4.splat) (i32x4.add (local.get $q)))')

    L.append('(func (export "pad_typed_ref") (result i32) (local $f (ref null $u))'
             ' (local.set $f (ref.func $sq))'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (call $maybe (i32.const 1))'
             '     (local.set $f (ref.func $cu))'
             '     (call $maybe (i32.const 0))'
             '     (local.set $f (ref.null $u))))'
             ' (call_ref $u (i32.const 7) (local.get $f)))')
    L.append('(func $catch1 (param $n i32) (result exnref)'
             ' (block $c (result exnref)'
             '   (try_table (result exnref) (catch_all_ref $c)'
             '     (call $ti (local.get $n)) (unreachable))))')
    L.append('(func $payload1 (param $x exnref) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (throw_ref (local.get $x)))'
             '   (i32.const -1)))')
    L.append('(func (export "pad_exnref") (result i32) (local $x exnref)'
             ' (local.set $x (call $catch1 (i32.const 41)))'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (call $maybe (i32.const 1))'
             '     (local.set $x (call $catch1 (i32.const 42)))'
             '     (call $maybe (i32.const 0))'
             '     (local.set $x (call $catch1 (i32.const 43)))))'
             ' (call $payload1 (local.get $x)))')

    L.append('(func (export "pad_default") (result i64) (local $a i64) (local $b f64)'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (call $maybe (i32.const 0))'
             '     (local.set $a (i64.const 99))'
             '     (local.set $b (f64.const %s))))'
             ' (i64.add (local.get $a) (i64.trunc_f64_s (local.get $b))))'
             % f64lit(9.5))
    L.append('(func (export "pad_tee") (result i32) (local $a i32) (local $b i32)'
             ' (local.set $a (i32.const 1))'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (local.set $b (i32.add (local.tee $a (i32.const 10)) (i32.const 1)))'
             '     (call $maybe (i32.const 1))'
             '     (local.set $b (i32.add (local.tee $a (i32.const 20)) (i32.const 2)))'
             '     (call $maybe (i32.const 0))'
             '     (local.set $a (i32.const 30))))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 100)) (local.get $b)))')

    sites = "".join(
        ' (local.set $i (i32.const %d)) (local.set $l (i64.const %d))'
        ' (local.set $f (f32.const %s)) (local.set $d (f64.const %s))'
        ' (local.set $v (v128.const i32x4 %d %d %d %d))'
        ' (call $maybe (i32.const %d))'
        % (k, k * 7, f32lit(k + 0.5), f64lit(k + 0.25), k, k, k, k,
           0 if k == 6 else 1)
        for k in range(8))
    L.append('(func (export "pad_eight_sites") (result i64)'
             ' (local $i i32) (local $l i64) (local $f f32) (local $d f64)'
             ' (local $v v128)'
             ' (block $h (try_table (catch_all $h)%s))'
             ' (i64.add (i64.extend_i32_s (local.get $i))'
             '   (i64.add (local.get $l)'
             '     (i64.add (i64.trunc_f32_s (local.get $f))'
             '       (i64.add (i64.trunc_f64_s (local.get $d))'
             '         (i64.extend_i32_s'
             '           (i32x4.extract_lane 1 (local.get $v))))))))' % sites)

    L.append('(func (export "pad_dead_param") (result i32)'
             ' (local $a i32) (local $b i32)'
             ' (local.set $a (i32.const 1))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (local.set $a (i32.const 2))'
             '     (call $maybe (i32.const 1))'
             '     (local.set $a (i32.const 3))'
             '     (local.set $b (i32.const 4))'
             '     (call $ti (i32.const 55))'
             '     (i32.const -1)))'
             ' (i32.add (local.get $b)))')

    L.append('(func (export "near_bce") (result i32) (local $p i32)'
             ' (local.set $p (i32.and (i32.load $m0 (i32.const 0)) (i32.const 0x3f)))'
             ' (i32.store $m0 (local.get $p) (i32.const 11))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (i32.store $m0 (i32.add (local.get $p) (i32.const 8)) (i32.const 22))'
             '     (call $maybe (i32.const 0))'
             '     (i32.store $m0 (i32.add (local.get $p) (i32.const 16)) (i32.const 33)))'
             '   (return (i32.const -1)))'
             ' (drop)'
             ' (i32.add (i32.load $m0 (local.get $p))'
             '   (i32.add (i32.load $m0 (i32.add (local.get $p) (i32.const 8)))'
             '            (i32.load $m0 (i32.add (local.get $p) (i32.const 16))))))')
    L.append('(func (export "near_fold") (result i32) (local $a i32)'
             ' (local.set $a (i32.mul (i32.const 6) (i32.const 7)))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (call $maybe (i32.const 0))'
             '     (local.set $a (i32.add (local.get $a) (i32.const 1))))'
             '   (return (i32.const -1)))'
             ' (drop) (local.get $a))')
    L.append('(func (export "near_dead_region") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (call $ti (i32.const 71)) (call $ti (i32.const 72))'
             '     (call $ti (i32.const 73)))'
             '   (return (i32.const -1))))')
    L.append('(func (export "near_unreachable_after") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 74)))'
             '   (unreachable)))')
    L.append('(func (export "near_value_dead") (result i32)'
             ' (global.set $g (i32.const 1))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $ti (i32.const 75)))'
             '   (return (i32.const -1)))'
             ' (drop) (global.set $g (i32.const 2)) (global.get $g))')
    L.append('(func (export "near_loop") (result i32) (local $i i32) (local $s i32)'
             ' (loop $l'
             '   (local.set $s (i32.add (local.get $s)'
             '     (block $h (result i32)'
             '       (try_table (result i32) (catch $ei $h)'
             '         (local.set $s (i32.add (local.get $s) (i32.const 1000)))'
             '         (call $maybe (i32.and (local.get $i) (i32.const 1)))'
             '         (i32.const 7)))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $l (i32.lt_s (local.get $i) (i32.const 6))))'
             ' (local.get $s))')

    L.append('(func (export "near_siblings") (result i32) (local $i i32) (local $s i32)'
             ' (loop $l'
             '   (if (i32.and (local.get $i) (i32.const 1))'
             '     (then (local.set $s (i32.add (local.get $s) (i32.mul (i32.const 10)'
             '       (block $h1 (result i32)'
             '         (try_table (result i32) (catch $ei $h1)'
             '           (call $ti (local.get $i)) (i32.const -1)))))))'
             '     (else (local.set $s (i32.add (local.get $s)'
             '       (block $h2 (result i32)'
             '         (try_table (result i32) (catch $ei $h2)'
             '           (call $ti (local.get $i)) (i32.const -1)))))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $l (i32.lt_s (local.get $i) (i32.const 4))))'
             ' (local.get $s))')

    L.append('(func $sqm (param $n i32) (param $bad i32) (result i32)'
             ' (if (i32.eq (local.get $n) (local.get $bad))'
             '   (then (call $ti (i32.const 91))))'
             ' (i32.add (i32.mul (local.get $n) (local.get $n)) (i32.const 1)))')

    L.append('(func $inl_loop (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (local.set $s (i32.add (local.get $s)'
             '         (call $sqm (local.get $i) (local.get $bad))))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 8))))'
             '     (i32.mul (local.get $s) (i32.const 2)))'
             '   (return))'
             ' (i32.add (i32.mul (local.get $s) (i32.const 1000))))')
    L.append('(func (export "pipe_inl_loop") (result i32)'
             ' (call $inl_loop (i32.const 99)))')
    L.append('(func (export "pipe_inl_loop_raise") (result i32)'
             ' (call $inl_loop (i32.const 3)))')

    L.append('(func $licm_loop (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (i32.store $m0 (i32.const 1024) (i32.const 5))'
             ' (global.set $g (i32.const 3))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (local.set $s (i32.add (local.get $s)'
             '         (i32.mul (i32.add (global.get $g)'
             '                           (i32.load $m0 (i32.const 1024)))'
             '                  (call $sqm (local.get $i) (local.get $bad)))))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 6))))'
             '     (local.get $s))'
             '   (return))'
             ' (i32.add (i32.mul (local.get $s) (i32.const 1000))))')
    L.append('(func (export "pipe_licm") (result i32) (call $licm_loop (i32.const 99)))')
    L.append('(func (export "pipe_licm_raise") (result i32) (call $licm_loop (i32.const 4)))')

    L.append('(func $licm_after_call (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (local.set $s (call $sqm (i32.const 2) (local.get $bad)))'
             '     (loop $l'
             '       (local.set $s (i32.add (local.get $s)'
             '         (i32.add (global.get $g) (i32.load $m0 (i32.const 1024)))))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 5))))'
             '     (local.get $s))'
             '   (return))'
             ' (i32.add (i32.mul (local.get $s) (i32.const 1000))))')
    L.append('(func (export "pipe_licm_preheader") (result i32)'
             ' (call $licm_after_call (i32.const 99)))')
    L.append('(func (export "pipe_licm_preheader_raise") (result i32)'
             ' (call $licm_after_call (i32.const 2)))')

    L.append('(func $walk (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (memory.fill $m0 (i32.const 1152) (i32.const 0) (i32.const 64))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (i32.store $m0 (i32.add (i32.const 1152)'
             '                               (i32.mul (local.get $i) (i32.const 4)))'
             '         (call $sqm (local.get $i) (local.get $bad)))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 8))))'
             '     (i32.const 0))'
             '   (return))'
             ' (drop)'
             ' (local.set $i (i32.const 0))'
             ' (loop $r'
             '   (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))'
             '     (i32.load $m0 (i32.add (i32.const 1152)'
             '                            (i32.mul (local.get $i) (i32.const 4))))))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $r (i32.lt_s (local.get $i) (i32.const 8))))'
             ' (local.get $s))')
    L.append('(func (export "pipe_walk_raise") (result i32) (call $walk (i32.const 5)))')

    L.append('(func $vecloop (param $bad i32) (result i32) (local $i i32)'
             ' (memory.fill $m0 (i32.const 1280) (i32.const 1) (i32.const 128))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (i32.store $m0 (i32.add (i32.const 1280)'
             '                               (i32.mul (local.get $i) (i32.const 4)))'
             '         (i32.mul (local.get $i) (i32.const 7)))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_u (local.get $i) (i32.const 32))))'
             '     (call $sqm (i32.const 1) (local.get $bad)))'
             '   (return))'
             ' (i32.add (i32.load $m0 (i32.const 1360))))')
    L.append('(func (export "pipe_vec") (result i32) (call $vecloop (i32.const 99)))')
    L.append('(func (export "pipe_vec_raise") (result i32) (call $vecloop (i32.const 1)))')

    L.append('(func $writes_then_raises (param $bad i32)'
             ' (i32.store $m0 (i32.const 1088) (i32.const 77))'
             ' (if (i32.eqz (local.get $bad)) (then (call $ti (i32.const 92)))))')
    L.append('(func $mem_pad (param $bad i32) (result i32) (local $i i32) (local $s i32)'
             ' (i32.store $m0 (i32.const 1088) (i32.const 11))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (drop (i32.load $m0 (i32.const 1088)))'
             '       (call $writes_then_raises'
             '         (i32.sub (local.get $i) (local.get $bad)))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 4))))'
             '     (i32.const 0))'
             '   (return))'
             ' (drop)'
             ' (i32.add (i32.mul (i32.load $m0 (i32.const 1088)) (i32.const 100))'
             '          (local.get $i)))')
    L.append('(func (export "pipe_mem_pad") (result i32) (call $mem_pad (i32.const 99)))')
    L.append('(func (export "pipe_mem_pad_raise") (result i32) (call $mem_pad (i32.const 2)))')

    L.append('(func $empty_pad (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (block $h'
             '   (try_table (catch_all $h)'
             '     (loop $l'
             '       (local.set $s (i32.add (local.get $s)'
             '         (call $sqm (local.get $i) (local.get $bad))))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 5)))))'
             '   (return (local.get $s)))'
             ' (i32.add (local.get $s) (i32.const 7000)))')
    L.append('(func (export "pipe_empty_pad") (result i32)'
             ' (call $empty_pad (i32.const 99)))')
    L.append('(func (export "pipe_empty_pad_raise") (result i32)'
             ' (call $empty_pad (i32.const 3)))')

    L.append('(func $two_calls (param $n i32) (param $bad i32) (result i32)'
             ' (i32.add (i32.mul (call $sqm (local.get $n) (i32.const 99))'
             '                   (i32.const 3))'
             '          (call $sqm (i32.add (local.get $n) (i32.const 1))'
             '                     (local.get $bad))))')
    L.append('(func $split_body (param $bad i32) (result i32)'
             ' (local $i i32) (local $s i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (loop $l'
             '       (local.set $s (i32.add (local.get $s)'
             '         (call $two_calls (local.get $i) (local.get $bad))))'
             '       (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '       (br_if $l (i32.lt_s (local.get $i) (i32.const 4))))'
             '     (local.get $s))'
             '   (return))'
             ' (i32.add (i32.mul (local.get $s) (i32.const 1000))))')
    L.append('(func (export "pipe_split_body") (result i32)'
             ' (call $split_body (i32.const 99)))')
    L.append('(func (export "pipe_split_body_raise") (result i32)'
             ' (call $split_body (i32.const 3)))')

    L.append('(func $before (param $n i32) (result i32)'
             ' (i32.add (i32.mul (local.get $n) (i32.const 5)) (i32.const 2)))')
    L.append('(func (export "pipe_call_before_region") (result i32) (local $a i32)'
             ' (local.set $a (call $before (i32.const 4)))'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h)'
             '     (call $sqm (i32.const 1) (i32.const 1)))'
             '   (return))'
             ' (i32.add (local.get $a)))')

    L.append('(func $mem_throw'
             ' (i32.store $m1 (i32.const 100) (i32.const 7))'
             ' (i64.store $w (i64.const 8) (i64.const 9))'
             ' (call $ti (i32.const 82)))')
    L.append('(func (export "thru_memories") (result i64)'
             ' (i32.store $m0 (i32.const 512) (i32.const 5))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $mem_throw))'
             '   (return (i64.const -1)))'
             ' (drop)'
             ' (i64.add (i64.extend_i32_u (i32.add (i32.load $m0 (i32.const 512))'
             '                                     (i32.load $m1 (i32.const 100))))'
             '          (i64.load $w (i64.const 8))))')
    L.append('(func (export "thru_bulk") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h)'
             '     (memory.init $m0 $d (i32.const 0) (i32.const 0) (i32.const 8))'
             '     (call $ti (i32.const 83)))'
             '   (return (i32.const -1)))'
             ' (drop)'
             ' (memory.copy $m0 $m0 (i32.const 64) (i32.const 0) (i32.const 8))'
             ' (memory.fill $m0 (i32.const 128) (i32.const 3) (i32.const 4))'
             ' (i32.add (i32.load8_u $m0 (i32.const 66))'
             '          (i32.load8_u $m0 (i32.const 129))))')
    L.append('(func $vec_throw'
             ' (v128.store $m0 (i32.const 256) (v128.const i32x4 1 2 3 4))'
             ' (call $ti (i32.const 84)))')
    L.append('(func (export "thru_v128") (result v128) (local $q v128)'
             ' (local.set $q (v128.const i32x4 10 20 30 40))'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $vec_throw))'
             '   (return (v128.const i32x4 0 0 0 0)))'
             ' (drop) (i32x4.add (local.get $q) (v128.load $m0 (i32.const 256))))')

    L.append('(func $grow_throw (drop (memory.grow $m1 (i32.const 1)))'
             ' (call $ti (i32.const 85)))')
    L.append('(func (export "thru_grow") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (catch $ei $h) (call $grow_throw))'
             '   (return (i32.const -1)))'
             ' (drop)'
             ' (i32.add (i32.mul (memory.size $m1) (i32.const 10))'
             '          (memory.size $m0)))')

    L.append('(func $tiny (param $n i32) (result i32)'
             ' (if (i32.eq (local.get $n) (i32.const 3))'
             '   (then (call $ti (i32.const 81))))'
             ' (i32.mul (local.get $n) (local.get $n)))')
    L.append('(func $host (param $n i32) (result i32) (local $k i32)'
             ' (local.set $k (i32.add (call $tiny (local.get $n)) (i32.const 1)))'
             ' (i32.add (local.get $k)'
             '          (call $tiny (i32.add (local.get $n) (i32.const 1)))))')
    L.append('(func (export "inl_throwing_callee") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $host (i32.const 2)))'
             '   (return))'
             ' (i32.add (i32.const 100)))')
    L.append('(func $catcher (param $n i32) (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $ti (local.get $n))'
             '     (i32.const -1))'
             '   (return))'
             ' (i32.add (i32.const 200)))')
    L.append('(func (export "inl_catching_callee") (result i32)'
             ' (i32.add (call $catcher (i32.const 1)) (call $catcher (i32.const 2))))')

    for name, expr, base in (
            ("unusual_in_add",
             "(i32.add (i32.const 100) (call $tiu (i32.const 21)))", 3000),
            ("unusual_mid_args",
             "(call $three (i32.const 1) (call $tiu (i32.const 22)) (i32.const 3))",
             4000),
            ("unusual_in_select",
             "(select (i32.const 5) (call $tiu (i32.const 23)) (i32.const 0))",
             5000),
            ("unusual_in_address",
             "(i32.load $m0 (call $tiu (i32.const 25)))", 7000)):
        L.append('(func (export "%s") (result i32)'
                 ' (block $h (result i32)'
                 '   (try_table (result i32) (catch $ei $h) %s)'
                 '   (return))'
                 ' (i32.add (i32.const %d)))' % (name, expr, base))
    L.append('(func $three (param i32 i32 i32) (result i32)'
             ' (i32.add (i32.add (local.get 0) (local.get 1)) (local.get 2)))')
    L.append('(func (export "unusual_in_brif") (result i32)'
             ' (block $h (result i32)'
             '   (block $x'
             '     (try_table (result i32) (catch $ei $h)'
             '       (br_if $x (call $tiu (i32.const 24))) (i32.const -1))'
             '     (return))'
             '   (return (i32.const -2)))'
             ' (i32.add (i32.const 6000)))')

    L.append('(func $last (param $n i32) (result i32)'
             ' (drop (i32.mul (local.get $n) (local.get $n)))'
             ' (local.get $n) (throw $ei))')
    L.append('(func (export "unusual_throw_last") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $last (i32.const 31)))'
             '   (return))'
             ' (i32.add (i32.const 8000)))')
    L.append('(func $whole (param $n i32) (result i32)'
             ' (try_table (catch $ei 0) (call $ti (local.get $n)))'
             ' (i32.const -1))')
    L.append('(func (export "unusual_whole_body") (result i32)'
             ' (i32.add (i32.const 10000) (call $whole (i32.const 33))))')

    for nm, ins in (("tail_direct", "(return_call $tiu (local.get $n))"),
                    ("tail_indirect",
                     "(return_call_indirect (type $u) (local.get $n) (i32.const 0))"),
                    ("tail_ref",
                     "(return_call_ref $u (local.get $n) (ref.func $tiu))")):
        L.append('(func $%s_inner (param $n i32) (result i32)'
                 ' (block $h (result i32)'
                 '   (try_table (result i32) (catch $ei $h) %s)'
                 '   (return)))' % (nm, ins))
        L.append('(func (export "unusual_%s") (result i32)'
                 ' (block $h (result i32)'
                 '   (try_table (result i32) (catch $ei $h)'
                 '     (call $%s_inner (i32.const 11)))'
                 '   (return))'
                 ' (i32.add (i32.const 1000)))' % (nm, nm))
    L.append('(func (export "unusual_tail_from_handler") (result i32)'
             ' (block $h (result i32)'
             '   (try_table (result i32) (catch $ei $h) (call $tiu (i32.const 12)))'
             '   (return))'
             ' (return_call $sq))')

    L.append('(func $rec (param $n i32) (result i32) (local $k i32)'
             ' (local.set $k (i32.mul (local.get $n) (i32.const 7)))'
             ' (if (i32.eqz (local.get $n)) (then (call $ti (i32.const 60))))'
             ' (i32.add (local.get $k)'
             '   (block $h (result i32)'
             '     (try_table (result i32) (catch $ei $h)'
             '       (call $rec (i32.sub (local.get $n) (i32.const 1))))'
             '     (return))))')
    L.append('(func (export "unusual_rec_region") (result i32)'
             ' (call $rec (i32.const 6)))')
    return "".join("  " + x + "\n" for x in L)

def gen_mutstate():
    """Reads of state that another instruction changes.

    `memory.size`, `table.size` and `table.get` are reads, not computations:
    a `grow` changes what the first two answer, every table write changes what
    the third does, and `table.get` faults out of bounds on top of that.  All
    three were `default: true` in `ir_is_pure`, which is the single question
    value numbering, the hoist, loop rotation and dead-code elimination all
    ask -- so a size read twice either side of a `grow` was read once, a
    `table.get` either side of a `table.set` was read once, a `memory.size`
    inside a loop that grows left the loop, and a `table.get` nobody used was
    deleted along with the trap it owed.  Every one of them is green at -O0.

    Not an exception test: it was found by an exception probe that read
    `memory.size` in a handler after the callee grew, and turned out to need
    no handler at all.
    """
    L = []
    L.append('(memory 1 40)')
    L.append('(type $u (func (param i32) (result i32)))')

    L.append('(table $t 2 (ref null $u))')
    L.append('(table $e 2 externref)')
    L.append('(func $f (param i32) (result i32) (i32.mul (local.get 0) (i32.const 3)))')
    L.append('(elem declare func $f)')

    L.append('(func (export "ms_grow") (result i32) (local $a i32)'
             ' (local.set $a (memory.size))'
             ' (drop (memory.grow (i32.const 2)))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 100)) (memory.size)))')
    L.append('(func $grow3 (drop (memory.grow (i32.const 3))))')
    L.append('(func (export "ms_grow_call") (result i32) (local $a i32)'
             ' (local.set $a (memory.size))'
             ' (call $grow3)'
             ' (i32.add (i32.mul (local.get $a) (i32.const 100)) (memory.size)))')

    L.append('(func (export "ms_grow_loop") (result i32) (local $i i32) (local $s i32)'
             ' (loop $l'
             '   (local.set $s (i32.add (local.get $s) (memory.size)))'
             '   (drop (memory.grow (i32.const 1)))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $l (i32.lt_s (local.get $i) (i32.const 4))))'
             ' (local.get $s))')
    L.append('(func (export "ts_grow") (result i32) (local $a i32)'
             ' (local.set $a (table.size $t))'
             ' (drop (table.grow $t (ref.null $u) (i32.const 3)))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 100)) (table.size $t)))')

    L.append('(func (export "tg_set") (result i32) (local $a i32)'
             ' (local.set $a (ref.is_null (table.get $t (i32.const 0))))'
             ' (table.set $t (i32.const 0) (ref.func $f))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 10))'
             '          (ref.is_null (table.get $t (i32.const 0)))))')
    L.append('(func (export "tg_copy") (result i32) (local $a i32)'
             ' (local.set $a (ref.is_null (table.get $t (i32.const 1))))'
             ' (table.copy $t $t (i32.const 1) (i32.const 0) (i32.const 1))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 10))'
             '          (ref.is_null (table.get $t (i32.const 1)))))')
    L.append('(func (export "tg_fill") (result i32) (local $a i32)'
             ' (table.set $e (i32.const 1) (ref.null extern))'
             ' (local.set $a (ref.is_null (table.get $e (i32.const 1))))'
             ' (table.fill $e (i32.const 0) (ref.null extern) (i32.const 2))'
             ' (i32.add (i32.mul (local.get $a) (i32.const 10))'
             '          (ref.is_null (table.get $e (i32.const 1)))))')
    L.append('(func (export "tg_grow") (result i32) (local $a i32)'
             ' (local.set $a (ref.is_null (table.get $t (i32.const 1))))'
             ' (drop (table.grow $t (ref.func $f) (i32.const 1)))'
             ' (i32.add (i32.add (i32.mul (local.get $a) (i32.const 10))'
             '                   (ref.is_null (table.get $t (i32.const 1))))'
             '          (i32.mul (i32.const 100) (table.size $t))))')

    L.append('(func (export "tg_loop") (result i32) (local $i i32) (local $s i32)'
             ' (table.set $t (i32.const 0) (ref.null $u))'
             ' (loop $l'
             '   (local.set $s (i32.add (local.get $s)'
             '     (ref.is_null (table.get $t (i32.const 0)))))'
             '   (table.set $t (i32.const 0) (ref.func $f))'
             '   (local.set $i (i32.add (local.get $i) (i32.const 1)))'
             '   (br_if $l (i32.lt_s (local.get $i) (i32.const 4))))'
             ' (local.get $s))')

    L.append('(func (export "tg_then_call") (result i32)'
             ' (table.set $t (i32.const 0) (ref.null $u))'
             ' (drop (ref.is_null (table.get $t (i32.const 0))))'
             ' (table.set $t (i32.const 0) (ref.func $f))'
             ' (call_ref $u (i32.const 5)'
             '   (table.get $t (i32.const 0))))')
    return "".join("  " + x + "\n" for x in L)

def gen_bswap():
    """A byte reverse written out longhand, which `rev` does in one.

    wasm has no byte-reverse instruction, so a producer writes the mask,
    rotate and or that LLVM emits.  What is under test is not that the shape
    computes a byte reversal -- it does by construction -- but that the
    *rewrite* fires only where the shape really is one.  So every case is
    paired with a near miss: the same expression with one operand of the mask,
    the rotate or the source changed, which must still compute whatever it
    computes.

    The 64-bit row is the important one.  The identity is a coincidence of
    four bytes -- rotating by eight moves a byte one place, which reverses
    four and does not reverse eight -- so the same expression at 64 bits with
    the mask widened is a different permutation, and a rewrite that read the
    width off the type would answer wrongly here and nowhere else.

    Every source comes out of linear memory, so nothing folds the expression
    away before the rule sees it.
    """
    out = ['(memory 1)\n',
           '(data (i32.const 0) "\\01\\23\\45\\67\\89\\ab\\cd\\ef'
           '\\fe\\dc\\ba\\98\\76\\54\\32\\10")\n']
    x = "(i32.load (i32.const 0))"
    y = "(i32.load (i32.const 4))"
    q = "(i64.load (i32.const 0))"
    M, M64 = "0x00ff00ff", "0x00ff00ff00ff00ff"

    def rot(w, v, k, left=False):
        return "(i%d.rot%s %s (i%d.const %d))" % (w, "l" if left else "r", v, w, k)

    def msk(w, v, m):
        return "(i%d.and %s (i%d.const %s))" % (w, v, w, m)

    cases = [

        ("bs32", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 8), msk(32, rot(32, x, 24), M))),
        ("bs32_swap", "(i32.or %s %s)" % (msk(32, rot(32, x, 24), M), rot(32, msk(32, x, M), 8))),
        ("bs32_xor", "(i32.xor %s %s)" % (rot(32, msk(32, x, M), 8), msk(32, rot(32, x, 24), M))),
        ("bs32_rotl", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 24, True),
                                          msk(32, rot(32, x, 8, True), M))),
        ("bs32_mixrot", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 8),
                                            msk(32, rot(32, x, 8, True), M))),

        ("bs32_mask", "(i32.or %s %s)" % (rot(32, msk(32, x, "0x00ff00fe"), 8),
                                          msk(32, rot(32, x, 24), M))),
        ("bs32_amt", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 9),
                                         msk(32, rot(32, x, 24), M))),
        ("bs32_amt2", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 8),
                                          msk(32, rot(32, x, 16), M))),
        ("bs32_srcs", "(i32.or %s %s)" % (rot(32, msk(32, x, M), 8),
                                          msk(32, rot(32, y, 24), M))),
        ("bs32_and", "(i32.and %s %s)" % (rot(32, msk(32, x, M), 8),
                                          msk(32, rot(32, x, 24), M))),

        ("bs64_not", "(i32.wrap_i64 (i64.or %s %s))"
                     % (rot(64, msk(64, q, M64), 8), msk(64, rot(64, q, 56), M64))),
        ("bs64_not_hi", "(i32.wrap_i64 (i64.shr_u (i64.or %s %s) (i64.const 32)))"
                        % (rot(64, msk(64, q, M64), 8), msk(64, rot(64, q, 56), M64))),
    ]
    for name, expr in cases:
        out.append('(func (export "%s") (result i32) %s)\n' % (name, expr))

    out.append('(func (export "rot_longhand") (result i32)\n'
               '  (i32.or (i32.shr_u %s (i32.const 7))\n'
               '          (i32.shl %s (i32.const 25))))\n' % (x, x))
    return "".join(out)

def gen_vector64():
    """The same counted loops over a memory whose index is an i64.

    A separate module rather than more exports in `vector`, because the thing
    under test is the index space and a module has one per memory.

    Two properties are new here and neither is visible in an answer that comes
    out right.  A 32-bit address has the guard reservation behind it, so the
    vectoriser only has to show that its advancing pointer agrees with the
    wasm address; past 2^32 nothing stands behind the memory, so it has to
    show the whole vector range is *inside* it.  And a byte written one lane
    beyond the loop's own range changes no answer -- so every kernel writes
    into a region the checksum reads eight words past the end of.

    Layout in a two-page memory: A at 0, B at 8192, C at 16384, D at 24576
    (the one with slack behind it), and the alias playground at 32768.  The
    last 4n bytes of the memory are what `kedge` walks, so its vector range
    ends exactly where the memory does.
    """
    A, Bs, C, D, AL = 0, 8192, 16384, 24576, 32768
    END = 2 * 65536
    out = ["(memory $w i64 2)\n(type $a3 (func (param i64 i64 i64)))\n"
           "(table 1 funcref)\n(elem (i32.const 0) $kalias)\n"]

    xs = """    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 13))))
    (local.set $x (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 17))))
    (local.set $x (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 5))))
"""
    out.append("""(func $fill (param $n i32) (local $i i64) (local $x i32) (local $lim i64)
  (local.set $lim (i64.extend_i32_u (i32.add (local.get $n) (i32.const 16))))
  (local.set $x (i32.const 12345))
  (block $d (loop $L
    (br_if $d (i64.gt_u (local.get $i) (local.get $lim)))
%s    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (local.get $x))
%s    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (local.get $x))
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (i32.const 0))
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (i32.const 0))
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (local.get $x))
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2)) (i32.const 0))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br $L))))
""" % (xs, A, xs, Bs, C, D, AL, AL + 4096))

    out.append("""(func $fillf (param $n i32) (local $i i64) (local $lim i64)
  (local.set $lim (i64.extend_i32_u (i32.add (local.get $n) (i32.const 16))))
  (block $d (loop $L
    (br_if $d (i64.gt_u (local.get $i) (local.get $lim)))
    (f64.store $w offset=%d (i64.shl (local.get $i) (i64.const 3))
      (f64.mul (f64.convert_i64_s (i64.sub (local.get $i) (i64.const 7)))
               (f64.const 0.5)))
    (f64.store $w offset=%d (i64.shl (local.get $i) (i64.const 3))
      (f64.add (f64.convert_i64_s (local.get $i)) (f64.const 1.25)))
    (f64.store $w offset=%d (i64.shl (local.get $i) (i64.const 3)) (f64.const 0))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br $L))))
""" % (A, Bs, C))

    out.append("""(func $chk (param $n i32) (param $base i64) (result i32)
  (local $i i64) (local $h i32) (local $lim i64)
  (local.set $lim (i64.extend_i32_u (local.get $n)))
  (local.set $h (i32.const 0x811c9dc5))
  (block $d (loop $L
    (br_if $d (i64.ge_u (local.get $i) (local.get $lim)))
    (local.set $h (i32.mul (i32.xor (local.get $h)
      (i32.load $w (i64.add (local.get $base) (i64.shl (local.get $i) (i64.const 2)))))
      (i32.const 16777619)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br $L)))
  (local.get $h))
(func $chkb (param $n i32) (param $base i64) (result i32)
  (local $i i64) (local $h i32) (local $lim i64)
  (local.set $lim (i64.extend_i32_u (local.get $n)))
  (local.set $h (i32.const 0x811c9dc5))
  (block $d (loop $L
    (br_if $d (i64.ge_u (local.get $i) (local.get $lim)))
    (local.set $h (i32.mul (i32.xor (local.get $h)
      (i32.load8_u $w (i64.add (local.get $base) (local.get $i))))
      (i32.const 16777619)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br $L)))
  (local.get $h))
""")

    kern = []

    kern.append("""(func $kadd (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2))
      (i32.add (i32.mul (i32.load $w offset=%d (i64.shl (local.get $i) (i64.const 2)))
                        (i32.const 3))
               (i32.load $w offset=%d (i64.shl (local.get $i) (i64.const 2)))))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""" % (D, A, Bs))

    kern.append("""(func $ksum (param $n i64) (result i32) (local $i i64) (local $s i32)
  (if (i64.eqz (local.get $n)) (then (return (local.get $s))))
  (loop $L
    (local.set $s (i32.add (local.get $s)
      (i32.load $w offset=%d (i64.shl (local.get $i) (i64.const 2)))))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n))))
  (local.get $s))
""" % A)

    kern.append("""(func $kbyte (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store8 $w offset=%d (local.get $i)
      (i32.xor (i32.load8_u $w offset=%d (local.get $i)) (i32.const 0x5a)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""" % (D, A))

    kern.append("""(func $kfmul (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (f64.store $w offset=%d (i64.shl (local.get $i) (i64.const 3))
      (f64.mul (f64.load $w offset=%d (i64.shl (local.get $i) (i64.const 3)))
               (f64.load $w offset=%d (i64.shl (local.get $i) (i64.const 3)))))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""" % (C, A, Bs))

    kern.append("""(func $kdown (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (local.set $i (local.get $n))
  (loop $L
    (local.set $i (i64.sub (local.get $i) (i64.const 1)))
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2))
      (i32.xor (i32.load $w offset=%d (i64.shl (local.get $i) (i64.const 2)))
               (i32.const 0x5a5a5a5a)))
    (br_if $L (i64.ne (local.get $i) (i64.const 0)))))
""" % (D, A))

    kern.append("""(func $kdep (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store $w offset=%d (i64.shl (local.get $i) (i64.const 2))
      (i32.add (i32.load $w offset=%d (i64.shl (local.get $i) (i64.const 2)))
               (i32.const 1)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""" % (D + 4, D))

    kern.append("""(func $kalias (param $d i64) (param $s i64) (param $n i64)
  (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store $w (i64.add (local.get $d) (i64.shl (local.get $i) (i64.const 2)))
      (i32.add (i32.mul (i32.load $w (i64.add (local.get $s)
                                              (i64.shl (local.get $i) (i64.const 2))))
                        (i32.const 3))
               (i32.const 1)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""")

    kern.append("""(func $kedge (param $base i64) (param $n i64) (local $i i64)
  (if (i64.eqz (local.get $n)) (then (return)))
  (loop $L
    (i32.store $w (i64.add (local.get $base) (i64.shl (local.get $i) (i64.const 2)))
      (i32.add (i32.load $w (i64.add (local.get $base)
                                     (i64.shl (local.get $i) (i64.const 2))))
               (i32.const 7)))
    (local.set $i (i64.add (local.get $i) (i64.const 1)))
    (br_if $L (i64.lt_u (local.get $i) (local.get $n)))))
""")
    out.extend(kern)

    NS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 15, 16, 17, 31, 32, 33,
          63, 64, 65, 127, 128, 129]
    for n in NS:

        out.append('(func (export "w_add_%d") (result i32)\n'
                   '  (call $fill (i32.const %d)) (call $kadd (i64.const %d))\n'
                   '  (call $chk (i32.const %d) (i64.const %d)))\n'
                   % (n, n, n, n + 8, D))
        out.append('(func (export "w_sum_%d") (result i32)\n'
                   '  (call $fill (i32.const %d)) (call $ksum (i64.const %d)))\n'
                   % (n, n, n))
        out.append('(func (export "w_byte_%d") (result i32)\n'
                   '  (call $fill (i32.const %d)) (call $kbyte (i64.const %d))\n'
                   '  (call $chkb (i32.const %d) (i64.const %d)))\n'
                   % (n, n + 32, n, n + 32, D))
        out.append('(func (export "w_fmul_%d") (result i32)\n'
                   '  (call $fillf (i32.const %d)) (call $kfmul (i64.const %d))\n'
                   '  (call $chk (i32.const %d) (i64.const %d)))\n'
                   % (n, n, n, 2 * n + 16, C))
        out.append('(func (export "w_down_%d") (result i32)\n'
                   '  (call $fill (i32.const %d)) (call $kdown (i64.const %d))\n'
                   '  (call $chk (i32.const %d) (i64.const %d)))\n'
                   % (n, n, n, n + 8, D))
        out.append('(func (export "w_dep_%d") (result i32)\n'
                   '  (call $fill (i32.const %d)) (call $kdep (i64.const %d))\n'
                   '  (call $chk (i32.const %d) (i64.const %d)))\n'
                   % (n, n + 8, n, n + 8, D))
        out.append('(func (export "w_edge_%d") (result i32)\n'
                   '  (call $fill (i32.const 300))\n'
                   '  (call $kedge (i64.const %d) (i64.const %d))\n'
                   '  (call $chk (i32.const %d) (i64.const %d)))\n'
                   % (n, END - 4 * n, n, n, END - 4 * n))

        for tag, doff in (("dis", 4096), ("f1", 4), ("f4", 16), ("f16", 64),
                          ("same", 0), ("ov1", max(0, 4 * n - 4))):
            out.append('(func (export "w_al%s_%d") (result i32)\n'
                       '  (call $fill (i32.const 400))\n'
                       '  (i64.const %d) (i64.const %d) (i64.const %d)\n'
                       '  (i32.const 0) (call_indirect (type $a3))\n'
                       '  (call $chk (i32.const 300) (i64.const %d)))\n'
                       % (tag, n, AL + doff, AL, n, AL))
    return "".join(out)

def gen_stackparams():
    """Stack parameters whose allocated home is an incoming argument register.

    AArch64 passes seven integer parameters in X1-X7 and the rest on the
    stack.  When the register parameters' moves clash, the prologue defers
    them to one parallel copy -- and the stack parameters used to land in
    their homes before that copy ran, so a home in X1-X7 overwrote a register
    parameter the copy had not read yet.  TinyGo 0.43's panic-catch wrappers
    have exactly this shape, and parameter 0 came out holding parameter 8:
    every program that printed an error trapped in call_indirect.

    permparams never reaches it: a parameter only lives in another argument
    register when it is also a merge, so each function here may reload all of
    its parameters on one path (a mutable global the compiler cannot see
    through decides which), passes them on shifted by one with parameter 0 as
    the call's table index, and folds every parameter in again after the call
    so all of them are live across it.  The export reaches each function
    through call_indirect too, as TinyGo does, so -O2 cannot inline it away.
    """
    shapes = [
        ("i32x9", ["i32"] * 9),
        ("i32x11", ["i32"] * 11),
        ("i32x12", ["i32"] * 12),
        ("i64x11", ["i32"] + ["i64"] * 10),
        ("mixx11", ["i32", "i64", "i32", "i64", "i32", "i64", "i32", "i64",
                    "i32", "i64", "i32"]),
    ]
    out = ["  (memory 1)",
           "  (global $sp_state (mut i32) (i32.const 0))",
           "  (table $sp_tab %d funcref)" % (2 * len(shapes) + 1)]

    def fold(i, t):
        if t == "i32":
            return "(i64.extend_i32_u (local.get %d))" % i
        return "(local.get %d)" % i

    def hash_all(ts, acc):
        lines = []
        for i, t in enumerate(ts):
            lines.append("    (local.set %d (i64.add (i64.mul (local.get %d) "
                         "(i64.const 1000003)) %s))" % (acc, acc, fold(i, t)))
        return lines

    for slot, (tag, ts) in enumerate(shapes, 1):
        n = len(ts)
        ps = " ".join(ts)
        cs = " ".join(ts[1:])
        out.append("  (type $sp_c_%s (func (param %s) (result i64)))" % (tag, cs))
        out.append("  (func $sp_callee_%s (type $sp_c_%s) (local i64)" % (tag, tag))
        out += hash_all(ts[1:], n - 1)
        out.append("    (local.get %d))" % (n - 1))
        out.append("  (elem (table $sp_tab) (i32.const %d) func $sp_callee_%s)"
                   % (slot, tag))

        out.append("  (type $sp_t_%s (func (param %s) (result i64)))" % (tag, ps))
        out.append("  (elem (table $sp_tab) (i32.const %d) func $sp_target_%s)"
                   % (slot + len(shapes), tag))
        out.append("  (func $sp_target_%s (type $sp_t_%s) (local i64)" % (tag, tag))
        out.append("    (if (i32.eq (global.get $sp_state) (i32.const 2))")
        out.append("      (then")
        for i, t in enumerate(ts):
            ld = "i32.load" if t == "i32" else "i64.load"
            out.append("        (local.set %d (%s offset=%d (i32.const 0)))"
                       % (i, ld, 8 * i))
        out.append("      ))")
        args = " ".join("(local.get %d)" % i for i in range(1, n))
        out.append("    (local.set %d (call_indirect $sp_tab (type $sp_c_%s) %s "
                   "(local.get 0)))" % (n, tag, args))
        out += hash_all(ts, n)
        out.append("    (local.get %d))" % n)

        consts = []
        for i, t in enumerate(ts):
            if i == 0:
                consts.append("(i32.const %d)" % slot)
            elif t == "i32":
                consts.append("(i32.const %d)" % (0x01010101 * (i + 1)))
            else:
                consts.append("(i64.const %d)" % (0x0101010101010101 * (i + 1)))
        out.append("  (func (export \"stackparams_%s\") (result i64)" % tag)
        out.append("    (call_indirect $sp_tab (type $sp_t_%s) %s (i32.const %d)))"
                   % (tag, " ".join(consts), slot + len(shapes)))
    return "\n".join(out) + "\n"

def gen_permparams():
    """Functions whose parameters have to swap registers on entry.

    Every other family here exports a function of no arguments, so nothing in
    the corpus ever put a value in an incoming ABI register and asked the
    compiler to move it somewhere else.  A prologue that places parameters one
    at a time is only correct while each keeps the register it arrived in, and
    that is what the allocator's hints usually arrange -- so the shape that
    breaks it has to make the hints *disagree* with the arrival.

    Passing the parameters on to another function in a rotated order does
    exactly that: argument k is hinted to the register argument k arrives in,
    so parameter i is wanted in the register parameter j came in, and the
    prologue has a cycle to resolve.  Rotating locals through a loop does not
    -- the hints keep each parameter where it started and there is no cycle,
    which is why the first version of this family passed against a compiler
    that was known to be wrong.

    Each parameter is folded into a polynomial hash before the call as well as
    after it, so a value clobbered on entry changes the answer.  A sum would be
    blind to two parameters swapping, which is the failure being looked for.
    """
    out = []
    shapes = [
        ("i32", ["i32"] * 3),
        ("i32b", ["i32"] * 5),
        ("i32c", ["i32"] * 7),
        ("i32d", ["i32"] * 10),
        ("i64", ["i64"] * 4),
        ("mix", ["i32", "i64", "i32", "i64", "i32"]),
        ("fmix", ["i32", "f32", "i64", "f64", "i32", "f32"]),
        ("fonly", ["f64"] * 5),
        ("wide", ["i32", "i64", "f32", "f64", "i32", "i64", "f32", "f64",
                  "i32", "i64"]),

        ("manyf", ["f64"] * 11),
        ("manyi", ["i64"] * 11),
        ("v128", ["v128"] * 4),
        ("vmix", ["v128", "i32", "v128", "f64", "v128", "i64"]),
        ("vwide", ["v128"] * 10),
    ]

    def fold(i, t):
        if t == "i32":
            return "(i64.extend_i32_u (local.get $p%d))" % i
        if t == "i64":
            return "(local.get $p%d)" % i
        if t == "f32":
            return "(i64.extend_i32_u (i32.reinterpret_f32 (local.get $p%d)))" % i
        if t == "v128":

            return ("(i64.add (i64x2.extract_lane 0 (local.get $p%d)) "
                    "(i64x2.extract_lane 1 (local.get $p%d)))" % (i, i))
        return "(i64.reinterpret_f64 (local.get $p%d))" % i

    for tag, ts in shapes:
        n = len(ts)
        ps = " ".join("(param $p%d %s)" % (i, t) for i, t in enumerate(ts))

        body = ["  (func $sink_%s %s (result i64)" % (tag, ps),
                "    (local $acc i64)"]
        for i, t in enumerate(ts):
            body.append("    (local.set $acc (i64.add (i64.mul (local.get $acc) "
                        "(i64.const 1000003)) %s))" % fold(i, t))
        body.append("    (local.get $acc))")

        body.append("  (func $perm_%s %s (result i64)" % (tag, ps))
        body.append("    (local $i i32) (local $acc i64)")
        body.append("    (loop $l")
        for i, t in enumerate(ts):
            body.append("      (local.set $acc (i64.add (i64.mul (local.get $acc) "
                        "(i64.const 1000003)) %s))" % fold(i, t))
        body.append("      (local.set $i (i32.add (local.get $i) (i32.const 1)))")
        body.append("      (br_if $l (i32.lt_u (local.get $i) (i32.const 3))))")

        rot = list(range(n))
        for t in sorted(set(ts)):
            idx = [i for i, u in enumerate(ts) if u == t]
            for k, i in enumerate(idx):
                rot[i] = idx[(k + 1) % len(idx)]
        args = " ".join("(local.get $p%d)" % rot[i] for i in range(n))
        body.append("    (i64.add (local.get $acc) (call $sink_%s %s)))" % (tag, args))

        call = []
        for i, t in enumerate(ts):
            if t == "i32":
                call.append("(i32.const %d)" % ((0x11111111 + i * 0x01010101) & 0x7fffffff))
            elif t == "i64":
                call.append("(i64.const %d)" % (0x1122334455667700 + i))
            elif t == "f32":
                call.append("(f32.const %d.5)" % (i + 3))
            elif t == "v128":
                call.append("(v128.const i64x2 %d %d)"
                            % (0x0102030405060700 + i, 0x1122334455667700 + i))
            else:
                call.append("(f64.const %d.25)" % (i + 7))
        body.append('  (func (export "perm_%s") (result i64) (call $perm_%s %s))'
                    % (tag, tag, " ".join(call)))
        out.append("\n".join(body) + "\n")
    return "".join(out)

def gen_magicdiv():
    """Unsigned division and remainder by a constant, which is done with a
    multiply rather than a divide.

    `test/magic.py` checks that the *magic number* the compiler picks is the
    right one, in exact arithmetic.  This checks the other half: that the code
    emitted around it computes what the magic says it should.  They are separate
    instruments because they fail separately -- a wrong multiplier and a
    correctly-lowered wrong multiplier look the same from here, and a right
    multiplier with the `add` fixup dropped looks fine to magic.py.

    What the dividends are chosen to hit:

      * **The top of every quotient bucket.**  A wrong magic or a shift off by
        one is wrong at `k*d - 1` and right at every other dividend, so those are
        generated from the divisor rather than listed -- `k*d - 1`, `k*d` and
        `k*d + 1` for the first twenty k.
      * **The ends of the range**, where the product overflows if it is going to:
        0, 1, 2^31, 2^32-1, and at 64 bits 2^63 and 2^64-1.
      * **Values with no relation to the divisor**, from a table, so that a magic
        that happens to work on multiples is still caught.

    The divisors straddle the two forms the sequence takes.  Roughly a third of
    divisors need the `add` fixup, where the multiplier does not fit in the width
    and the missing bit is carried by hand; 7, 14 and 641 need it and 3, 5, 10 do
    not, so both arms are covered at both widths.  Powers of two are absent
    because they never reach this path, and so is any divisor with the top bit
    set, which the transform declines.
    """

    d32 = [3, 5, 6, 7, 9, 10, 11, 14, 100, 641, 1000, 65535, 0x7ffffffd]
    d64 = [3, 7, 10, 1000, 0xffffffff, 0x100000007]
    outs = []
    outs.append("  (memory 1)")

    vals = [0, 1, 2, 3, 17, 255, 256, 65535, 65536, 1000000, 0x7fffffff,
            0x80000000, 0x80000001, 0xdeadbeef, 0xfffffffe, 0xffffffff]
    data = "".join("\\%02x" % b for v in vals
                   for b in (v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff,
                             (v >> 24) & 0xff))
    outs.append('  (data (i32.const 0) "%s")' % data)
    n = len(vals)

    for d in d32:
        for op in ("div_u", "rem_u"):
            outs.append("""  (func (export "u32_%s_%d") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const %d)))
      (local.set $s (i32.add (local.get $s) (i32.%s
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const %d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.%s
        (i32.sub (i32.mul (local.get $i) (i32.const %d)) (i32.const 1))
        (i32.const %d))))
      (local.set $s (i32.add (local.get $s) (i32.%s
        (i32.mul (local.get $i) (i32.const %d)) (i32.const %d))))
      (local.set $s (i32.add (local.get $s) (i32.%s
        (i32.add (i32.mul (local.get $i) (i32.const %d)) (i32.const 1))
        (i32.const %d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))""" % (op, d, n, op, d, op, d, d, op, d, d, op, d, d))

    for d in d64:
        for op in ("div_u", "rem_u"):
            outs.append("""  (func (export "u64_%s_%d") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const %d)))
      (local.set $s (i64.add (local.get $s) (i64.%s
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const %d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.%s
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const %d))
                 (i64.const 1)) (i64.const %d))))
      (local.set $s (i64.add (local.get $s) (i64.%s
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const %d))
        (i64.const %d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.%s (i64.const -1) (i64.const %d))))
    (local.set $s (i64.add (local.get $s) (i64.%s
      (i64.const 0x8000000000000000) (i64.const %d))))
    (local.get $s))""" % (op, d, n, op, d, op, d, d, op, d, d, op, d, op, d))

    return "\n".join(outs) + "\n"

def gen_bulkcopy():
    """`memory.copy` across the sizes and overlaps where the runtime changes
    strategy.

    The runtime does small copies itself, hands big ones to the platform, and --
    since the overlapping ones are what its own loop is worst at -- hands those
    over from 512 bytes up.  It also picks the direction: two regions that do not
    overlap can be copied forwards whichever way round they sit, and only a real
    overlap forces the pointers to walk down.  Every one of those decisions is a
    boundary, and a wrong answer at one of them is a handful of wrong bytes in the
    middle of a region that is otherwise right.

    The existing coverage was two copies of six and eight bytes, which reaches
    none of it.  So: every combination of

      * a size around each boundary -- 63, 64, 65 for the block loop, 511, 512,
        513 for the hand-over, and 4095, 4096, 4097 well past it;
      * an overlap distance of 1, 16, 63, 64 and 65 bytes, which is where a copy
        that moves a block at a time can tread on itself;
      * both directions, plus the disjoint case that must not be copied backwards
        merely because the destination is higher up.

    Each export refills the window from a generating polynomial, does one copy,
    and returns a *position-weighted* checksum of the window: a plain byte sum
    would pass a copy that moved the right bytes to the wrong places, and the
    first version of this generator did exactly that.
    """
    sizes = [63, 64, 65, 127, 255, 511, 512, 513, 1023, 1024, 4095, 4096, 4097]
    deltas = [1, 16, 63, 64, 65]
    base = 1024
    outs = []
    outs.append("  (memory 2)")

    outs.append("""  (func $fill (param $n i32)
    (local $i i32)
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (i32.store8 (i32.add (i32.const %d) (local.get $i))
                  (i32.add (i32.mul (local.get $i) (i32.const 37))
                           (i32.shr_u (local.get $i) (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))""" % base)
    outs.append("""  (func $sum (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (i32.add (local.get $i) (i32.const 1))
                 (i32.load8_u (i32.add (i32.const %d) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))""" % base)

    def one(name, dst, src, n, span):
        outs.append("""  (func (export "%s") (result i32)
    (call $fill (i32.const %d))
    (memory.copy (i32.const %d) (i32.const %d) (i32.const %d))
    (call $sum (i32.const %d)))""" % (name, span, dst, src, n, span))

    for n in sizes:
        span = n + 4 * 4096
        for d in deltas:

            one("up_%d_%d" % (n, d), base + d, base, n, span)

            one("dn_%d_%d" % (n, d), base, base + d, n, span)

        one("dis_hi_%d" % n, base + n + 64, base, n, span)
        one("dis_lo_%d" % n, base, base + n + 64, n, span)

        one("adj_hi_%d" % n, base + n, base, n, span)
        one("adj_lo_%d" % n, base, base + n, n, span)
    return "\n".join(outs) + "\n"

def gen_downloop():
    """Reductions that count *down*, which the vectoriser now accepts.

    It used to take only `!=` from a decreasing counter, on the grounds that
    `<` against one is either immediately false or runs to a wrap.  True, but
    `>=` is the form `for (i = n-1; i >= 0; i--)` leaves, and it is a perfectly
    good trip count -- the distance is just measured the other way.  Getting
    that wrong is a wrong sum rather than a crash, so the lengths below
    straddle every boundary the arithmetic has: none, fewer than a vector,
    exactly a vector, one more, and several with a remainder.

    Both signednesses and both strictnesses, because the guard picks a
    different comparison and a different wrap check for each of the four, and
    a mistake in one is invisible in the others.

    Four-byte elements, also deliberately: a narrower load summed into an i32
    is a *widening* reduction, which is a different path with its own rules --
    "widening term is not a load" refused every one of these while they were
    bytes, and the family tested nothing.

    The body is a plain sum, deliberately.  A polynomial hash would catch a
    reordering as well, but it is a serial recurrence and does not vectorise
    at all -- which makes the whole family test nothing, as the first version
    of it did.  What is being checked here is the trip count and the guard,
    and a sum is sensitive to both: an element too few or too many changes it.
    """
    out = []
    lens = [0, 1, 3, 4, 15, 16, 17, 31, 32, 33, 64, 100]
    AL = 0
    data = "".join("\\%02x" % ((i * 37 + 11) & 0xff) for i in range(128))
    out.append('  (memory 1)\n  (data (i32.const %d) "%s")\n' % (AL, data))

    for tag, rel, floor in (("ges", "ge_s", 0), ("gts", "gt_s", 0),
                            ("geu", "ge_u", 4), ("gtu", "gt_u", 4)):
        for n in lens:
            if floor and n <= floor + 1:
                continue
            out.append(
'  (func (export "down_%s_%d") (result i32)\n'
'    (local $i i32) (local $s i32)\n'
'    (local.set $i (i32.const %d))\n'
'    (block $out\n'
'      (br_if $out (i32.lt_s (local.get $i) (i32.const %d)))\n'
'      (loop $l\n'
'        (local.set $s (i32.add (local.get $s)\n'
'          (i32.mul (i32.const 3)\n'
'            (i32.load (i32.add (i32.const %d)\n'
'              (i32.shl (local.get $i) (i32.const 2)))))))\n'
'        (local.set $i (i32.sub (local.get $i) (i32.const 1)))\n'
'        (br_if $l (i32.%s (local.get $i) (i32.const %d)))))\n'
'    (local.get $s))\n' % (tag, n, n - 1, floor, AL, rel, floor))
    return "".join(out)


# ---- loop shapes for the vectoriser and the unroller -------------------------
#
# Loops the way producers leave them, P copies of the body to an iteration
# addressed by memarg offsets off one byte counter (LLVM's unrolling), in
# every element type, over trip counts that fall on and between vector
# boundaries, constant and read from a data segment the compiler cannot see
# through, with != and < exits and a counter running down; an output that
# overlaps the input it reads at small distances, the bases both constant
# and loaded; and the constant a loop uses read again after it.  Every
# export hashes what its loop wrote, a few elements past the end included,
# so a vector loop that runs over, under, or out of order shows.

VL_TYPES = {
    #       value  load            store          size  integer
    "i8":  ("i32", "i32.load8_u",  "i32.store8",  1, True),
    "i16": ("i32", "i32.load16_u", "i32.store16", 2, True),
    "i32": ("i32", "i32.load",     "i32.store",   4, True),
    "i64": ("i64", "i64.load",     "i64.store",   8, True),
    "f32": ("f32", "f32.load",     "f32.store",   4, False),
    "f64": ("f64", "f64.load",     "f64.store",   8, False),
}
VL_N = 400                       # elements each array is filled with
VL_A, VL_B, VL_C = 4096, 8192, 12288
VL_TAB = 64                      # i32 table in a data segment: ends, bases

def gen_vecloops(tn):
    vt, ld, st, S, isint = VL_TYPES[tn]
    A, B, C = VL_A, VL_B, VL_C
    tab = []                      # the data segment's values, i32 each

    def dyn(v):
        tab.append(v)
        return "(i32.load (i32.const %d))" % (VL_TAB + 4 * (len(tab) - 1))

    def k_int(c):
        return "(%s.const %d)" % (vt, c)
    def binop(op, x, y):
        return "(%s.%s %s %s)" % (vt, op, x, y)
    ops = ["add", "sub", "mul", "xor"] if isint else ["add", "mul", "sub"]
    K = k_int(37) if isint else "(%s.const 1.5)" % vt
    K2 = k_int(12345) if isint else "(%s.const -0.75)" % vt

    def bits(addr):
        if tn == "f32":
            return "(i64.extend_i32_u (i32.reinterpret_f32 (f32.load %s)))" % addr
        if tn == "f64":
            return "(i64.reinterpret_f64 (f64.load %s))" % addr
        if vt == "i64":
            return "(%s %s)" % (ld, addr)
        return "(i64.extend_i32_u (%s %s))" % (ld, addr)

    def hash_region(base, n, plus=None):
        el = "(i32.add %s (i32.mul (local.get $k) (i32.const %d)))" % (base, S)
        val = bits(el)
        if plus is not None:
            # the element plus the loop's own constant, read again after it
            v = "(%s %s)" % (ld, el)
            val = bits_of_value(binop("add", v, plus))
        return ("    (local.set $k (i32.const 0))\n"
                "    (block $hd (loop $hl\n"
                "      (br_if $hd (i32.ge_u (local.get $k) (i32.const %d)))\n"
                "      (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 0x100000001b3)) %s))\n"
                "      (local.set $k (i32.add (local.get $k) (i32.const 1)))\n"
                "      (br $hl)))\n" % (n, val))

    def bits_of_value(v):
        if tn == "f32":
            return "(i64.extend_i32_u (i32.reinterpret_f32 %s))" % v
        if tn == "f64":
            return "(i64.reinterpret_f64 %s)" % v
        if vt == "i64":
            return v
        return "(i64.extend_i32_u %s)" % v

    out = []
    # the fill: a and b pseudo-random, c a pattern of its own
    def fill(addr_k, which):
        kk = "(local.get $k)"
        if isint:
            if vt == "i64":
                v = "(i64.add (i64.mul (i64.extend_i32_u %s) (i64.const 0x9E3779B97F4A7C15)) (i64.const %d))" % (kk, which * 977)
            else:
                v = "(i32.add (i32.mul %s (i32.const %d)) (i32.const %d))" % (kk, (2654435761, 40503, 2246822519)[which], which * 977 + 5)
        else:
            v = "(%s.mul (%s.convert_i32_s (i32.sub (i32.rem_u (i32.mul %s (i32.const %d)) (i32.const 1001)) (i32.const 500))) (%s.const 0.25))" % (
                vt, vt, kk, (37, 53, 71)[which], vt)
        return "      (%s %s %s)\n" % (st, addr_k, v)
    init = ["  (func $init (local $k i32)\n    (loop $l\n"]
    for base, w in ((A, 0), (B, 1), (C, 2)):
        init.append(fill("(i32.add (i32.const %d) (i32.mul (local.get $k) (i32.const %d)))" % (base, S), w))
    init.append("      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1))) (i32.const %d)))))\n" % VL_N)

    exports = []
    def export(name, body, locals_="(local $i i32) (local $e i32) (local $pa i32) (local $pb i32) (local $pc i32) (local $k i32) (local $h i64) (local $acc %s)" % vt):
        exports.append('  (func (export "%s") (result i64)\n    %s\n    (call $init)\n%s    (local.get $h))\n' % (name, locals_, body))

    def loop(P, shape, rel, T, dynend, base_mode, outb=C, op="add"):
        """One loop of P copies; returns its body text and the region it wrote."""
        step = P * S
        end_bytes = T * S
        endv = dyn(end_bytes) if dynend else "(i32.const %d)" % end_bytes
        b = []
        if base_mode == "dyn":
            b.append("    (local.set $pa %s)\n    (local.set $pb %s)\n    (local.set $pc %s)\n" % (
                dyn(A), dyn(B), dyn(outb)))
        else:
            b.append("    (local.set $pa (i32.const %d))\n    (local.set $pb (i32.const %d))\n    (local.set $pc (i32.const %d))\n" % (A, B, outb))
        b.append("    (local.set $e %s)\n" % endv)
        if rel == "down":
            b.append("    (local.set $i (local.get $e))\n")
        else:
            b.append("    (local.set $i (i32.const 0))\n")
        if shape == "red":
            b.append("    (local.set $acc %s)\n" % ("(%s.const 1)" % vt))
        b.append("    (loop $l\n")
        if rel == "down":
            b.append("      (local.set $i (i32.sub (local.get $i) (i32.const %d)))\n" % step)
        pa = "(i32.add (local.get $pa) (local.get $i))"
        pb = "(i32.add (local.get $pb) (local.get $i))"
        pc = "(i32.add (local.get $pc) (local.get $i))"
        for c in range(P):
            la = "(%s offset=%d %s)" % (ld, c * S, pa)
            la1 = "(%s offset=%d %s)" % (ld, (c + 1) * S, pa)
            lb = "(%s offset=%d %s)" % (ld, c * S, pb)
            if shape == "elem":
                b.append("      (%s offset=%d %s %s)\n" % (st, c * S, pc, binop(op, la, lb)))
            elif shape == "neigh":
                b.append("      (%s offset=%d %s %s)\n" % (st, c * S, pc, binop(op, la, la1)))
            elif shape == "scale":
                b.append("      (%s offset=%d %s %s)\n" % (st, c * S, pc, binop("add", binop("mul", la, K), K2)))
            elif shape == "inplace":
                b.append("      (%s offset=%d %s %s)\n" % (st, c * S, pa, binop("add", la, K2)))
            elif shape == "red":
                rop = "xor" if (isint and op == "xor") else "add"
                b.append("      (local.set $acc %s)\n" % binop(rop, "(local.get $acc)", la))
        if rel == "down":
            b.append("      (br_if $l (i32.ne (local.get $i) (i32.const 0))))\n")
        else:
            cmpop = "i32.ne" if rel == "ne" else "i32.lt_u"
            b.append("      (br_if $l (%s (local.tee $i (i32.add (local.get $i) (i32.const %d))) (local.get $e))))\n" % (cmpop, step))
        ran = -(-T // P) * P if rel == "lt" else T
        return "".join(b), ran

    VARIANTS = [("ne", 16, False), ("ne", 17, False), ("lt", None, False), ("ne", 31, True),
                ("down", 9, False), ("lt", None, True), ("ne", 1, False), ("ne", 64, False),
                ("down", 33, True), ("lt", None, False)]
    LT_T = [61, 5, 129, 2, 100, 7]
    vi = 0
    for P in (1, 2, 3, 4, 5):
        for si, shape in enumerate(("elem", "neigh", "scale", "inplace", "red")):
            for j in range(3):
                rel, mult, dynend = VARIANTS[vi % len(VARIANTS)]
                T = LT_T[vi % len(LT_T)] if rel == "lt" else mult * P
                op = ops[vi % len(ops)]
                base_mode = "dyn" if vi % 4 == 3 else "const"
                vi += 1
                body, ran = loop(P, shape, rel, T, dynend, base_mode, op=op)
                if shape == "red":
                    body += "    (local.set $h %s)\n" % bits_of_value("(local.get $acc)")
                elif shape == "inplace":
                    body += hash_region("(i32.const %d)" % A, ran + 4)
                elif shape == "scale":
                    body += hash_region("(i32.const %d)" % C, ran + 4, plus=K2)
                else:
                    body += hash_region("(i32.const %d)" % C, ran + 4)
                export("%s_p%d_%s_%s%d%s%s" % (tn, P, shape, rel, T, "_dyn" if dynend else "",
                                               "_dynbase" if base_mode == "dyn" else ""), body)
    # an output that overlaps the input it reads: c[e] = a[e] + 1 with c at a + d
    for P in (1, 3, 4):
        for d in (-4, -1, 1, 2, 3, 4, 16):
            for base_mode in ("const", "dyn"):
                outb = A + d * S
                T = 12 * P
                body, ran = loop(P, "elem", "ne", T, False, base_mode, outb=outb, op="add")
                # elem reads b too; make it a read of a alone
                body = body.replace("(local.get $pb)", "(local.get $pa)")
                body += hash_region("(i32.const %d)" % min(A, outb), ran + abs(d) + 4)
                export("%s_overlap_p%d_d%s%d_%s" % (tn, P, "m" if d < 0 else "", abs(d), base_mode), body)

    seg = "".join("\\%02x" % ((v >> (8 * i)) & 0xff) for v in tab for i in range(4))
    return ("  (memory 1)\n  (data (i32.const %d) \"%s\")\n" % (VL_TAB, seg) +
            "".join(init) + "".join(exports))

# ---- interleaved loads ----------------------------------------------------
#
# Loops that read m interleaved streams -- RGB pixels, xyz points, stereo
# samples -- m = 2, 3, 4 elements a step of one input, and write one element
# a step of the output: weighted sums with and without a rounding constant
# shifted down by the narrow type's width (fixed-point code, which in bytes
# and halfwords needs lanes twice as wide), shifted by something else, plain
# lane arithmetic, products of the streams.  The counters as producers leave
# them, one for each array or one scaled by m, the trip counts as in
# vecloops, an output overlapping the input; each export hashes its output,
# a few elements past the end too.

IL_TYPES = {
    #       value  load             store          size
    "u8":  ("i32", "i32.load8_u",  "i32.store8",  1),
    "s8":  ("i32", "i32.load8_s",  "i32.store8",  1),
    "u16": ("i32", "i32.load16_u", "i32.store16", 2),
    "s16": ("i32", "i32.load16_s", "i32.store16", 2),
    "i32": ("i32", "i32.load",     "i32.store",   4),
    "i64": ("i64", "i64.load",     "i64.store",   8),
    "f32": ("f32", "f32.load",     "f32.store",   4),
    "f64": ("f64", "f64.load",     "f64.store",   8),
}
IL_W = {
    "u8": ([77, 150, 29, 200], 128),
    "s8": ([-3, 5, 7, -9], 64),
    "u16": ([19595, 38470, 7471, 1000], 32768),
    "s16": ([-300, 500, 700, -900], 16384),
    "i32": ([3, -5, 7, 11], 1000),
    "i64": ([3, -5, 7, 11], 1000),
}

def gen_ilvloops(tn):
    vt, ld, st, S = IL_TYPES[tn]
    isint = vt in ("i32", "i64")
    A, C = VL_A, VL_C
    tab = []

    def dyn(v):
        tab.append(v)
        return "(i32.load (i32.const %d))" % (VL_TAB + 4 * (len(tab) - 1))

    def k(c):
        return "(%s.const %s)" % (vt, c)

    def shapes(m):
        """name -> function of the m loaded values giving the stored value"""
        sh = {}
        if isint:
            w, rnd = IL_W[tn]
            def dot(x, r):
                t = None
                for i in range(m):
                    p = "(%s.mul %s %s)" % (vt, x[i], k(w[i]))
                    t = p if t is None else "(%s.add %s %s)" % (vt, t, p)
                return t if r is None else "(%s.add %s %s)" % (vt, t, k(r))
            hb = 8 * S if S < 4 else 3
            sh["dot"] = lambda x: "(%s.shr_u %s %s)" % (vt, dot(x, rnd), k(hb))
            sh["dotnr"] = lambda x: "(%s.shr_u %s %s)" % (vt, dot(x, None), k(hb))
            # the constant first, as a producer may put it
            sh["dotcf"] = lambda x: "(%s.shr_u (%s.add %s %s) %s)" % (
                vt, vt, k(rnd), dot(x, None), k(hb))
            sh["dot7"] = lambda x: "(%s.shr_u %s %s)" % (vt, dot(x, rnd), k(hb - 1))
            sh["low"] = lambda x: dot(x, rnd)
            def mix(x):
                t = "(%s.xor (%s.add %s (%s.shl %s %s)) %s)" % (
                    vt, vt, x[0], vt, x[1], k(1), x[m - 1])
                return t
            sh["mix"] = mix
            def prod(x):
                t = "(%s.mul %s %s)" % (vt, x[0], x[1])
                if m > 2:
                    t = "(%s.add %s (%s.mul %s %s))" % (vt, t, vt, x[2], x[m - 1])
                return "(%s.shr_u (%s.add %s %s) %s)" % (vt, vt, t, k(rnd), k(hb))
            sh["prod"] = prod
        else:
            def fdot(x):
                cs = ["0.5", "0.25", "-1.5", "2"]
                t = None
                for i in range(m):
                    p = "(%s.mul %s %s)" % (vt, x[i], k(cs[i]))
                    t = p if t is None else "(%s.add %s %s)" % (vt, t, p)
                return t
            sh["dot"] = fdot
            sh["mix"] = lambda x: "(%s.sub (%s.max %s %s) %s)" % (vt, vt, x[0], x[1], x[m - 1])
        return sh

    def fill():
        kk = "(local.get $k)"
        if vt == "i64":
            v = "(i64.add (i64.mul (i64.extend_i32_u %s) (i64.const 0x9E3779B97F4A7C15)) (i64.const 977))" % kk
        elif vt == "i32":
            v = "(i32.add (i32.mul %s (i32.const 2654435761)) (i32.shr_u %s (i32.const 3)))" % (kk, kk)
        else:
            v = "(%s.mul (%s.convert_i32_s (i32.sub (i32.rem_u (i32.mul %s (i32.const 37)) (i32.const 1001)) (i32.const 500))) (%s.const 0.25))" % (vt, vt, kk, vt)
        out = ["  (func $init (local $k i32)\n    (loop $l\n"]
        for base in (A, C):
            out.append("      (%s (i32.add (i32.const %d) (i32.mul (local.get $k) (i32.const %d))) %s)\n" % (
                st, base, S, v if base == A else "(%s.const 0)" % vt))
        out.append("      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1))) (i32.const %d)))))\n" % VL_N)
        return "".join(out)

    def hash_region(base, n):
        if tn in ("f32",):
            val = "(i64.extend_i32_u (i32.reinterpret_f32 (f32.load %s)))"
        elif tn == "f64":
            val = "(i64.reinterpret_f64 (f64.load %s))"
        elif vt == "i64":
            val = "(i64.load %s)"
        else:
            val = "(i64.extend_i32_u (" + ld + " %s))"
        el = "(i32.add %s (i32.mul (local.get $k) (i32.const %d)))" % (base, S)
        return ("    (local.set $k (i32.const 0))\n"
                "    (block $hd (loop $hl\n"
                "      (br_if $hd (i32.ge_u (local.get $k) (i32.const %d)))\n"
                "      (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 0x100000001b3)) %s))\n"
                "      (local.set $k (i32.add (local.get $k) (i32.const 1)))\n"
                "      (br $hl)))\n" % (n, val % el))

    exports = []
    def export(name, body):
        exports.append('  (func (export "%s") (result i64)\n'
                       '    (local $i i32) (local $j i32) (local $e i32) (local $pa i32) (local $pc i32) (local $k i32) (local $h i64)\n'
                       '    (call $init)\n%s    (local.get $h))\n' % (name, body))

    def loop(m, expr, P, rel, T, dynend, dynbase, scaled, outb=C):
        b = []
        if dynbase:
            b.append("    (local.set $pa %s)\n    (local.set $pc %s)\n" % (dyn(A), dyn(outb)))
        else:
            b.append("    (local.set $pa (i32.const %d))\n    (local.set $pc (i32.const %d))\n" % (A, outb))
        step = P * S
        endv = dyn(T * S) if dynend else "(i32.const %d)" % (T * S)
        b.append("    (local.set $e %s)\n" % endv)
        if rel == "down":
            b.append("    (local.set $i (local.get $e))\n"
                     "    (local.set $j (i32.mul (local.get $e) (i32.const %d)))\n" % m)
        else:
            b.append("    (local.set $i (i32.const 0))\n    (local.set $j (i32.const 0))\n")
        b.append("    (loop $l\n")
        if rel == "down":
            b.append("      (local.set $i (i32.sub (local.get $i) (i32.const %d)))\n" % step)
            if not scaled:
                b.append("      (local.set $j (i32.sub (local.get $j) (i32.const %d)))\n" % (m * step))
        if scaled:
            pa = "(i32.add (local.get $pa) (i32.mul (local.get $i) (i32.const %d)))" % m
        else:
            pa = "(i32.add (local.get $pa) (local.get $j))"
        pc = "(i32.add (local.get $pc) (local.get $i))"
        for c in range(P):
            xs = ["(%s offset=%d %s)" % (ld, (c * m + t) * S, pa) for t in range(m)]
            b.append("      (%s offset=%d %s %s)\n" % (st, c * S, pc, expr(xs)))
        if rel == "down":
            b.append("      (br_if $l (i32.ne (local.get $i) (i32.const 0))))\n")
        else:
            if not scaled:
                b.append("      (local.set $j (i32.add (local.get $j) (i32.const %d)))\n" % (m * step))
            cmpop = "i32.ne" if rel == "ne" else "i32.lt_u"
            b.append("      (br_if $l (%s (local.tee $i (i32.add (local.get $i) (i32.const %d))) (local.get $e))))\n" % (cmpop, step))
        ran = -(-T // P) * P if rel == "lt" else T
        return "".join(b), ran

    VARIANTS = [("ne", 16, False), ("ne", 17, False), ("lt", None, False), ("ne", 31, True),
                ("down", 9, False), ("lt", None, True), ("ne", 1, False), ("ne", 64, False),
                ("down", 33, True), ("ne", 50, False)]
    LT_T = [61, 5, 99, 2, 40, 7]
    vi = 0
    for m in (2, 3, 4):
        for name, expr in sorted(shapes(m).items()):
            for P in (1, 2):
                for j in range(2):
                    rel, mult, dynend = VARIANTS[vi % len(VARIANTS)]
                    T = LT_T[vi % len(LT_T)] if rel == "lt" else mult * P
                    T = min(T, (VL_N // m) // P * P)
                    dynbase = vi % 4 == 3
                    scaled = vi % 3 == 1
                    vi += 1
                    body, ran = loop(m, expr, P, rel, T, dynend, dynbase, scaled)
                    body += hash_region("(i32.const %d)" % C, ran + 4)
                    export("%s_m%d_%s_p%d_%s%d%s%s%s" % (
                        tn, m, name, P, rel, T, "_dyn" if dynend else "",
                        "_dynbase" if dynbase else "", "_scaled" if scaled else ""), body)
    # the output over the input: c at a + d, read m elements a step and
    # written one, so the two meet
    for m in (2, 3, 4):
        expr = sorted(shapes(m).items())[0][1]
        for d in (-3, -1, 1, 2, 5, 16, 40):
            for dynbase in (False, True):
                outb = A + d * S
                T = 24
                body, ran = loop(m, expr, 1, "ne", T, False, dynbase, False, outb=outb)
                body += hash_region("(i32.const %d)" % min(A, outb), m * ran + abs(d) + 4)
                export("%s_overlap_m%d_d%s%d%s" % (tn, m, "m" if d < 0 else "", abs(d),
                                                 "_dynbase" if dynbase else ""), body)

    seg = "".join("\\%02x" % ((v >> (8 * i)) & 0xff) for v in tab for i in range(4))
    return ("  (memory 1)\n  (data (i32.const %d) \"%s\")\n" % (VL_TAB, seg) +
            fill() + "".join(exports))

for _t in IL_TYPES:
    write("ilvloops_" + _t, gen_ilvloops(_t))

write("vecloops_i8", gen_vecloops("i8"))
write("vecloops_i16", gen_vecloops("i16"))
write("vecloops_i32", gen_vecloops("i32"))
write("vecloops_i64", gen_vecloops("i64"))
write("vecloops_f32", gen_vecloops("f32"))
write("vecloops_f64", gen_vecloops("f64"))
write("downloop", gen_downloop())
write("bulkcopy", gen_bulkcopy())
write("magicdiv", gen_magicdiv())
write("permparams", gen_permparams())
write("stackparams", gen_stackparams())
write("bswap", gen_bswap())
write("vector64", gen_vector64())
write("exceptions", gen_exceptions())
write("exnclause", gen_exn_clauses())
write("exnref", gen_exnref())
write("exnwalk", gen_exn_walk())
write("exnedge", gen_exn_edge())
write("mutstate", gen_mutstate())
write("typedref", gen_typedref())
write("wideframe", gen_wideframe())
write("fuse", gen_fuse())
write("vector", gen_vector())
write("i32", gen_i32())
write("i64", gen_i64())
write("f32", gen_float(32))
write("f64", gen_float(64))
write("conv", gen_conv())
write("bits", gen_bits())
write("brtable", gen_brtable())
write("vmctx", gen_vmctx())
print("generated arithmetic cases in", OUT)
