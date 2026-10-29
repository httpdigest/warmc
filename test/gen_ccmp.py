"""test/cases_opt/ccmp_chain.wat: ands and ors of compares, which arm64
makes as cmp and ccmp (isel.c find_cmp_chains, isel_ops.inc emit_cmp_chain)
-- each tested by a select, by a branch and as a 0 or 1 of its own -- and
the near misses that keep their csets.  Every answer is worked out here,
from the same lists the code is made from.

  python3 test/gen_ccmp.py test/cases_opt/ccmp_chain.wat
"""
import itertools
import struct
import sys

M32, M64 = (1 << 32) - 1, (1 << 64) - 1
# the parameters every function takes: four i32, two i64, two f64
PARAMS = [("a", "i32"), ("b", "i32"), ("c", "i32"), ("d", "i32"),
          ("x", "i64"), ("y", "i64"), ("p", "f64"), ("q", "f64")]
NAN = float("nan")


def signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


# a leaf: (op, type, l, r) with l and r a parameter's name or an int; tst
# is eqz of l & r
CMPS = {
    "eq": lambda l, r, w: l == r, "ne": lambda l, r, w: l != r,
    "lt_s": lambda l, r, w: signed(l, w) < signed(r, w), "lt_u": lambda l, r, w: l < r,
    "gt_s": lambda l, r, w: signed(l, w) > signed(r, w), "gt_u": lambda l, r, w: l > r,
    "le_s": lambda l, r, w: signed(l, w) <= signed(r, w), "le_u": lambda l, r, w: l <= r,
    "ge_s": lambda l, r, w: signed(l, w) >= signed(r, w), "ge_u": lambda l, r, w: l >= r,
}
FCMPS = {"lt": lambda l, r: l < r, "gt": lambda l, r: l > r, "le": lambda l, r: l <= r,
         "ge": lambda l, r: l >= r, "eq": lambda l, r: l == r, "ne": lambda l, r: not (l == r)}


def leaf(op, ty, l, r=None):
    return ("leaf", op, ty, l, r)


def wat_operand(o, ty):
    return "(local.get $%s)" % o if isinstance(o, str) else "(%s.const %d)" % (ty, o)


def wat(e):
    if e[0] in ("and", "or", "xor"):
        return "(i32.%s %s %s)" % (e[0], wat(e[1]), wat(e[2]))
    _, op, ty, l, r = e
    if op == "eqz":
        return "(%s.eqz %s)" % (ty, wat_operand(l, ty))
    if op == "tst":
        return "(%s.eqz (%s.and %s %s))" % (ty, ty, wat_operand(l, ty), wat_operand(r, ty))
    return "(%s.%s %s %s)" % (ty, op, wat_operand(l, ty), wat_operand(r, ty))


def value(o, ty, env):
    if isinstance(o, str):
        return env[o]
    return o & (M32 if ty == "i32" else M64)


def ev(e, env):
    if e[0] in ("and", "or", "xor"):
        a, b = ev(e[1], env), ev(e[2], env)
        return {"and": a & b, "or": a | b, "xor": a ^ b}[e[0]]
    _, op, ty, l, r = e
    if ty == "f64":
        return int(FCMPS[op](env[l], env[r]))
    w = 32 if ty == "i32" else 64
    lv = value(l, ty, env)
    if op == "eqz":
        return int(lv == 0)
    rv = value(r, ty, env)
    if op == "tst":
        return int(lv & rv == 0)
    return int(CMPS[op](lv, rv, w))


def leaves(e):
    if e[0] in ("and", "or", "xor"):
        return leaves(e[1]) + leaves(e[2])
    return [e]


def pool(e, name, ty):
    """values of a parameter: round each constant it is compared with, and
    the edges of its type"""
    if ty == "f64":
        return [1.5, 2.0, -0.0, NAN]
    w = 32 if ty == "i32" else 64
    m = (1 << w) - 1
    vs = {0, 1, m, 1 << (w - 1), (1 << (w - 1)) - 1}
    for lf in leaves(e):
        _, op, lty, l, r = lf
        if l == name and isinstance(r, int):
            vs |= {(r - 1) & m, r & m, (r + 1) & m}
            if op == "tst":
                vs |= {r & m, (~r) & m}
        if r == name and isinstance(l, int):
            vs |= {(l - 1) & m, l & m, (l + 1) & m}
    return sorted(vs)


