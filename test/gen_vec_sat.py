"""test/cases_opt/vec_sat.wat: saturating arithmetic on bytes and halfwords
as LLVM writes it for wasm, which has no such types -- compares, selects
and a 64-bit product on values moved to the top of an i32 -- vectorised in
the elements' own lanes (opt.c v_sat_form, v_shlsat, v_mulsat, v_guard and
the lanes' fits by ir_known_bits and ir_sign_ge).  The module's header says
what each function is; every answer is worked out here, on a model of the
memory and of each loop as wasm runs it.

  python3 test/gen_vec_sat.py test/cases_opt/vec_sat.wat

which writes test/cases_opt/vec_sat_nofold.wat and vec_narrow.wat beside it.
"""
import sys

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1
A, B, OUT, LEN = 0x400, 0x1400, 0x2400, 1024
FNV = 1099511628211


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


# ---- the module's helpers, and their model ----------------------------------

# the halfwords A starts with: the ends of their range and the values beside
EDGES = b"".join(h.to_bytes(2, "little") for h in (0x7fff, 0x8000, 0x0000, 0xffff, 0x0001, 0x7ffe, 0x8001, 0x4000))

HELPERS = """  ;; A and B, a thousand bytes each, by a generator of the seed; A starts
  ;; with the halfwords at the ends of their range
  (func $fill (param $x i32)
    (local $i i32)
    (loop $l
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (i32.store8 (i32.add (i32.const %(A)d) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 13)))
      (i32.store8 (i32.add (i32.const %(B)d) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 21)))
      (i32.store8 (i32.add (i32.const %(OUT)d) (local.get $i)) (i32.const 0x5a))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const %(LEN)d))))
    (i64.store (i32.const %(A)d) (i64.const %(E0)d))
    (i64.store (i32.const %(A8)d) (i64.const %(E1)d)))
  ;; the three, hashed
  (func $sum (result i64)
    (local $i i32) (local $h i64)
    (loop $l
      (local.set $h (i64.xor (i64.mul (local.get $h) (i64.const %(FNV)d))
                             (i64.load8_u (i32.add (i32.const %(A)d) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const %(SPAN)d))))
    (local.get $h))
""" % {"A": A, "B": B, "OUT": OUT, "LEN": LEN, "FNV": FNV, "A8": A + 8, "SPAN": OUT + LEN - A,
       "E0": int.from_bytes(EDGES[:8], "little", signed=True), "E1": int.from_bytes(EDGES[8:], "little", signed=True)}


