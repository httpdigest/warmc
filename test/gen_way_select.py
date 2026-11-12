"""test/cases_opt/way_select.wat: a select whose condition each way into
its block decides is a parameter of the block, handed the one operand or the
other on each edge (opt.c pass_bit_facts, the selects among what the ways in
decide).  The module's header says what each function is; every answer is
worked out here.

  python3 test/gen_way_select.py test/cases_opt/way_select.wat
"""
import sys

M32 = (1 << 32) - 1
MEM = bytes([7, 9, 3, 200, 1, 5, 0, 11, 13, 0, 255, 2, 4, 6, 8, 0] + [0] * 16 +
            [7, 7, 7, 0, 7, 7, 9, 7] + [0] * 8 +                      # 32: holes' sevens
            [7, 0, 0, 1, 9, 0, 0, 0, 1, 0, 1, 2] + [0] * 4)           # 48: wide's halfwords


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


# A scan of bytes that ends at a zero or at the byte looked for, and what it
# found picked by the byte: strstr's `*h ? h - 1 : 0`.
FIND = """  (func $find (export "find") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
"""


def find(p, k):
    while True:
        c = MEM[p]
        if c == 0:
            return -1
        if c == k:
            return p
        p += 1


# Three ways in, the first and the last finding the byte not zero, and
# what each picked a value made before the loop.
THREE = """  (func $three (export "three") (param $p i32) (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $x i32) (local $y i32)
    (local.set $x (i32.mul (local.get $a) (i32.const 3)))
    (local.set $y (i32.xor (local.get $b) (i32.const 0x5a)))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eq (local.get $c) (i32.const 200)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 250)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $p) (select (local.get $x) (local.get $y) (local.get $c))))
"""


def three(p, a, b):
    x, y = (a * 3) & M32, (b ^ 0x5a) & M32
    while True:
        c = MEM[p]
        if c == 0 or c == 200 or c > 250:
            return (p + (x if c else y)) & M32
        p += 1


# One way out is taken before the byte is loaded: what the last trip left
# there, or the 0 it starts as, and the select stays.
OPEN = """  (func $open (export "open") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $p) (local.get $k)))
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
"""


def open_(p, k):
    c = 0
    while True:
        if p >= k:
            return p if c else -1
        c = MEM[p]
        if c == 0:
            return -1
        p += 1


# What the select picks is made in its own block: not there on the ways in.
INBLOCK = """  (func $inblock (export "inblock") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.load8_u offset=1 (local.get $p)) (i32.load8_u offset=2 (local.get $p)) (local.get $c)))
"""


def inblock(p, k):
    while True:
        c = MEM[p]
        if c == 0 or c == k:
            return MEM[p + 1] if c else MEM[p + 2]
        p += 1


# Every way says the same: the one operand.
AGREE = """  (func $agree (export "agree") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eq (local.get $c) (i32.const 11)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 100)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (local.get $k) (local.get $c)))
"""


def agree(p, k):
    while True:
        c = MEM[p]
        if c == 11 or c > 100:
            return p
        p += 1


# The second operand alone is made in the select's block.
INBLOCK2 = INBLOCK.replace("$inblock", "$inblock2").replace('"inblock"', '"inblock2"').replace(
    "(select (i32.load8_u offset=1 (local.get $p)) (i32.load8_u offset=2 (local.get $p)) (local.get $c))",
    "(select (local.get $k) (i32.load8_u offset=2 (local.get $p)) (local.get $c))")
INBLOCK1 = INBLOCK.replace("$inblock", "$inblock1").replace('"inblock"', '"inblock1"').replace(
    "(select (i32.load8_u offset=1 (local.get $p)) (i32.load8_u offset=2 (local.get $p)) (local.get $c))",
    "(select (i32.load8_u offset=1 (local.get $p)) (local.get $k) (local.get $c))")


def inblock2(p, k):
    while True:
        c = MEM[p]
        if c == 0 or c == k:
            return k if c else MEM[p + 2]
        p += 1


def inblock1(p, k):
    while True:
        c = MEM[p]
        if c == 0 or c == k:
            return MEM[p + 1] if c else k
        p += 1


# A way out on `c != 7` leaves the byte 0 to 6 or over 7: a set of two
# ranges with the zero in it, which decides nothing.
HOLES = """  (func $holes (export "holes") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.ne (local.get $c) (i32.const 7)))
        (br_if $out (i32.ge_u (local.get $p) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
"""
def holes(p, k):
    while True:
        c = MEM[p]
        if c != 7 or p >= k:
            return p if c else -1
        p += 1