def runs(e, n=8):
    """argument lists that between them make each compare go both ways, and
    as many of the ways the compares can go together as n lists take"""
    used = sorted({o for lf in leaves(e) for o in (lf[3], lf[4]) if isinstance(o, str)})
    types = dict(PARAMS)
    pools = [pool(e, u, types[u]) for u in used]
    seen, picked = set(), []
    for vals in itertools.product(*pools):
        env = dict(zip(used, vals))
        key = tuple(ev(lf, env) for lf in leaves(e))
        if key in seen:
            continue
        seen.add(key)
        picked.append(env)
    # each outcome of the whole, then the most varied first
    picked.sort(key=lambda env: (ev(e, env), sorted(env.items(), key=str)))
    out, outs = [], set()
    for env in picked:
        if len(out) >= n:
            break
        if ev(e, env) not in outs or len(out) < n - 1:
            out.append(env)
            outs.add(ev(e, env))
    return out


def arg_text(env, name, ty):
    v = env.get(name, 0)
    if ty == "f64":
        return "0x%016x" % struct.unpack("<Q", struct.pack("<d", v))[0]
    return str(v)


# name, expression, comment
I = "i32"
CHAINS = [
    ("eqz_and", ("and", leaf("eq", I, "a", "b"), leaf("eqz", I, "c")),
     "musl decfloat's k == a && !x[k]: a register compare, then eqz as ccmp #0"),
    ("swap", ("or", leaf("gt_u", I, "b", 1000), leaf("lt_s", I, "a", 5)),
     "the ccmp the compare whose constant takes no register: lt_s a, 5, the first operand"),
    ("ccmn", ("and", leaf("ge_s", I, "b", 100000), leaf("ne", I, "a", -3)),
     "a negative constant: ccmn #3; 100000 a register in the cmp"),
    ("imm31", ("or", leaf("eqz", I, "a"), leaf("eq", I, "b", 31)),
     "ccmp #31, the largest immediate"),
    ("big", ("and", leaf("lt_u", I, "a", 32), leaf("gt_s", I, "b", -32)),
     "no 5-bit immediate either way: -32 made in a register"),
    ("nest2", ("and", ("or", leaf("eq", I, "a", 1), leaf("eq", I, "a", 2)), leaf("ne", I, "b", "c")),
     "a chain the first side of a chain"),
    ("nest4", ("or", ("or", ("or", leaf("eq", I, "a", 1), leaf("eq", I, "a", 3)),
                      leaf("lt_u", I, "b", 7)), leaf("ge_s", I, "c", 15)),
     "four compares, CCH_MAX"),
    ("nest5", ("and", ("or", ("or", ("or", leaf("eq", I, "a", 1), leaf("ne", I, "b", 3)),
                              leaf("eq", I, "a", 7)), leaf("eq", I, "c", 15)),
               leaf("ne", I, "b", "d")),
     "five: the four first a chain, the and of it and the fifth an and"),
    ("wide", ("or", leaf("eqz", "i64", "x"), leaf("gt_u", "i64", "y", 9)),
     "i64 compares"),
    ("mixed", ("and", leaf("lt_s", "i64", "x", "y"), leaf("eq", I, "a", "b")),
     "an i64 compare and an i32 one"),
    ("wneg", ("and", leaf("ne", "i64", "x", -1), leaf("le_s", "i64", "y", -31)),
     "i64 ccmn"),
    ("flt", ("and", leaf("lt", "f64", "p", "q"), leaf("eq", I, "a", "b")),
     "an fcmp first"),
    ("fltr", ("or", leaf("eq", I, "a", "b"), leaf("ne", "f64", "p", "q")),
     "an fcmp second, made first: f64.ne holds where p or q is a NaN"),
    ("fltle", ("and", leaf("le", "f64", "p", "q"), leaf("ge_u", I, "c", "d")),
     "fcmp's ls, which fails on a NaN"),
    ("tstl", ("and", leaf("tst", I, "a", 8), leaf("eq", I, "b", "c")),
     "tst first"),
    ("tstr", ("or", leaf("eq", I, "b", "c"), leaf("tst", I, "a", 8)),
     "a tst second, made first"),
    ("kfirst", ("and", leaf("lt_u", I, 5, "a"), leaf("ne", I, "b", "c")),
     "the constant first"),
    ("all_ops", ("or", ("and", ("and", leaf("le_u", I, "a", "b"), leaf("gt_s", I, "c", 2)),
                        leaf("ge_u", I, "d", 4)), leaf("lt_s", I, "a", -5)),
     "le_u, gt_s, ge_u and lt_s, and and or together"),
    ("le_ne", ("and", leaf("ne", I, "c", "d"), leaf("le_s", I, "a", 0)),
     "le_s #0, false where the flags say z"),
    ("hi_ls", ("or", leaf("ge_s", I, "c", "d"), leaf("gt_u", I, "a", 17)),
     "gt_u's hi, true where the flags say c and not z"),
    ("costly", ("and", leaf("eq", I, "a", 3), leaf("gt_u", I, "b", 1000)),
     "the ccmp the first operand, whose constant is an immediate: 1000 would take a register"),
    ("costneg", ("and", leaf("eq", I, "a", -5), leaf("gt_u", I, "b", 1000)),
     "the ccmp the first operand, a ccmn"),
]
# no chain: both float, both tst, an xor
NEAR = [
    ("fltboth", ("and", leaf("le", "f64", "p", "q"), leaf("ge", "f64", "p", "q")),
     "two fcmps: no ccmp takes a float"),
    ("tst2", ("and", leaf("tst", I, "a", 8), leaf("tst", I, "b", 16)),
     "two tsts: no ccmp takes one"),
    ("xor", ("xor", leaf("eq", I, "a", "b"), leaf("lt_s", I, "c", "d")),
     "an xor"),
]