class Mem:
    def __init__(self, seed):
        self.m = bytearray(65536)
        x = seed & M32
        for i in range(LEN):
            x = (x * 1664525 + 1013904223) & M32
            self.m[A + i] = (x >> 13) & 0xff
            self.m[B + i] = (x >> 21) & 0xff
            self.m[OUT + i] = 0x5a
        self.m[A:A + 16] = EDGES

    def ld(self, a, n, signed):
        return int.from_bytes(self.m[a:a + n], "little", signed=signed)

    def st(self, a, n, v):
        self.m[a:a + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")

    def sum(self):
        h = 0
        for i in range(OUT + LEN - A):
            h = ((h * FNV) & M64) ^ self.m[A + i]
        return h


# ---- the pieces, in wasm and in Python ---------------------------------------
# Each takes wasm text for its operands and gives wasm text; $t and $u are
# i32 locals for its own use, $p an i64.

def w_clamp16(x):
    return ("(local.set $t %s)\n"
            "          (local.set $t (select (local.get $t) (i32.const 32767) (i32.lt_s (local.get $t) (i32.const 32767))))\n"
            "          (local.set $t (select (local.get $t) (i32.const -32768) (i32.gt_s (local.get $t) (i32.const -32768))))" % x)


def clamp(v, bits):
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return lo if v < lo else hi if v > hi else v


def w_mulsat(top, mul, bits=16, sign="hi", ne=True, sat=None, down=None, cmp=31, half=32, sgsh=None):
    """the product of the i32 `top` (a value at the top of its i32) and the
    i64 `mul`, held to the i32 and moved down: into $t.  The rest are the
    form's constants, for the functions that have one of them a step off."""
    up = 32 - bits
    down = up if down is None else down
    sat = sat if sat is not None else ((1 << (bits - 1)) - 1) << up
    hi = "(i32.wrap_i64 (i64.shr_u (local.get $p) (i64.const %d)))" % half
    sg = ("(i32.shr_s %s (i32.const %d))" % (hi, sgsh or 31) if sign == "hi"
          else "(i32.wrap_i64 (i64.shr_s (local.get $p) (i64.const %d)))" % (sgsh or 63))
    over = "(i32.ne %s (i32.shr_s (i32.wrap_i64 (local.get $p)) (i32.const %d)))" % (hi, cmp)
    arms = ("(i32.xor %s (i32.const %d)) (i32.wrap_i64 (local.get $p))" % (sg, sat)) if ne else \
           ("(i32.wrap_i64 (local.get $p)) (i32.xor %s (i32.const %d))" % (sg, sat))
    if not ne:
        over = over.replace("(i32.ne ", "(i32.eq ", 1)
    return ("(local.set $p (i64.mul (i64.extend_i32_s %s) %s))\n"
            "          (local.set $t (i32.shr_s (select %s %s) (i32.const %d)))" % (top, mul, arms, over, down))


def mulsat(top, mul, bits=16, sat=None, sign="hi", down=None, cmp=31, half=32, sgsh=None):
    """as wasm computes it: top an i32, mul an i64"""
    up = 32 - bits
    down = up if down is None else down
    sat = sat if sat is not None else ((1 << (bits - 1)) - 1) << up
    p = (s32(top) * s64(mul)) & M64
    lo, hi = p & M32, (p >> half) & M32
    over = hi != (s32(lo) >> cmp) & M32
    sg = (s32(hi) >> (sgsh or 31)) & M32 if sign == "hi" else (s64(p) >> (sgsh or 63)) & M32
    return s32((sg ^ sat) if over else lo) >> down


# ---- the functions ------------------------------------------------------------

FUNCS = []   # (name, wasm body, model(mem, n, q), runs [(n, q)], shapes)
NS = [0, 1, 7, 8, 9, 31, 32, 33, 100]


def loop(body, step=1):
    return ("    (call $fill (local.get $q))\n"
            "    (block $done\n      (loop $l\n        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))\n"
            "          %s\n"
            "        (local.set $i (i32.add (local.get $i) (i32.const %d)))\n        (br $l)))\n"
            "    (call $sum)" % (body, step))


def at(base, size):
    sh = {1: 0, 2: 1}[size]
    i = "(local.get $i)" if not sh else "(i32.shl (local.get $i) (i32.const %d))" % sh
    return "(i32.add (i32.const %d) %s)" % (base, i)


def fn(name, body, model, qs, shapes, ns=NS):
    def run(n, q):
        m = Mem(q)
        for i in range(n):
            model(m, i, q)
        return m.sum()
    FUNCS.append((name, loop(body), run, [(n, q) for q in qs for n in ns], shapes))


# -- bytes: (p +| bright) -| dark <<| shift, in place, as zigbench/saturate's tone
def tone(m, i, q):
    b, d, sh = q & 0xff, (q >> 8) & 0xff, (q >> 16) & 31
    t = min(m.ld(A + i, 1, False) + b, 255)
    u = (t - d) & M32
    u = 0 if u > t else u
    v = (u << 24) & M32
    w = (v << sh) & M32
    r = M32 if (w >> sh) != v else w
    m.st(A + i, 1, r >> 24)


TONE = """(local.set $t (i32.add (i32.load8_u %(a)s) (i32.and (local.get $q) (i32.const 255))))
          (local.set $t (select (local.get $t) (i32.const 255) (i32.lt_u (local.get $t) (i32.const 255))))
          (local.set $u (i32.sub (local.get $t) (i32.and (i32.shr_u (local.get $q) (i32.const 8)) (i32.const 255))))
          (local.set $u (select (i32.const 0) (local.get $u) (i32.gt_u (local.get $u) (local.get $t))))
          (local.set $t (i32.shl (local.get $u) (i32.const 24)))
          (local.set $u (i32.shl (local.get $t) (i32.shr_u (local.get $q) (i32.const 16))))
          (i32.store8 %(a)s
            (i32.shr_u (select (i32.const -1) (local.get $u)
                               (i32.ne (local.get $t) (i32.shr_u (local.get $u) (i32.shr_u (local.get $q) (i32.const 16)))))
                       (i32.const 24)))""" % {"a": at(A, 1)}
SHIFTS = [0, 1, 3, 7, 8, 31, 33]
fn("tone", TONE, tone, [0x1234 | s << 16 for s in SHIFTS] + [0x00ff, 0xff00, 0x30000], {r"\suqshl\.16b\s": 5},
   ns=[0, 1, 15, 16, 17, 100])


# the same on halfwords, the bound's ones only where the way down keeps them
def shl16(m, i, q):
    sh = q & 31
    v = (m.ld(A + 2 * i, 2, False) << 16) & M32
    w = (v << sh) & M32
    r = 0xffff0000 if (w >> sh) != v else w
    m.st(OUT + 2 * i, 2, r >> 16)


SHL16 = """(local.set $t (i32.shl (i32.load16_u %(a)s) (i32.const 16)))
          (local.set $u (i32.shl (local.get $t) (local.get $q)))
          (i32.store16 %(o)s
            (i32.shr_u (select (local.get $u) (i32.const 0xffff0000)
                               (i32.eq (i32.shr_u (local.get $u) (local.get $q)) (local.get $t)))
                       (i32.const 16)))""" % {"a": at(A, 2), "o": at(OUT, 2)}
fn("shl16", SHL16, shl16, [0, 1, 5, 15, 16, 17, 31, 36], {r"\suqshl\.8h\s": 5}, ns=[0, 1, 8, 9, 33, 100])


# the bound a bit short of the lane's ones: no saturating shift, as it was
def shlshort(m, i, q):
    sh = q & 31
    v = (m.ld(A + i, 1, False) << 24) & M32
    w = (v << sh) & M32
    r = 0x7f000000 if (w >> sh) != v else w
    m.st(OUT + i, 1, r >> 24)


SHLSHORT = """(local.set $t (i32.shl (i32.load8_u %(a)s) (i32.const 24)))
          (local.set $u (i32.shl (local.get $t) (local.get $q)))
          (i32.store8 %(o)s
            (i32.shr_u (select (i32.const 0x7f000000) (local.get $u)
                               (i32.ne (local.get $t) (i32.shr_u (local.get $u) (local.get $q))))
                       (i32.const 24)))""" % {"a": at(A, 1), "o": at(OUT, 1)}
fn("shlshort", SHLSHORT, shlshort, [0, 2, 7, 9], {r"\suqshl\.": 0}, ns=[0, 17, 100])


# one constant of the shift's form a step off
def mk_shloff(name, up, back, down):
    def model(m, i, q):
        sh, bk = q & 31, (q ^ back) & 31
        v = (m.ld(A + i, 1, False) << up) & M32
        w = (v << sh) & M32
        m.st(OUT + i, 1, (M32 if (w >> bk) != v else w) >> down)
    amt = "(local.get $q)"
    bk = amt if not back else "(i32.xor (local.get $q) (i32.const %d))" % back
    fn(name, """(local.set $t (i32.shl (i32.load8_u %(a)s) (i32.const %(up)d)))
          (local.set $u (i32.shl (local.get $t) %(amt)s))
          (i32.store8 %(o)s
            (i32.shr_u (select (i32.const -1) (local.get $u)
                               (i32.ne (local.get $t) (i32.shr_u (local.get $u) %(bk)s)))
                       (i32.const %(down)d)))""" % {"a": at(A, 1), "o": at(OUT, 1), "up": up, "amt": amt, "bk": bk, "down": down},
       model, [0, 2, 7, 9], {r"\suqshl\.": 0}, ns=[0, 17, 100])


mk_shloff("shl_down25", 24, 0, 25)
mk_shloff("shl_up23", 23, 0, 24)
mk_shloff("shl_back", 24, 1, 24)


# -- a byte's difference kept where it did not wrap, the three other ways round
def subsat(m, i, q):
    x, y = m.ld(A + i, 1, False), m.ld(B + i, 1, False)
    m.st(OUT + i, 1, x - y if x >= y else 0)


for nm, sel in (("sub_le", "(select (local.get $u) (i32.const 0) (i32.le_u (local.get $u) (local.get $t)))"),
                ("sub_lt", "(select (i32.const 0) (local.get $u) (i32.lt_u (local.get $t) (local.get $u)))"),
                ("sub_ge", "(select (local.get $u) (i32.const 0) (i32.ge_u (local.get $t) (local.get $u)))")):
    fn(nm, """(local.set $t (i32.load8_u %s))
          (local.set $u (i32.sub (local.get $t) (i32.load8_u %s)))
          (i32.store8 %s %s)""" % (at(A, 1), at(B, 1), at(OUT, 1), sel), subsat, [1, 77],
       {r"\suqsub\.16b\s": 5}, ns=[0, 15, 16, 17, 100])


# a difference above its first operand or equal to it is no test of a wrap
def sub_geu(m, i, q):
    x, y = m.ld(A + i, 1, False), m.ld(B + i, 1, False)
    d = (x - y) & M32
    m.st(OUT + i, 1, 0 if d >= x else d)


fn("sub_wrong", """(local.set $t (i32.load8_u %s))
          (local.set $u (i32.sub (local.get $t) (i32.load8_u %s)))
          (i32.store8 %s (select (i32.const 0) (local.get $u) (i32.ge_u (local.get $u) (local.get $t))))"""
   % (at(A, 1), at(B, 1), at(OUT, 1)), sub_geu, [1, 77], {r"\suqsub\.": 0}, ns=[0, 16, 100])


def sub_geu2(m, i, q):
    x, y = m.ld(A + i, 1, False), m.ld(B + i, 1, False)
    d = (x - y) & M32
    m.st(OUT + i, 1, d if d >= x else 0)


fn("sub_wrong2", """(local.set $t (i32.load8_u %s))
          (local.set $u (i32.sub (local.get $t) (i32.load8_u %s)))
          (i32.store8 %s (select (local.get $u) (i32.const 0) (i32.ge_u (local.get $u) (local.get $t))))"""
   % (at(A, 1), at(B, 1), at(OUT, 1)), sub_geu2, [1, 77], {r"\suqsub\.": 0}, ns=[0, 16, 100])


# -- a byte's sum made in 64 bits and narrowed, and one widened to be stored
def thru(m, i, q):
    m.st(OUT + i, 1, m.ld(A + i, 1, False) + 5)
    m.st(OUT + 512 + i, 1, (m.ld(B + i, 1, False) ^ 0x33))


fn("thru", """(i32.store8 %s (i32.wrap_i64 (i64.add (i64.load8_u %s) (i64.const 5))))
          (i64.store8 %s (i64.extend_i32_u (i32.xor (i32.load8_u %s) (i32.const 0x33))))"""
   % (at(OUT, 1), at(A, 1), at(OUT + 512, 1), at(B, 1)), thru, [1, 9], {r"\sadd\.16b\s": 5, r"\seor\.16b\s": 5},
   ns=[0, 15, 16, 17, 100])


# -- a halfword times a constant, held to its range
def mk_mulk(name, K, shapes, sign="hi", ne=True, top=None, pmodel=None, ns=None, **form):
    top = top or "(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2)

    def model(m, i, q):
        x = m.ld(A + 2 * i, 2, True)
        t = (pmodel(x) if pmodel else (x << 16)) & M32
        m.st(OUT + 2 * i, 2, mulsat(t, K, sign=sign, **form))
    body = w_mulsat(top, "(i64.const %d)" % K, sign=sign, ne=ne, **form) + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2)
    fn(name, body, model, [1, 0xdeadbeef], shapes, ns=ns or [0, 1, 8, 9, 31, 32, 33, 100])