# A halfword found over 255, before any test of it for zero: not zero, by
# its high byte alone.
WIDE = """  (func $wide (export "wide") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load16_u (local.get $p)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 255)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $p (i32.add (local.get $p) (i32.const 2)))
        (br $l)))
    (select (local.get $p) (local.get $k) (local.get $c)))
"""
def wide(p, k):
    while True:
        c = MEM[p] | MEM[p + 1] << 8
        if c > 255 or c == 0:
            return p if c else k
        p += 2


# Both operands constants of the select's block, the ways in from the two
# arms of a branch, neither above the other: each way has its own.
CONSTS = """  (func $consts (export "consts") (param $p i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (if (i32.and (local.get $p) (i32.const 1))
          (then (i32.store8 (i32.const 100) (local.get $c))
                (br_if $out (i32.eqz (local.get $c))))
          (else (i32.store8 (i32.const 101) (local.get $c))
                (br_if $out (i32.gt_u (local.get $c) (i32.const 12)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $p) (select (i32.const 7777) (i32.const 99999) (local.get $c))))
"""


def consts(p):
    while True:
        c = MEM[p]
        if (c == 0) if p & 1 else (c > 12):
            return p + (7777 if c else 99999)
        p += 1


# The ways agree, and what they pick is made in the select's block.
AGREE_IN = INBLOCK.replace("$inblock", "$agree_in").replace('"inblock"', '"agree_in"').replace(
    "(br_if $out (i32.eqz (local.get $c)))", "(br_if $out (i32.gt_u (local.get $c) (i32.const 100)))").replace(
    "(i32.eq (local.get $c) (local.get $k))", "(i32.eq (local.get $c) (i32.const 11))")


def agree_in(p, k):
    while True:
        c = MEM[p]
        if c > 100 or c == 11:
            return MEM[p + 1]
        p += 1


# One way in: an arm of a branch on the value itself.
ONEWAY = """  (func $oneway (export "oneway") (param $c i32) (param $a i32) (param $b i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.store (i32.const 60) (local.get $a))
            (select (local.get $a) (local.get $b) (local.get $c)))
      (else (i32.store (i32.const 60) (local.get $b))
            (i32.add (select (local.get $a) (local.get $b) (local.get $c)) (i32.const 1)))))
"""


def oneway(c, a, b):
    return a if c else b + 1


def ways(n):
    """n ways out of a scan, each a byte it stops at; the last on a zero"""
    stops = [3, 200, 1, 5, 11, 13, 255, 2, 4, 6][:n - 1]
    body = "".join("        (br_if $out (i32.eq (local.get $c) (i32.const %d)))\n" % k for k in stops)
    name = "ways%d" % n
    wat = ("  (func $%s (export \"%s\") (param $p i32) (param $a i32) (result i32)\n    (local $c i32)\n"
           "    (block $out\n      (loop $l\n        (local.set $c (i32.load8_u (local.get $p)))\n"
           "        (br_if $out (i32.eqz (local.get $c)))\n%s"
           "        (local.set $p (i32.add (local.get $p) (i32.const 1)))\n        (br $l)))\n"
           "    (i32.xor (select (local.get $p) (local.get $a) (local.get $c)) (i32.const 0x1000)))\n"
           % (name, name, body))

    def model(p, a):
        while True:
            c = MEM[p]
            if c == 0 or c in stops:
                return ((p if c else a) ^ 0x1000) & M32
            p += 1
    return name, wat, model


SCAN = """  (func $%(n)s (export "%(n)s") (param $p i32) (param $k i32) (result i32)
    (local $c i32)%(lo)s
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    %(tail)s)
"""


def scan_end(p, k):
    """where the scan ends, and whether at the byte looked for"""
    while True:
        c = MEM[p]
        if c == 0 or c == k:
            return p, c != 0
        p += 1


# What the select picks is made in its block for it alone, of a value from
# above and a constant: made on the way that picks it (gobench/unicode's
# search of a range table, `lo = mid + 1` picked by a test the branch before
# has made).
MADE = SCAN % dict(n="made", lo="", tail="(select (i32.add (local.get $p) (i32.const 100)) (local.get $k) (local.get $c))")


def made(p, k):
    q, hit = scan_end(p, k)
    return q + 100 if hit else k


# Both operands are.
MADEBOTH = SCAN % dict(n="madeboth", lo="", tail="(select (i32.shl (local.get $p) (i32.const 3)) "
                       "(i32.xor (local.get $k) (i32.const 0x1234)) (local.get $c))")