SIG = " ".join("(param $%s %s)" % p for p in PARAMS)
CCH_MAX = 4


def args_line(fn, env):
    return " ".join([fn] + [arg_text(env, n, t) for n, t in PARAMS])


def mask(ty):
    return M32 if ty == "i32" else M64


def cost(lf):
    """what the ccmp of lf costs beyond itself: a register for a constant no
    5-bit immediate, either way, takes"""
    _, op, ty, l, r = lf
    if op == "eqz" or not isinstance(r, int):
        return 0
    k = r & mask(ty)
    return 0 if k <= 31 or (-k) & mask(ty) <= 31 else 1


def model(e):
    """e as find_cmp_chains takes it: (the compares in the chain it is, 0 for
    none, the compares made as ccmp in it and its sides, how many chains)"""
    if e[0] not in ("and", "or"):
        return 0, [], 0
    sides = [model(e[1]), model(e[2])]
    best = None
    for r in (1, 0):
        x, l = (e[1], e[2])[r], (e[1], e[2])[1 - r]
        if x[0] != "leaf" or x[2] == "f64" or x[1] == "tst":
            continue
        chain = 0 < sides[1 - r][0] < CCH_MAX
        if not chain and l[0] != "leaf":
            continue
        if best is None or cost(x) < cost((e[1], e[2])[best]):
            best = r
    if best is None:
        return 0, sides[0][1] + sides[1][1], sides[0][2] + sides[1][2]
    ls = sides[1 - best]
    return (ls[0] or 1) + 1, ls[1] + [(e[1], e[2])[best]], ls[2] + 1


def ccmn(lf):
    _, op, ty, l, r = lf
    return isinstance(r, int) and op != "eqz" and (r & mask(ty)) > 31 and 0 < (-r) & mask(ty) <= 31


def reg_form(lf):
    _, op, ty, l, r = lf
    return op != "eqz" and (not isinstance(r, int) or cost(lf) == 1)