VEC = {r"\ssmull\.4s\s": 5, r"\ssmull2\.4s\s": 5, r"\ssqxtn2\.8h\s": 5}
NOVEC = {r"\ssmull2?\.4s\s": 0}
mk_mulk("mul_m18", -18, VEC)
mk_mulk("mul_70", 70, VEC, sign="p", ne=False)
mk_mulk("mul_3", 3, VEC)             # a shift and an add
mk_mulk("mul_7", 7, VEC)             # a shift and a subtract
mk_mulk("mul_16", 16, VEC)           # a shift
mk_mulk("mul_max", 32767, VEC)
mk_mulk("mul_min", -32768, VEC)
mk_mulk("mul_over", 32768, NOVEC)    # no halfword: as it was
mk_mulk("mul_under", -32769, NOVEC)
# (s >> 3) *| k: the value shifted up three short and masked
mk_mulk("mul_sh3", -18, {**VEC, r"\ssshr\.8h\s": 5},
        top="(i32.and (i32.shl (i32.load16_s %s) (i32.const 13)) (i32.const -65536))" % at(A, 2),
        pmodel=lambda x: (x << 13) & 0xffff0000)
# ... of an unsigned halfword, whose shift down is not the lane's
mk_mulk("mul_sh3u", 70, NOVEC,
        top="(i32.and (i32.shl (i32.load16_u %s) (i32.const 13)) (i32.const -65536))" % at(A, 2),
        pmodel=lambda x: ((x & 0xffff) << 13) & 0xffff0000)


# one constant of the form a step off: no saturating product, as it was
OFF = [0, 9, 100]
mk_mulk("mul_down15", 70, NOVEC, ns=OFF, down=15)
mk_mulk("mul_cmp30", 70, NOVEC, ns=OFF, cmp=30)
mk_mulk("mul_half31", 70, NOVEC, ns=OFF, half=31)
mk_mulk("mul_sg30", 70, NOVEC, ns=OFF, sgsh=30)
mk_mulk("mul_sg62", 70, NOVEC, ns=OFF, sign="p", sgsh=62)
mk_mulk("mul_top15", 70, NOVEC, ns=OFF, top="(i32.shl (i32.load16_s %s) (i32.const 15))" % at(A, 2),
        pmodel=lambda x: x << 15)