def madeboth(p, k):
    q, hit = scan_end(p, k)
    return q << 3 if hit else k ^ 0x1234


# Read once more beside the select: it stays in the block, and the select with it.
MADE2 = SCAN % dict(n="made2", lo=" (local $x i32)",
                    tail="(local.set $x (i32.sub (local.get $p) (i32.const 3)))\n    "
                         "(i32.mul (select (local.get $x) (local.get $k) (local.get $c)) (local.get $x))")


def made2(p, k):
    q, hit = scan_end(p, k)
    return ((q - 3 if hit else k) * (q - 3)) & M32


# Made of a value the block itself loads: not there on the ways in.
MADEIN = SCAN % dict(n="madein", lo="",
                     tail="(select (i32.add (i32.load8_u offset=1 (local.get $p)) (i32.const 50)) "
                          "(local.get $k) (local.get $c))")


def madein(p, k):
    q, hit = scan_end(p, k)
    return MEM[q + 1] + 50 if hit else k


# A multiplication is not made on a way that may not want it.
MADEMUL = SCAN % dict(n="mademul", lo="",
                      tail="(select (i32.mul (local.get $p) (i32.const 1000003)) (local.get $k) (local.get $c))")


def mademul(p, k):
    q, hit = scan_end(p, k)
    return (q * 1000003) & M32 if hit else k


# A way in ends in a call a handler stands behind: the constant that way
# picks is made ahead of the call, which stays its block's last, and so is
# an operand made in the select's block; one made of a call's result is made
# on the way that begins after that call.
EHWAY = """  (func $%(n)s (export "%(n)s") (param $c i32) (param $a i32) (result i32)
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220))))
          (else (i32.store8 (i32.const 230) (local.get $a))))
        (return (select %(pick)s (local.get $a) (local.get $c)))))
    (i32.const -7))
"""
EHCONST = EHWAY % dict(n="ehconst", pick="(i32.const 77)")
EHMADE = EHWAY % dict(n="ehmade", pick="(i32.add (local.get $a) (i32.const 9))")


EHRESULT = """  (func $ehresult (export "ehresult") (param $c i32) (param $a i32) (result i32)
    (local $r i32)
    (block $h
      (try_table (catch_all $h)
        (local.set $r (call_indirect (type $t2) (local.get $a) (i32.load8_u (i32.const 221))))
        (if (local.get $c) (then (i32.store8 (i32.const 231) (local.get $a))))
        (return (select (local.get $a) (i32.add (local.get $r) (i32.const 9)) (local.get $c)))))
    (i32.const -7))
"""


def ehresult(c, a):
    return -7 if a == 3 else a if c else (a * 5 + 9) & M32


def ehconst(c, a):
    return -7 if c == 3 else 77 if c else a


def ehmade(c, a):
    return -7 if c == 3 else a + 9 if c else a


CSEL = r"\scs(el|inv|inc|neg)\s"