# made by hand, with their answers: (name, why, body, model of the answer, shapes)
T = ("and", leaf("eq", I, "a", "b"), leaf("eqz", I, "c"))
SPECIAL = [
    ("two", "two selects on one chain, a sub between them: both read its flags, and no cset",
     "    (local $t i32)\n"
     "    (local.set $t %s)\n"
     "    (i32.add (select (local.get $a) (i32.const 7) (local.get $t))\n"
     "             (select (i32.sub (local.get $b) (i32.const 9)) (i32.const 11) (local.get $t)))"
     % wat(T),
     T, lambda env, r: (env["a"] if r else 7) + ((env["b"] - 9) if r else 11),
     ["\\sccmp\\s=1", "\\scset\\s=0"]),
    ("clob", "two selects on one chain, a compare between them: the second tests the cset",
     "    (local $t i32)\n"
     "    (local.set $t %s)\n"
     "    (i32.add (select (local.get $a) (i32.const 7) (local.get $t))\n"
     "             (i32.add (i32.lt_s (local.get $b) (local.get $d))\n"
     "                      (select (local.get $d) (i32.const 11) (local.get $t))))" % wat(T),
     T, lambda env, r: ((env["a"] if r else 7) + int(signed(env["b"], 32) < signed(env["d"], 32)) +
                        (env["d"] if r else 11)),
     ["\\sccmp\\s=1", "\\scset\\s=2"]),
    ("apart", "a compare between a chain and the select on it: the chain is made at the csel",
     "    (local $t i32)\n"
     "    (local.set $t %s)\n"
     "    (i32.add (i32.lt_s (local.get $b) (local.get $d))\n"
     "             (select (i32.const 7) (i32.const 9) (local.get $t)))" % wat(T),
     T, lambda env, r: int(signed(env["b"], 32) < signed(env["d"], 32)) + (7 if r else 9),
     ["\\sccmp\\s=1", "\\scset\\s=1"]),
    ("armsel", "a chain that is a select's arm as well as its condition: the csel reads the flags",
     "    (local $t i32)\n"
     "    (local.set $t %s)\n"
     "    (i32.add (select (local.get $t) (local.get $b) (local.get $t)) (local.get $t))" % wat(T),
     T, lambda env, r: (1 if r else env["b"]) + r,
     ["\\sccmp\\s=1", "\\scmp\\s=1"]),
    ("shared", "a compare the add reads as well as the and: no chain's",
     "    (local $t i32)\n"
     "    (i32.add (i32.and (local.tee $t (i32.eq (local.get $a) (local.get $b)))\n"
     "                      (i32.lt_s (local.get $c) (local.get $d)))\n"
     "             (local.get $t))",
     ("and", leaf("eq", I, "a", "b"), leaf("lt_s", I, "c", "d")),
     lambda env, r: r + int(env["a"] == env["b"]),
     ["\\sccm[pn]\\s=0"]),
    ("cross", "a compare made in the block before the and's: no chain's",
     "    (local $t i32)\n"
     "    (local.set $t (i32.eq (local.get $a) (local.get $b)))\n"
     "    (if (i32.lt_s (local.get $c) (i32.const 0))\n"
     "      (then (i32.store (i32.const 8) (local.get $c))))\n"
     "    (select (i32.const 7) (i32.const 9)\n"
     "            (i32.and (local.get $t) (i32.lt_s (local.get $d) (local.get $c))))",
     ("and", leaf("eq", I, "a", "b"), leaf("lt_s", I, "d", "c")),
     lambda env, r: 7 if r else 9,
     ["\\sccm[pn]\\s=0"]),
    ("tstand", "eqz of an and of compares, which a tst takes: no chain",
     "    (select (i32.const 7) (i32.const 9)\n"
     "            (i32.eqz (i32.and (i32.eq (local.get $a) (local.get $b))\n"
     "                              (i32.lt_s (local.get $c) (local.get $d)))))",
     ("and", leaf("eq", I, "a", "b"), leaf("lt_s", I, "c", "d")),
     lambda env, r: 9 if r else 7,
     ["\\sccm[pn]\\s=0"]),
]