mk_mulk("mul_mask17", 70, NOVEC, ns=OFF,
        top="(i32.and (i32.shl (i32.load16_s %s) (i32.const 13)) (i32.const -131072))" % at(A, 2),
        pmodel=lambda x: (x << 13) & 0xfffe0000)


# the bound of another range than the lane's: as it was
def mulbad(m, i, q):
    m.st(OUT + 2 * i, 2, mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, 70, sat=0x7ffe0000))


FUNCS.append(("mul_bound", loop(w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2), "(i64.const 70)",
                                         sat=0x7ffe0000)
                                + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2)),
              lambda n, q: (lambda m: ([mulbad(m, i, q) for i in range(n)], m.sum())[1])(Mem(q)),
              [(n, q) for q in (1, 0xdeadbeef) for n in (0, 9, 100)], NOVEC))


# -- times a value the loop does not change: in the lane's range, or the scalar loop
def mulinv_u(m, i, q):
    m.st(OUT + 2 * i, 2, mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, q & M32))


def mulinv_s(m, i, q):
    m.st(OUT + 2 * i, 2, mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, s32(q)))


QS = [5, 1, 0, 32767, 32768, 40000, 0xffff8000, 0xffff7fff, 0xffffffff, 0x80000000]
fn("mulinv_u", "(local.set $g (i64.extend_i32_u (local.get $q)))\n          "
   + w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2), "(local.get $g)", sign="p")
   + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2), mulinv_u, QS, VEC, ns=[0, 9, 33, 100])
fn("mulinv_s", "(local.set $g (i64.extend_i32_s (local.get $q)))\n          "
   + w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2), "(local.get $g)")
   + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2), mulinv_s, QS, VEC, ns=[0, 9, 33, 100])


# -- times the other array's halfword, lane by lane
def mulvar(m, i, q):
    m.st(OUT + 2 * i, 2, mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, m.ld(B + 2 * i, 2, True)))


fn("mulvar", w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2),
                      "(i64.extend_i32_s (i32.load16_s %s))" % at(B, 2))
   + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2), mulvar, [1, 0xdeadbeef], VEC, ns=[0, 1, 8, 9, 33, 100])


# -- a byte's signed product
def mul8(m, i, q):
    m.st(OUT + i, 1, mulsat((m.ld(A + i, 1, True) << 24) & M32, -5, bits=8))


fn("mul8", w_mulsat("(i32.shl (i32.load8_s %s) (i32.const 24))" % at(A, 1), "(i64.const -5)", bits=8)
   + "\n          (i32.store8 %s (local.get $t))" % at(OUT, 1), mul8, [1, 0xdeadbeef],
   {r"\ssmull\.8h\s": 5, r"\ssqxtn2\.16b\s": 5}, ns=[0, 15, 16, 17, 100])


# -- zigbench/saturate's mix, in place: s -| (s >> 2) +| ((f >> 4) *| gain)
def mix(m, i, q):
    s, f = m.ld(A + 2 * i, 2, True), m.ld(B + 2 * i, 2, True)
    a = clamp(s - (s >> 2), 16)
    b = mulsat((f << 12) & 0xffff0000, q & M32)
    m.st(A + 2 * i, 2, clamp(a + b, 16))


MIX = ("(local.set $g (i64.extend_i32_u (local.get $q)))\n          "
       "(local.set $u (i32.load16_s %(a)s))\n          "
       + w_clamp16("(i32.sub (local.get $u) (i32.shr_s (local.get $u) (i32.const 2)))")
       + "\n          (local.set $u (local.get $t))\n          "
       + w_mulsat("(i32.and (i32.shl (i32.load16_s %(b)s) (i32.const 12)) (i32.const -65536))", "(local.get $g)", sign="p")
       + "\n          " + w_clamp16("(i32.add (local.get $u) (local.get $t))")
       + "\n          (i32.store16 %(a)s (local.get $t))") % {"a": at(A, 2), "b": at(B, 2)}
fn("mix", MIX, mix, [1, 3, 5, 32767, 70000], {r"\ssqsub\.8h\s": 5, r"\ssqadd\.8h\s": 5, r"\ssshr\.8h\s": 10,
                                                r"\ssqxtn2\.8h\s": 5}, ns=[0, 1, 8, 9, 33, 100])


# -- its filter, three taps of it: sums that stick, in the order written
TAPS = [3, -18, 70]


def filt(m, i, q):
    acc = 0
    for j, k in enumerate(TAPS):
        t = mulsat((m.ld(A + 2 * (i + j), 2, True) << 13) & 0xffff0000, k)
        acc = t if j == 0 else clamp(acc + t, 16)
    m.st(OUT + 2 * i, 2, acc)


FILT = ""
for j, k in enumerate(TAPS):
    FILT += w_mulsat("(i32.and (i32.shl (i32.load16_s offset=%d %s) (i32.const 13)) (i32.const -65536))"
                     % (2 * j, at(A, 2)), "(i64.const %d)" % k) + "\n          "
    FILT += "(local.set $u (local.get $t))\n          " if j == 0 else \
            w_clamp16("(i32.add (local.get $u) (local.get $t))") + "\n          (local.set $u (local.get $t))\n          "
FILT += "(i32.store16 %s (local.get $u))" % at(OUT, 2)
fn("filter", FILT, filt, [1, 0xdeadbeef], {r"\ssqadd\.8h\s": 10, r"\ssqxtn2\.8h\s": 15}, ns=[0, 1, 8, 9, 33, 100])


# -- a halfword shifted down with its sign, in place; and an unsigned one, whose
#    shift with the sign is not the lane's
def shrs(m, i, q):
    m.st(A + 2 * i, 2, m.ld(A + 2 * i, 2, True) >> 2)


def shrs_u(m, i, q):
    m.st(OUT + 2 * i, 2, (m.ld(A + 2 * i, 2, False) | 0x10000) >> 3)


fn("shrs", "(i32.store16 %(a)s (i32.shr_s (i32.load16_s %(a)s) (i32.const 2)))" % {"a": at(A, 2)}, shrs, [1, 9],
   {r"\ssshr\.8h\s": 5}, ns=[0, 7, 8, 9, 100])