def main(path):
    funcs, shapes, runs = [], [], []

    def add(name, wat, model, argsets, csels):
        funcs.append(wat)
        shapes.append(";; shape: %s %s=%d" % (name, CSEL, csels))
        for args in argsets:
            runs.append(";; run: %s %s => i32:%d" % (name, " ".join(str(a) for a in args), s32(model(*args))))

    add("find", FIND, find, [(0, 3), (0, 200), (0, 77), (2, 5), (7, 13), (7, 99), (6, 1), (10, 2)], 0)
    add("three", THREE, three, [(0, 5, 9), (4, 5, 9), (7, 100, 3), (10, 1, 2), (11, 7, 0xff)], 0)
    add("open", OPEN, open_, [(0, 3), (0, 100), (4, 4), (6, 0), (7, 8), (10, 30)], 1)
    add("inblock", INBLOCK, inblock, [(0, 3), (0, 77), (2, 5), (7, 99), (10, 4)], 1)
    add("inblock2", INBLOCK2, inblock2, [(0, 3), (0, 77), (2, 5), (7, 99), (10, 4)], 1)
    add("inblock1", INBLOCK1, inblock1, [(0, 3), (0, 77), (2, 5), (7, 99), (10, 4)], 1)
    add("holes", HOLES, holes, [(32, 99), (32, 33), (36, 99), (36, 37), (39, 99), (35, 99)], 1)
    add("wide", WIDE, wide, [(48, 5), (50, 6), (52, 7), (56, 8)], 0)
    add("consts", CONSTS, consts, [(0,), (4,), (5,), (7,), (11,), (12,)], 0)
    add("agree_in", AGREE_IN, agree_in, [(0, 3), (0, 9), (4, 5), (7, 13), (8, 2)], 0)
    add("oneway", ONEWAY, oneway, [(0, 5, 9), (1, 5, 9), (256, 3, 4), (0x80000000, 1, 2)], 0)
    add("agree", AGREE, agree, [(0, 5), (4, 9), (7, 1), (8, 2)], 0)
    n8, w8, m8 = ways(8)
    add(n8, w8, m8, [(0, 77), (4, 5), (7, 9), (10, 1), (11, 3), (12, 8)], 0)
    n9, w9, m9 = ways(9)
    add(n9, w9, m9, [(0, 77), (4, 5), (7, 9), (10, 1), (11, 3), (12, 8)], 1)
    scans = [(0, 3), (0, 77), (2, 5), (7, 99), (10, 4)]
    add("made", MADE, made, scans, 0)
    add("madeboth", MADEBOTH, madeboth, scans, 0)
    add("made2", MADE2, made2, scans, 1)
    add("madein", MADEIN, madein, scans, 1)
    add("mademul", MADEMUL, mademul, scans, 1)
    add("ehconst", EHCONST, ehconst, [(0, 5), (1, 5), (3, 5), (2, 0x80000001)], 0)
    add("ehmade", EHMADE, ehmade, [(0, 5), (1, 5), (3, 5), (2, 0x80000001)], 0)
    add("ehresult", EHRESULT, ehresult, [(0, 5), (1, 5), (0, 3), (1, 3), (0, 0x80000001)], 0)
    data = "".join("\\%02x" % b for b in MEM)
    with open(path, "w") as f:
        f.write(HEAD + "\n".join(shapes) + "\n;;\n" + "\n".join(runs) +
                "\n\n(module\n  (type $t1 (func (param i32)))\n  (type $t2 (func (param i32) (result i32)))\n"
                "  (memory 1)\n  (table 2 funcref)\n"
                "  (elem (i32.const 0) $thrower $five)\n  (tag $e)\n  (data (i32.const 0) \"%s\")\n" % data +
                "  (data (i32.const 221) \"\\01\")\n"
                "  ;; throw at 3; called through the table at an index read from memory, which keeps them out of line\n"
                "  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))\n"
                "  (func $five (type $t2) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e)))\n"
                "    (i32.mul (local.get 0) (i32.const 5)))\n" +
                "".join(funcs) + ")\n")
    print("%s: %d functions, %d runs" % (path, len(funcs), len(runs)))


HEAD = """;; A select whose condition each way into its block decides is what that
;; way picked: a parameter of the block, handed the one operand or the other
;; on each edge (opt.c pass_bit_facts).  Each function scans bytes and stops
;; several ways, then picks by the byte it stopped at.  From
;; test/gen_way_select.py, which works out every answer.
;;
;;   find      two ways out, at a zero and at the byte looked for: the one
;;             hands on -1, a constant made in the select's block, the other
;;             the place
;;   three     three ways, the operands values made before the loop
;;   open      one way out is taken before the byte is loaded: the select
;;             stays
;;   inblock   the operands are loads made in the select's block, which no
;;             way in has: it stays
;;   inblock1 inblock2
;;             one of the two is
;;   holes     a way out on `c != 7`: the byte is 0 to 6 or over 7 there,
;;             zero among them, and the select stays
;;   wide      a halfword found over 255 before it is tested for zero: not
;;             zero, by its high byte alone
;;   consts    both operands constants of the block, the ways in from two
;;             arms of a branch
;;   agree_in  the ways agree on an operand made in the block: that one
;;   oneway    one way in, an arm of a branch on the value: the operand
;;   agree     both ways find the byte not zero: the operand
;;   ways8     eight ways in, as many as are looked at
;;   ways9     nine: the select stays
;;   made      what one way picks is made in the select's block for it
;;             alone, of a value from above and a constant: made on that
;;             way
;;   madeboth  what each picks is
;;   made2     read once more beside the select: it stays, and the select
;;   madein    made of a byte the block loads: it stays
;;   mademul   a multiplication: it stays
;;   ehconst   a way in ends in a call a handler stands behind: the
;;             constant it picks is made ahead of that call
;;   ehmade    and what it picks, made in the select's block, is too
;;   ehresult  what one way picks is made of a call's result: on the way
;;             that begins after the call
;;
;; stats -O2: way_selects=14 way_select_made=5
;; stats -O2 -fno-fold: way_selects=0
"""

if __name__ == "__main__":
    main(sys.argv[1])