def main():
    out = []
    w = out.append
    w(";; arm64 makes an and or or of compares, which a select or a branch tests,")
    w(";; as a cmp and a ccmp (isel.c find_cmp_chains, isel_ops.inc emit_cmp_chain):")
    w(";; the ccmp's compare is made where the first side leaves the answer open,")
    w(";; and where that closes it the ccmp sets flags that say so.  Each chain is")
    w(";; tested by a select (s_), by a branch to stores (b_) and returned as a 0")
    w(";; or 1 (v_, a cset); the near misses keep their csets.  The shapes count")
    w(";; the ccmps, ccmns and the ccmps of a register: the compare a chain makes")
    w(";; as its ccmp is one whose constant takes no register where it can be.")
    w(";; Made by test/gen_ccmp.py, which works out every answer.")
    w(";;")
    nch = sum(model(e)[2] for _, e, _ in CHAINS + NEAR)
    # each chain in s_, b_ and v_; and one in each of two, clob, apart and armsel
    w(";; stats -O2 a64: ccmp_chains=%d" % (3 * nch + 4))
    w(";;")
    shapes, calls, funcs = [], [], []
    for name, e, why in CHAINS + NEAR:
        n, cc, _ = model(e)
        for kind in "sbv":
            fn = "%s_%s" % (kind, name)
            shapes.append("%s \\sccmp\\s=%d" % (fn, sum(1 for lf in cc if not ccmn(lf))))
            shapes.append("%s \\sccmn\\s=%d" % (fn, sum(1 for lf in cc if ccmn(lf))))
            shapes.append("%s \\sccmp\\s+[wx]\\d+, [wx](\\d+|zr),=%d"
                          % (fn, sum(1 for lf in cc if reg_form(lf))))
            if kind != "v":
                # a chain's flags tested where they are made; otherwise its
                # two sides' csets, and an and, or or xor of those
                shapes.append("%s \\scset\\s=%d" % (fn, 0 if n else 2))
            for env in runs(e):
                r = ev(e, env)
                calls.append(";; run: %s => i32:%d" % (args_line(fn, env),
                                                        {"s": 7 if r else 9, "b": 70 if r else 9,
                                                         "v": r}[kind]))
        funcs.append("  ;; %s" % why)
        funcs.append("  (func $s_%s (export \"s_%s\") %s (result i32)\n"
                     "    (select (i32.const 7) (i32.const 9) %s))" % (name, name, SIG, wat(e)))
        funcs.append("  (func $b_%s (export \"b_%s\") %s (result i32)\n"
                     "    (if %s\n"
                     "      (then (i32.store (i32.const 0) (i32.const 70)))\n"
                     "      (else (i32.store (i32.const 4) (i32.const 9))))\n"
                     "    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))"
                     % (name, name, SIG, wat(e)))
        funcs.append("  (func $v_%s (export \"v_%s\") %s (result i32)\n"
                     "    %s)" % (name, name, SIG, wat(e)))
    for name, why, body, e, ans, shs in SPECIAL:
        funcs.append("  ;; %s" % why)
        funcs.append("  (func $%s (export \"%s\") %s (result i32)\n%s)" % (name, name, SIG, body))
        shapes += ["%s %s" % (name, sh) for sh in shs]
        for i, env in enumerate(runs(e)):
            # what the answer reads besides the chain, taken in turn
            for v, vals in (("b", [5, -7, 0, 9]), ("d", [3, 100, -1, 5])):
                env.setdefault(v, vals[i % len(vals)] & M32)
            calls.append(";; run: %s => i32:%d" % (args_line(name, env),
                                                    signed(ans(env, ev(e, env)) & M32, 32)))
    for sh in shapes:
        w(";; shape: %s" % sh)
    w(";;")
    for c in calls:
        w(c)
    w("")
    w("(module")
    w("  (memory 1)")
    out += funcs
    w(")")
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(out) + "\n")


main()