fn("shrs_wide", "(i32.store16 %s (i32.shr_s (i32.or (i32.load16_u %s) (i32.const 0x10000)) (i32.const 3)))"
   % (at(OUT, 2), at(A, 2)), shrs_u, [1, 9], {r"\ssshr\.8h\s": 0}, ns=[0, 9, 100])


# -- what a lane holds, by what is known of a value's bits and of its sign

def w_addsat8(x, y):
    return ("(local.set $t (i32.add %s %s))\n          "
            "(local.set $t (select (local.get $t) (i32.const 255) (i32.lt_u (local.get $t) (i32.const 255))))" % (x, y))


def mk_add8(name, operand, pmodel, shapes, qs):
    def model(m, i, q):
        m.st(OUT + i, 1, min(m.ld(A + i, 1, False) + pmodel(q), 255))
    fn(name, w_addsat8("(i32.load8_u %s)" % at(A, 1), operand) + "\n          (i32.store8 %s (local.get $t))" % at(OUT, 1),
       model, qs, shapes, ns=[0, 16, 17, 100])


# the top byte of the argument: eight bits, by the shift; and a value that is 0 or 256
mk_add8("add_kb", "(i32.shr_u (local.get $q) (i32.const 24))", lambda q: (q & M32) >> 24, {r"\suqadd\.16b\s": 5},
        [0x05000001, 0xff000002, 0x80000003])
mk_add8("add_256", "(i32.and (local.get $q) (i32.const 256))", lambda q: q & 256, {r"\suqadd\.": 0}, [0x100, 0x2ff, 0x1100])


# (a -| b) +| c: the difference is the lane's value, by its form
def sub_add(m, i, q):
    x, y = m.ld(A + i, 1, False), m.ld(B + i, 1, False)
    m.st(OUT + i, 1, min((x - y if x >= y else 0) + (q & 0xff), 255))


fn("sub_add", """(local.set $t (i32.load8_u %s))
          (local.set $u (i32.sub (local.get $t) (i32.load8_u %s)))
          (local.set $u (select (i32.const 0) (local.get $u) (i32.gt_u (local.get $u) (local.get $t))))
          %s
          (i32.store8 %s (local.get $t))"""
   % (at(A, 1), at(B, 1), w_addsat8("(local.get $u)", "(i32.and (local.get $q) (i32.const 255))"), at(OUT, 1)),
   sub_add, [0x10, 0xf0, 0], {r"\suqsub\.16b\s": 5, r"\suqadd\.16b\s": 5}, ns=[0, 16, 17, 100])


# a sum with 17 bits of a value: no halfword
def add17(m, i, q):
    m.st(OUT + 2 * i, 2, clamp(m.ld(A + 2 * i, 2, True) + (s32(m.ld(B + 4 * (i & 127), 4, False)) >> 15), 16))


fn("add_17bit", w_clamp16("(i32.add (i32.load16_s %s) (i32.shr_s (i32.load (i32.add (i32.const %d) (i32.shl (i32.and (local.get $i) (i32.const 127)) (i32.const 2)))) (i32.const 15)))"
                          % (at(A, 2), B)) + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2),
   add17, [1, 0xdeadbeef], {r"\ssqadd\.": 0}, ns=[0, 9, 100])


# a signed halfword shifted down without its sign, an unsigned one with it
def shru_s(m, i, q):
    m.st(OUT + 2 * i, 2, ((m.ld(A + 2 * i, 2, True) & M32) >> 2))


def shrs_u16(m, i, q):
    m.st(OUT + 2 * i, 2, m.ld(A + 2 * i, 2, False) >> 2)


fn("shru_s16", "(i32.store16 %s (i32.shr_u (i32.load16_s %s) (i32.const 2)))" % (at(OUT, 2), at(A, 2)), shru_s, [1, 9],
   {r"\sushr\.8h\s": 0}, ns=[0, 9, 100])
fn("shrs_u16", "(i32.store16 %s (i32.shr_s (i32.load16_u %s) (i32.const 2)))" % (at(OUT, 2), at(A, 2)), shrs_u16, [1, 9],
   {r"\ssshr\.8h\s": 0}, ns=[0, 9, 100])


# -- the shift's amount: one the loop changes, and two in one trip
def shl_var(m, i, q):
    sh = i & 3
    v = (m.ld(A + i, 1, False) << 24) & M32
    w = (v << sh) & M32
    m.st(OUT + i, 1, (M32 if (w >> sh) != v else w) >> 24)


fn("shl_var", """(local.set $t (i32.shl (i32.load8_u %(a)s) (i32.const 24)))
          (local.set $u (i32.shl (local.get $t) (i32.and (local.get $i) (i32.const 3))))
          (i32.store8 %(o)s
            (i32.shr_u (select (i32.const -1) (local.get $u)
                               (i32.ne (local.get $t) (i32.shr_u (local.get $u) (i32.and (local.get $i) (i32.const 3)))))
                       (i32.const 24)))""" % {"a": at(A, 1), "o": at(OUT, 1)}, shl_var, [1, 9], {r"\suqshl\.": 0},
   ns=[0, 17, 100])


def shl64(m, i, q):
    sh = q & 63
    v = (m.ld(A + i, 1, False) << 56) & M64
    w = (v << sh) & M64
    m.st(OUT + i, 1, (M64 if (w >> sh) != v else w) >> 56)


fn("shl64", """(local.set $p (i64.shl (i64.load8_u %(a)s) (i64.const 56)))
          (local.set $g (i64.shl (local.get $p) (i64.extend_i32_u (local.get $q))))
          (i64.store8 %(o)s
            (i64.shr_u (select (i64.const -1) (local.get $g)
                               (i64.ne (local.get $p) (i64.shr_u (local.get $g) (i64.extend_i32_u (local.get $q)))))
                       (i64.const 56)))""" % {"a": at(A, 1), "o": at(OUT, 1)}, shl64, [0, 3, 7, 8, 63, 67],
   {r"\suqshl\.16b\s": 5}, ns=[0, 17, 100])


# -- per-lane multipliers of more than the lane, and two multipliers each tested
def mulvar_wide(m, i, q):
    m.st(OUT + 2 * i, 2, mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, s32((m.ld(B + 2 * i, 2, True) * 3) & M32)))


fn("mulvar_wide", w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2),
                           "(i64.extend_i32_s (i32.mul (i32.load16_s %s) (i32.const 3)))" % at(B, 2))
   + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2), mulvar_wide, [1, 0xdeadbeef], NOVEC, ns=[0, 9, 100])


def mul2inv(m, i, q):
    a = mulsat((m.ld(A + 2 * i, 2, True) << 16) & M32, q & 0xffff)
    b = mulsat((m.ld(B + 2 * i, 2, True) << 16) & M32, (q & M32) >> 16)
    m.st(OUT + 2 * i, 2, clamp(a + b, 16))


fn("mul2inv", w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(A, 2),
                       "(i64.extend_i32_u (i32.and (local.get $q) (i32.const 0xffff)))")
   + "\n          (local.set $u (local.get $t))\n          "
   + w_mulsat("(i32.shl (i32.load16_s %s) (i32.const 16))" % at(B, 2), "(i64.extend_i32_u (i32.shr_u (local.get $q) (i32.const 16)))")
   + "\n          " + w_clamp16("(i32.add (local.get $u) (local.get $t))") + "\n          (i32.store16 %s (local.get $t))" % at(OUT, 2),
   mul2inv, [0x00030005, 0x80000005, 0x00038000, 0x7fff7fff, 0xffffffff], {r"\ssqxtn2\.8h\s": 10}, ns=[0, 9, 33, 100])


# -- five taps: more loads in flight than four copies' worth, two copies
TAPS5 = [3, -18, 70, 200, 70]


def filt5(m, i, q):
    acc = 0
    for j, k in enumerate(TAPS5):
        t = mulsat((m.ld(A + 2 * (i + j), 2, True) << 13) & 0xffff0000, k)
        acc = t if j == 0 else clamp(acc + t, 16)
    m.st(OUT + 2 * i, 2, acc)


F5 = ""
for j, k in enumerate(TAPS5):
    F5 += w_mulsat("(i32.and (i32.shl (i32.load16_s offset=%d %s) (i32.const 13)) (i32.const -65536))"
                   % (2 * j, at(A, 2)), "(i64.const %d)" % k) + "\n          "
    F5 += "(local.set $u (local.get $t))\n          " if j == 0 else \
          w_clamp16("(i32.add (local.get $u) (local.get $t))") + "\n          (local.set $u (local.get $t))\n          "
F5 += "(i32.store16 %s (local.get $u))" % at(OUT, 2)
fn("filter5", F5, filt5, [1, 0xdeadbeef], {r"\ssqxtn2\.8h\s": 15, r"\ssqadd\.8h\s": 12}, ns=[0, 1, 8, 9, 33, 100])


# -- test/cases_opt/vec_narrow.wat: what a lane narrower than its values'
#    type compares, takes the lesser or greater of, and extends
NARROW = []
_all = FUNCS
FUNCS = NARROW
LD = {"u8": ("i32.load8_u", 1, False), "s8": ("i32.load8_s", 1, True),
      "u16": ("i32.load16_u", 2, False), "s16": ("i32.load16_s", 2, True)}
RELS = {"lt_s": lambda a, b: s32(a) < s32(b), "le_s": lambda a, b: s32(a) <= s32(b),
        "gt_s": lambda a, b: s32(a) > s32(b), "ge_s": lambda a, b: s32(a) >= s32(b),
        "lt_u": lambda a, b: a & M32 < b & M32, "le_u": lambda a, b: a & M32 <= b & M32,
        "gt_u": lambda a, b: a & M32 > b & M32, "ge_u": lambda a, b: a & M32 >= b & M32,
        "eq": lambda a, b: a & M32 == b & M32, "ne": lambda a, b: a & M32 != b & M32}


def mk_sel(name, ka, kb, rel, shapes):
    """out = a REL b ? a ^ 0x55 : b, on elements loaded as ka and kb"""
    (la, sz, sa), (lb, _, sb_) = LD[ka], LD[kb]

    def model(m, i, q):
        a, b = m.ld(A + sz * i, sz, sa), m.ld(B + sz * i, sz, sb_)
        m.st(OUT + sz * i, sz, (a ^ 0x55) if RELS[rel](a, b) else b)
    st = "i32.store8" if sz == 1 else "i32.store16"
    fn(name, """(local.set $t (%s %s))
          (local.set $u (%s %s))
          (%s %s (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.%s (local.get $t) (local.get $u))))"""
       % (la, at(A, sz), lb, at(B, sz), st, at(OUT, sz), rel), model, [1, 0xdeadbeef], shapes, ns=[0, 15, 16, 17, 100])


# unsigned values: every relation the lane's unsigned one
mk_sel("ltu_u8", "u8", "u8", "lt_u", {r"\scmhi\.16b\s": 5})
mk_sel("lts_u8", "u8", "u8", "lt_s", {r"\scmhi\.16b\s": 5, r"\scmgt\.": 0})
mk_sel("ges_u8", "u8", "u8", "ge_s", {r"\scmhs\.16b\s": 5, r"\scmge\.": 0})
mk_sel("geu_u16", "u16", "u16", "ge_u", {r"\scmhs\.8h\s": 5})
# signed values: each relation as it is
mk_sel("lts_s8", "s8", "s8", "lt_s", {r"\scmgt\.16b\s": 5})
mk_sel("les_s16", "s16", "s16", "le_s", {r"\scmge\.8h\s": 5})
mk_sel("ltu_s8", "s8", "s8", "lt_u", {r"\scmhi\.16b\s": 5})
mk_sel("gtu_s16", "s16", "s16", "gt_u", {r"\scmhi\.8h\s": 5})
mk_sel("eq_s8", "s8", "s8", "eq", {r"\scmeq\.16b\s": 5})
mk_sel("ne_u16", "u16", "u16", "ne", {r"\scmeq\.8h\s": 5})
# one of each: no byte lane's compare (a lane twice as wide holds both)
mk_sel("lts_mix", "s8", "u8", "lt_s", {r"\scm(gt|hi)\.16b\s": 0})
mk_sel("ltu_mix", "u8", "s8", "lt_u", {r"\scm(gt|hi)\.16b\s": 0})
mk_sel("eq_mix", "s8", "u8", "eq", {r"\scmeq\.16b\s": 0})


def mk_mm(name, ka, kb, rel, first, shapes):
    """the lesser or the greater of a and b: select(a, b, a REL b), or of b and a"""
    (la, sz, sa), (lb, _, sb_) = LD[ka], LD[kb]

    def model(m, i, q):
        a, b = m.ld(A + sz * i, sz, sa), m.ld(B + sz * i, sz, sb_)
        m.st(OUT + sz * i, sz, a if RELS[rel](a, b) else b)
    st = "i32.store8" if sz == 1 else "i32.store16"
    fn(name, """(local.set $t (%s %s))
          (local.set $u (%s %s))
          (%s %s (select (local.get $t) (local.get $u) (i32.%s (local.get $t) (local.get $u))))"""
       % (la, at(A, sz), lb, at(B, sz), st, at(OUT, sz), rel), model, [1, 0xdeadbeef], shapes, ns=[0, 15, 16, 17, 100])


mk_mm("umin_u8", "u8", "u8", "lt_u", True, {r"\sumin\.16b\s": 5})
mk_mm("smin_u8", "u8", "u8", "lt_s", True, {r"\sumin\.16b\s": 5, r"\ssmin\.": 0})
mk_mm("smax_u16", "u16", "u16", "gt_s", True, {r"\sumax\.8h\s": 5, r"\ssmax\.": 0})
mk_mm("smin_s8", "s8", "s8", "lt_s", True, {r"\ssmin\.16b\s": 5})
mk_mm("smax_s16", "s16", "s16", "ge_s", True, {r"\ssmax\.8h\s": 5})
mk_mm("umax_s8", "s8", "s8", "gt_u", True, {r"\sumax\.16b\s": 5})
mk_mm("umin_s16", "s16", "s16", "le_u", True, {r"\sumin\.8h\s": 5})
mk_mm("smin_mix", "s8", "u8", "lt_s", True, {r"\s[su]min\.16b\s": 0})
mk_mm("umax_mix", "u8", "s8", "gt_u", True, {r"\s[su]max\.16b\s": 0})


# a sign extension from bit n: of a sum of halfwords from bit 8, and of bytes
# from bit 8 and from bit 16, which a byte's lane has nothing above
def ext8_16(m, i, q):
    m.st(OUT + 2 * i, 2, sx(m.ld(A + 2 * i, 2, False) + m.ld(B + 2 * i, 2, False), 8))


def ext_b(bits):
    def model(m, i, q):
        m.st(OUT + i, 1, sx(m.ld(A + i, 1, False) + m.ld(B + i, 1, False), bits))
    return model


fn("ext8_h", "(i32.store16 %s (i32.extend8_s (i32.add (i32.load16_u %s) (i32.load16_u %s))))" % (at(OUT, 2), at(A, 2), at(B, 2)),
   ext8_16, [1, 0xdeadbeef], {r"\sshl\.8h\s": 5, r"\ssshr\.8h\s": 5}, ns=[0, 7, 8, 9, 100])
fn("ext8_b", "(i32.store8 %s (i32.extend8_s (i32.add (i32.load8_u %s) (i32.load8_u %s))))" % (at(OUT, 1), at(A, 1), at(B, 1)),
   ext_b(8), [1, 0xdeadbeef], {r"\sadd\.16b\s": 5, r"\ssshr\.": 0}, ns=[0, 15, 16, 17, 100])
fn("ext16_b", "(i32.store8 %s (i32.extend16_s (i32.add (i32.load8_u %s) (i32.load8_u %s))))" % (at(OUT, 1), at(A, 1), at(B, 1)),
   ext_b(16), [1, 0xdeadbeef], {r"\sadd\.16b\s": 5, r"\ssshr\.": 0}, ns=[0, 15, 16, 17, 100])
fn("ext16_h", "(i32.store16 %s (i32.extend16_s (i32.add (i32.load16_u %s) (i32.load16_u %s))))" % (at(OUT, 2), at(A, 2), at(B, 2)),
   lambda m, i, q: m.st(OUT + 2 * i, 2, m.ld(A + 2 * i, 2, False) + m.ld(B + 2 * i, 2, False)), [1, 0xdeadbeef],
   {r"\sadd\.8h\s": 5, r"\ssshr\.": 0}, ns=[0, 7, 8, 9, 100])
# (a product of two halfwords, which no fact says is its low 32 bits extended)
fn("ext32_h", "(i64.store16 %s (i64.extend32_s (i64.mul (i64.load16_u %s) (i64.load16_u %s))))" % (at(OUT, 2), at(A, 2), at(B, 2)),
   lambda m, i, q: m.st(OUT + 2 * i, 2, m.ld(A + 2 * i, 2, False) * m.ld(B + 2 * i, 2, False)), [1, 0xdeadbeef],
   {r"\smul\.8h\s": 5, r"\ssshr\.": 0}, ns=[0, 7, 8, 9, 100])
FUNCS = _all

HEAD_NARROW = """;; What a lane narrower than its values' type compares, takes the lesser
;; or the greater of, and extends (opt.c v_mask and v_build): the lane's own
;; compare where both values are the lane's by ir_known_bits or ir_sign_ge.
;; Each function fills two arrays by its second argument, runs its loop over
;; the first n elements and hashes the memory.  From test/gen_vec_sat.py.
;;
;;   ltu_u8 lts_u8 ges_u8 geu_u16
;;               unsigned bytes and halfwords: every relation the lane's
;;               unsigned one, a signed relation too -- none is negative
;;   lts_s8 les_s16 ltu_s8 gtu_s16 eq_s8 ne_u16
;;               signed ones: each relation as it is, the unsigned ones too
;;   lts_mix ltu_mix eq_mix
;;               a signed byte against an unsigned one: no byte's compare
;;   umin_u8 smin_u8 smax_u16 smin_s8 smax_s16 umax_s8 umin_s16
;;               the lesser or the greater: the lane's min or max, the
;;               unsigned one for unsigned values whatever the relation
;;   smin_mix umax_mix
;;               of a signed byte and an unsigned one: no byte's min or max
;;   ext8_h      a halfword's sum extended from bit 8: a shift up and down
;;   ext8_b ext16_b ext16_h ext32_h
;;               a byte's sum extended from bit 8 or 16, a halfword's from
;;               bit 16 and a product of two, made in 64 bits, from bit 32:
;;               the lane as it is
;;
"""

# -- with no folding, an and that clears nothing stays: the value shifted up
#    by more than the lane leaves room for, which is no lane's value shifted
#    down (test/cases_opt/vec_sat_nofold.wat, compiled with -fno-fold too)
NOFOLD = []
_all = FUNCS
FUNCS = NOFOLD
mk_mulk("mul_sh20", 70, {}, top="(i32.and (i32.shl (i32.load16_s %s) (i32.const 20)) (i32.const -65536))" % at(A, 2),
        pmodel=lambda x: (x << 20) & 0xffff0000, ns=[0, 9, 33, 100])
FUNCS = _all


def write(path, head, funcs_in):
    funcs, shapes, runs = [], [], []
    for name, body, run, rs, shape in funcs_in:
        funcs.append('  (func $%s (export "%s") (param $n i32) (param $q i32) (result i64)\n'
                     "    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)\n%s)\n"
                     % (name, name, body))
        for pat, n in shape.items():
            shapes.append(";; shape: %s %s=%d" % (name, pat, n))
        for n, q in rs:
            runs.append(";; run: %s %d %d => i64:%d" % (name, n, q, s64(run(n, q))))
    with open(path, "w") as f:
        f.write(head + "\n".join(shapes) + "\n;;\n" + "\n".join(runs) + "\n\n(module\n  (memory 1)\n" + HELPERS +
                "".join(funcs) + ")\n")
    print("%s: %d functions, %d runs" % (path, len(funcs_in), len(runs)))


def main(path):
    write(path, HEAD % {"VEC": VECTORISED}, FUNCS)
    write(path.replace(".wat", "_nofold.wat"), HEAD_NOFOLD, NOFOLD)
    write(path.replace("vec_sat.wat", "vec_narrow.wat"), HEAD_NARROW, NARROW)


HEAD_NOFOLD = """;; test/cases_opt/vec_sat.wat's saturating product with nothing folded: an
;; and that clears no bit stays, of a halfword shifted up by 20, more than
;; the 16 that put it at the top of its i32 -- no lane's value shifted
;; down, and no saturating product in the lanes (opt.c v_top_lane).  From
;; test/gen_vec_sat.py.
;;
;; flags: -O2 -fno-fold
"""

# the loops made vector loops, the module compiled alone
VECTORISED = 32

HEAD = """;; Saturating arithmetic on bytes and halfwords as LLVM writes it for wasm,
;; which has no such types, vectorised in the elements' own lanes (opt.c
;; v_sat_form, v_shlsat, v_mulsat, v_guard; a lane's fit by ir_known_bits and
;; ir_sign_ge).  Each function fills two arrays by its second argument,
;; runs its loop over the first n elements and hashes the memory.  From
;; test/gen_vec_sat.py, which works out every answer.
;;
;;   tone        bytes in place, (p +| b) -| d <<| s as zigbench/saturate
;;               has them: uqadd, uqsub and uqshl, the shift any amount
;;   shl16       the shift on halfwords, its select the other way round and
;;               its bound ones only where the way down keeps them
;;   shlshort    the bound a bit short of the lane's ones: no uqshl
;;   sub_le sub_lt sub_ge
;;               a difference kept where it did not wrap, each way of
;;               writing the test: uqsub
;;   sub_wrong sub_wrong2
;;               tests that are none of a wrap: as it was
;;   shl_down25 shl_up23 shl_back
;;               the shift's form with one constant a step off, or undone
;;               by another amount
;;   thru        a byte's sum made in 64 bits and narrowed, and one widened
;;               to be stored: the lane's own
;;   mul_down15 mul_cmp30 mul_half31 mul_sg30 mul_sg62 mul_top15 mul_mask17
;;               the product's form with one constant a step off
;;   mul_*       a halfword times a constant, held to its range: the
;;               widening multiply narrowed with saturation, for a constant
;;               in the lane's range -- the product by 3, 7 and 16 a shift
;;               and an add, a subtract, a shift alone -- and not past it
;;   mul_sh3     the value shifted down three first, (s >> 3) *| k
;;   mul_sh3u    the same of an unsigned halfword: no lane's shift
;;   mul_bound   the bound of another range than the lane's
;;   mulinv_u mulinv_s
;;               times a value the loop does not change, tested before the
;;               loop: in the lane's range the vector loop, past it the
;;               scalar one
;;   mulvar      times the other array's halfword, lane by lane
;;   mul8        a byte's signed product
;;   mix         saturate's mix, in place: a saturating difference, a
;;               product and their saturating sum
;;   filter      three taps of its filter, the sums sticking in the order
;;               written
;;   shrs        a halfword shifted down with its sign
;;   shrs_wide   17 bits shifted down with their sign: no lane's shift
;;   add_kb add_256
;;               a byte's saturating sum with the argument's top byte, eight
;;               bits by its shift, and with a value that is 0 or 256
;;   sub_add     (a -| b) +| c: the difference is the lane's value
;;   add_17bit   a halfword's saturating sum with 17 bits: none made
;;   shru_s16 shrs_u16
;;               a signed halfword shifted down without its sign, and an
;;               unsigned one with it: neither the lane's shift
;;   shl_var     the saturating shift by an amount the loop changes
;;   shl64       the shift written on an i64
;;   mulvar_wide the per-lane multiplier three times a halfword
;;   mul2inv     two products, each by its own value tested before the loop
;;   filter5     five taps: two copies, for the loads in flight
;;
;; stats -O2 a64: vectorised=%(VEC)d
;;
"""

if __name__ == "__main__":
    main(sys.argv[1])
