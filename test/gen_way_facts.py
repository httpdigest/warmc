"""test/cases_opt/way_facts.wat: what opt.c pass_bit_facts does.

Tests of one bit, (1 << n) & v read only as a truth value -- by a branch,
a select's condition, an eqz, an eq or ne of 0 -- made (v >> n) & 1: a
constant v and one that is not, i32 and i64, n past the width; and the near
misses whose and's value is read as more than a truth.

Instructions whose operand the ways into their block decide made that
operand or a constant: every integer operation the pass works out --
an and, an or, compares, eqz, sign extensions, shifts, and with one value
whatever fold_int_unop and fold_int_binop take; quotients and remainders
too, which by a constant fold has made multiplies by then, each bound one
value past -- under every kind of fact: each compare with a constant, eqz, a
compare of the operand plus a constant, two tests one inside the other,
two ways that meet, each with its own test, and a br_table's cases.  Each
function is called by a driver of its own on inputs either side of each
constant, which hashes what comes back; the traps are called alone.  The
driver's inputs are its parameter plus a constant, so that nothing is known
of them but what the tests say.  Every answer is worked out here.

  python3 test/gen_way_facts.py test/cases_opt/way_facts.wat
"""
import random
import sys

W = {"i32": 32, "i64": 64}


def mk(ty):
    return (1 << W[ty]) - 1


def sg(v, w):
    v &= (1 << w) - 1
    return v - (1 << w) if v >> (w - 1) else v


class Trap(Exception):
    def __init__(self, why):
        self.why = why


def clz(v, w):
    for i in range(w - 1, -1, -1):
        if v >> i & 1:
            return w - 1 - i
    return w


def ctz(v, w):
    for i in range(w):
        if v >> i & 1:
            return i
    return w


def unop(op, ty, a):
    """(result type, value) of ty.op a"""
    w, m = W[ty], mk(ty)
    a &= m
    if op == "eqz":
        return "i32", int(a == 0)
    if op == "clz":
        return ty, clz(a, w)
    if op == "ctz":
        return ty, ctz(a, w)
    if op == "popcnt":
        return ty, bin(a).count("1")
    if op == "extend8_s":
        return ty, sg(a, 8) & m
    if op == "extend16_s":
        return ty, sg(a, 16) & m
    if op == "extend32_s":
        return ty, sg(a, 32) & m
    if op == "wrap_i64":
        return "i32", a & mk("i32")
    if op == "extend_i32_s":
        return "i64", sg(a, 32) & mk("i64")
    if op == "extend_i32_u":
        return "i64", a
    raise ValueError(op)


def binop(op, ty, a, b):
    w, m = W[ty], mk(ty)
    a &= m
    b &= m
    sa, sb = sg(a, w), sg(b, w)
    k = b % w
    cmps = {"eq": a == b, "ne": a != b, "lt_s": sa < sb, "lt_u": a < b, "gt_s": sa > sb,
            "gt_u": a > b, "le_s": sa <= sb, "le_u": a <= b, "ge_s": sa >= sb, "ge_u": a >= b}
    if op in cmps:
        return "i32", int(cmps[op])
    if op in ("div_s", "div_u", "rem_s", "rem_u") and b == 0:
        raise Trap("integer divide by zero")
    if op == "div_s" and sa == -(1 << (w - 1)) and sb == -1:
        raise Trap("integer overflow")
    r = {
        "add": lambda: a + b, "sub": lambda: a - b, "mul": lambda: a * b,
        "and": lambda: a & b, "or": lambda: a | b, "xor": lambda: a ^ b,
        "shl": lambda: a << k, "shr_u": lambda: a >> k, "shr_s": lambda: sa >> k,
        "rotl": lambda: (a << k) | (a >> (w - k)) if k else a,
        "rotr": lambda: (a >> k) | (a << (w - k)) if k else a,
        "div_u": lambda: a // b, "rem_u": lambda: a % b,
        # wasm's signed division truncates toward zero
        "div_s": lambda: abs(sa) // abs(sb) * (1 if (sa < 0) == (sb < 0) else -1),
        "rem_s": lambda: (abs(sa) % abs(sb)) * (1 if sa >= 0 else -1),
    }[op]()
    return ty, r & m


CMPS = ["eq", "ne", "lt_s", "lt_u", "gt_s", "gt_u", "le_s", "le_u", "ge_s", "ge_u"]
BINS = ["add", "sub", "mul", "and", "or", "xor", "shl", "shr_u", "shr_s", "rotl", "rotr",
        "div_s", "div_u", "rem_s", "rem_u"] + CMPS
UNS = {"i32": ["eqz", "clz", "ctz", "popcnt", "extend8_s", "extend16_s", "extend_i32_s",
               "extend_i32_u"],
       "i64": ["eqz", "clz", "ctz", "popcnt", "extend8_s", "extend16_s", "extend32_s",
               "wrap_i64"]}


def un_wat(op, ty, x):
    if op == "extend_i32_s" or op == "extend_i32_u":
        return "(i64.%s %s)" % (op, x)
    if op == "wrap_i64":
        return "(i32.wrap_i64 %s)" % x
    return "(%s.%s %s)" % (ty, op, x)


# what a fact says: a test of x, its truth, as wat and as a predicate
def fact_rel(ty, op, k):
    return ("(%s.%s (local.get $x) (%s.const %d))" % (ty, op, ty, sg(k, W[ty])),
            lambda x: binop(op, ty, x, k)[1] == 1)


def fact_eqz(ty):
    return ("(%s.eqz (local.get $x))" % ty, lambda x: (x & mk(ty)) == 0)


def fact_off(ty, op, c, k):
    w = W[ty]
    return ("(%s.%s (%s.add (local.get $x) (%s.const %d)) (%s.const %d))"
            % (ty, op, ty, ty, sg(c, w), ty, sg(k, w)),
            lambda x: binop(op, ty, (x + c) & mk(ty), k)[1] == 1)


def interesting(ty):
    w = W[ty]
    h = 1 << (w - 1)
    v = [0, 1, 2, 7, 8, 9, 10, 15, 16, 48, 57, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
         1000, h - 1, h, h + 1, (1 << w) - 1, (1 << w) - 2, (1 << w) - 128, (1 << w) - 129]
    if w == 64:
        v += [1 << 32, (1 << 32) - 1, 1 << 40, (1 << 31) - 1, 1 << 31]
    return v


def main():
    rnd = random.Random(20261105)
    funcs, calls, shapes = [], [], []
    nfn = [0]

    def emit(name, ty, rty, params, body, inputs, model, why, traps=()):
        """$name (params) -> rty with body; its driver $name.d hashes
        model(*input) for each input; traps are (input, why) called alone"""
        funcs.append("  ;; %s" % why)
        funcs.append("  (func $%s (export \"%s\") %s (result %s)" % (name, name, params, rty))
        funcs.extend("    " + l for l in body)
        funcs.append("  )")
        # the driver: each input its parameter plus a constant
        tys = [p.split()[-1].rstrip(")") for p in params.split("(param")[1:]]
        funcs.append("  (func $%s_d (export \"%s_d\") (param $p i32) (result i64)" % (name, name))
        funcs.append("    (local $h i64)")
        h = 0
        for inp in inputs:
            args = []
            for t, v in zip(tys, inp):
                pv = "(local.get $p)" if t == "i32" else "(i64.extend_i32_s (local.get $p))"
                args.append("(%s.add %s (%s.const %d))" % (t, pv, t, sg(v, W[t])))
            r = model(*inp) & mk(rty)
            call = "(call $%s %s)" % (name, " ".join(args))
            if rty != "i64":
                call = "(i64.extend_i32_u %s)" % call
            funcs.append("    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 1000003)) %s))"
                         % call)
            h = (h * 1000003 + r) & mk("i64")
        funcs.append("    (local.get $h))")
        calls.append(";; run: %s_d 0 => i64:%d" % (name, sg(h, 64)))
        for inp, tw in traps:
            calls.append(";; run: %s %s => trap: %s" % (name, " ".join(str(sg(v, W[t])) for t, v in zip(tys, inp)), tw))
        nfn[0] += 1

    # ---- bit tests ----------------------------------------------------
    def bit_case(name, ty, use, mask, why, value_use=False):
        """a test of bit n of v: (1 << n) & v, with v the constant mask or the
        parameter $v; use says how the and is read"""
        w, m = W[ty], mk(ty)
        v_wat = "(%s.const %d)" % (ty, sg(mask, w)) if mask is not None else "(local.get $v)"
        a = "(%s.and (%s.shl (%s.const 1) (local.get $n)) %s)" % (ty, ty, ty, v_wat)
        if use == "br":
            body = ["(if (result i32) %s%s (then (i32.const %d)) (else (i32.const 9)))"
                    % ("(%s.ne %s (%s.const 0))" % (ty, a, ty) if ty == "i64" else a, "", 70 + nfn[0])]
            mod = lambda t: 70 + nfn_now if t else 9
        elif use == "select":
            body = ["(select (i32.const %d) (i32.const 9) %s)"
                    % (70 + nfn[0], "(i64.ne %s (i64.const 0))" % a if ty == "i64" else a)]
            mod = lambda t: 70 + nfn_now if t else 9
        elif use == "eqz":
            body = ["(%s.eqz %s)" % (ty, a)]
            mod = lambda t: int(not t)
        elif use == "ne0":
            body = ["(%s.ne %s (%s.const 0))" % (ty, a, ty)]
            mod = lambda t: int(t)
        elif use == "eq0":
            body = ["(%s.eq (%s.const 0) %s)" % (ty, ty, a)]
            mod = lambda t: int(not t)
        elif use == "value":
            body = [a]
            mod = None
        elif use == "eq8":
            body = ["(%s.eq %s (%s.const 8))" % (ty, a, ty)]
            mod = None
        rty = ty if use == "value" else "i32"
        nfn_now = nfn[0]

        def model(n, v):
            vv = mask if mask is not None else v
            bit = (vv & m) >> (n % w) & 1
            if use == "value":
                return (1 << (n % w)) & vv & m
            if use == "eq8":
                return int(((1 << (n % w)) & vv & m) == 8)
            return mod(bit)
        ns = [0, 1, 3, 4, 5, 9, 13, 23, 24, 31, 32, 33, 37, 63, 64, 65, -1]
        vs = [0x80001f, 0x2600, -1, 0, 8, 1 << 31] + ([1 << 40, -(1 << 33)] if w == 64 else [])
        inputs = [(n, v) for n in ns for v in (vs if mask is None else [0])]
        emit(name, ty, rty, "(param $n %s) (param $v %s)" % (ty, ty), body, inputs, model, why)

    nbits = 0
    for ty in ("i32", "i64"):
        for use in ("br", "select", "eqz", "ne0", "eq0"):
            bit_case("bt_%s_%s_c" % (ty, use), ty, use, 0x80001f if ty == "i32" else 0x100002600,
                     "a bit of a constant, read by %s" % use)
            bit_case("bt_%s_%s_v" % (ty, use), ty, use, None, "a bit of a value, read by %s" % use)
            nbits += 2
        bit_case("bt_%s_value" % ty, ty, "value", None, "near miss: the and's value is the result")
        bit_case("bt_%s_eq8" % ty, ty, "eq8", None, "near miss: compared with 8, not 0")
    # the shifts: lsr of the value, no lsl of 1 (but the near miss's)
    for fn, pat, k in [("bt_i32_br_c", "lsr", 1), ("bt_i32_br_c", "lsl", 0), ("bt_i32_br_v", "lsr", 1),
                       ("bt_i32_br_v", "lsl", 0), ("bt_i64_eqz_v", "lsr", 1), ("bt_i64_eqz_v", "lsl", 0),
                       ("bt_i32_value", "lsl", 1), ("bt_i32_value", "lsr", 0)]:
        shapes.append("%s \\s%s\\s+[wx]\\d=%d" % (fn, pat, k))
    # near misses whose and's value is read as more than a truth: a select's
    # value, a br_table's index, an if's result
    M32 = mk("i32")
    bt = lambda n, v: (1 << (n % 32)) & v & M32
    emit("bt_selval", "i32", "i32", "(param $n i32) (param $v i32)",
         ["(select (i32.and (i32.shl (i32.const 1) (local.get $n)) (local.get $v)) (i32.const 5)",
          "        (i32.lt_u (local.get $n) (i32.const 40)))"],
         [(n, v) for n in (0, 3, 31, 33, 45) for v in (8, -1, 0)],
         lambda n, v: bt(n, v) if (n & M32) < 40 else 5, "near miss: the and a select's value")
    emit("bt_table", "i32", "i32", "(param $n i32) (param $v i32)",
         ["(block $b2 (block $b1 (block $b0",
          "  (br_table $b0 $b1 $b2 (i32.and (i32.shl (i32.const 1) (local.get $n)) (local.get $v))))",
          "  (return (i32.const 70)))",
          "  (return (i32.const 71)))",
          "(i32.const 72)"],
         [(n, v) for n in (0, 1, 2, 3) for v in (0, 1, 2, 3, -1)],
         lambda n, v: {0: 70, 1: 71}.get(bt(n, v), 72), "near miss: the and a br_table's index")
    emit("bt_phi", "i32", "i32", "(param $n i32) (param $v i32)",
         ["(local $r i32)",
          "(if (i32.lt_u (local.get $n) (i32.const 40))",
          "  (then (i32.store (i32.const 64) (local.get $n))",
          "        (local.set $r (i32.and (i32.shl (i32.const 1) (local.get $n)) (local.get $v))))",
          "  (else (i32.store (i32.const 68) (local.get $v)) (local.set $r (i32.const 3))))",
          "(i32.add (local.get $r) (i32.load (i32.const 72)))"],
         [(n, v) for n in (0, 3, 31, 33, 45) for v in (8, -1, 0)],
         lambda n, v: bt(n, v) if (n & M32) < 40 else 3, "near miss: the and a value the ways hand on")

    # shl of 2, not 1: a near miss
    emit("bt_two", "i32", "i32", "(param $n i32) (param $v i32)",
         ["(if (result i32) (i32.and (i32.shl (i32.const 2) (local.get $n)) (local.get $v))",
          "  (then (i32.const 70)) (else (i32.const 9)))"],
         [(n, v) for n in (0, 1, 30, 31, 32) for v in (1, 2, 4, -1, 1 << 31)],
         lambda n, v: 70 if (2 << (n % 32)) & v & mk("i32") else 9,
         "near miss: 2 << n tests bit n + 1")

    # ---- way folds ----------------------------------------------------
    def consts_for(op, ty, ks):
        w = W[ty]
        if op in ("and", "or", "xor"):
            c = [0xff, 0x7f, 0x3f, 0xf0, 0x0f, 0x1ffffe, 1 << (w - 1), mk(ty) ^ 0xff, 0xffff, 1, 0x100]
        elif op in ("shl", "shr_u", "shr_s", "rotl", "rotr"):
            c = [1, 3, 4, 7, 8, 15, 16, w - 1, w, w + 1]
        elif op in ("div_s", "div_u", "rem_s", "rem_u"):
            c = [1, 2, 7, 10, 16, 100, 256, 0x10000, mk(ty), mk(ty) - 6, 1 << (w - 1)]
        elif op in CMPS:
            c = [k + d for k in ks for d in (-1, 0, 1)] + [0, mk(ty), 0x80, 1 << (w - 1)]
        else:
            c = [1, mk(ty), 3, 0x1000, 1 << (w - 1)]
        return [x & mk(ty) for x in c]

    def facts(ty):
        w, m = W[ty], mk(ty)
        out = []
        for op in CMPS:
            for k in rnd.sample(interesting(ty), 4):
                out.append(("rel", fact_rel(ty, op, k), [k]))
        out.append(("eqz", fact_eqz(ty), [0]))
        for op, c, k in [("lt_u", -48, 10), ("le_u", -0x30, 9), ("lt_u", 128, 256), ("gt_u", -1, 6),
                         ("lt_s", 5, 0), ("ge_u", -10, 0x20)]:
            out.append(("off", fact_off(ty, op, c & m, k & m), [(k - c) & m, (-c) & m]))
        return out

    def inputs_for(ty, ks):
        m = mk(ty)
        v = set()
        for k in ks:
            for d in (-2, -1, 0, 1, 2):
                v.add((k + d) & m)
        for x in (0, 1, m, 1 << (W[ty] - 1), (1 << (W[ty] - 1)) - 1, 0x7f, 0x80, 0xff, 0x100):
            v.add(x & m)
        v.update(rnd.randrange(1 << W[ty]) for _ in range(3))
        return sorted(v)

    def arm(ty, op, c, xfirst, tag):
        """op on $x and the constant c, xor tag: (wat, result type, model)"""
        x = "(local.get $x)"
        if op in UNS[ty]:
            rty = unop(op, ty, 0)[0]
            e = un_wat(op, ty, x)
            f = lambda xv: unop(op, ty, xv)[1]
        else:
            rty = binop(op, ty, 0, 1)[0]
            k = "(%s.const %d)" % (ty, sg(c, W[ty]))
            e = "(%s.%s %s %s)" % (ty, op, x, k) if xfirst else "(%s.%s %s %s)" % (ty, op, k, x)
            f = (lambda xv: binop(op, ty, xv, c)[1]) if xfirst else (lambda xv: binop(op, ty, c, xv)[1])
        return "(%s.xor %s (%s.const %d))" % (rty, e, rty, tag), rty, (lambda xv: f(xv) ^ tag)

    def way_case(name, ty, fact, op, c, xfirst, ks, why):
        fw, fp = fact
        t_wat, rty, t_mod = arm(ty, op, c, xfirst, 0x5a)
        e_wat, _, e_mod = arm(ty, op, c, xfirst, 0x3c)
        body = ["(if (result %s) %s" % (rty, fw), "  (then %s)" % t_wat, "  (else %s))" % e_wat]
        ins = inputs_for(ty, ks + [c])
        ok, traps = [], []
        for v in ins:
            try:
                (t_mod if fp(v) else e_mod)(v)
                ok.append((v,))
            except Trap as tr:
                traps.append(((v,), tr.why))
        emit(name, ty, rty, "(param $x %s)" % ty, body, ok,
             lambda v: (t_mod if fp(v) else e_mod)(v), why, traps[:2])

    n = 0
    for ty in ("i32", "i64"):
        fs = facts(ty)
        ops = BINS + UNS[ty]
        # every op under several facts, every fact under several ops
        pairs = [(op, f) for op in ops for f in rnd.sample(fs, 3)]
        pairs += [(rnd.choice(ops), f) for f in fs]
        for op, (kind, fact, ks) in pairs:
            if op in UNS[ty]:
                cs, xf = [0], [True]
            else:
                cs = rnd.sample(consts_for(op, ty, ks), 2)
                xf = [True, False] if op not in ("add", "mul", "and", "or", "xor", "eq", "ne") else [True]
            c = rnd.choice(cs)
            xfirst = rnd.choice(xf)
            way_case("wf%d" % n, ty, fact, op, c, xfirst, ks,
                     "%s.%s%s under %s" % (ty, op, "" if xfirst else " (constant first)", fact[0]))
            n += 1

    # two tests, one inside the other: what both say
    for i, (ty, f1, f2, op, c) in enumerate([
            ("i32", ("ge_s", 0), ("lt_s", 0x80), "and", 0xff),
            ("i32", ("gt_u", 9), ("lt_u", 16), "and", 0xf),
            ("i32", ("ge_u", 0x30), ("le_u", 0x39), "and", 0x3f),
            ("i32", ("ge_s", -16), ("lt_s", 0), "shr_s", 4),
            ("i32", ("lt_u", 300), ("ge_u", 256), "and", 0xff),
            ("i64", ("ge_s", 0), ("lt_u", 1 << 40), "shr_u", 40),
            ("i64", ("gt_s", -129), ("lt_s", 128), "extend8_s", 0),
            # each bound exactly: one value past it, which must not fold
            ("i32", ("ge_s", -129), ("lt_s", 0), "extend8_s", 0),
            ("i32", ("ge_u", 0x80), ("lt_u", 0x100), "extend8_s", 0),
            ("i32", ("ge_u", 0x103), ("le_u", 0x107), "or", 3),
            ("i32", ("ge_s", 0), ("le_s", 16), "shr_s", 4),
            ("i32", ("ge_s", -17), ("lt_s", 0), "shr_s", 4),
            ("i32", ("ge_u", 1), ("le_u", 10), "rem_u", 10),
            ("i32", ("ge_u", 1), ("le_u", 10), "div_u", 10),
            ("i32", ("ge_s", 0), ("lt_s", 100), "rem_s", -7),
            ("i32", ("ge_s", 0), ("lt_s", 100), "div_s", -7)]):
        m = mk(ty)
        a1 = fact_rel(ty, f1[0], f1[1] & m)
        a2 = fact_rel(ty, f2[0], f2[1] & m)
        t_wat, rty, t_mod = arm(ty, op, c & m, True, 0x5a)
        e_wat, _, e_mod = arm(ty, op, c & m, True, 0x3c)
        body = ["(if (result %s) %s" % (rty, a1[0]),
                "  (then (if (result %s) %s (then %s) (else %s)))" % (rty, a2[0], t_wat, e_wat),
                "  (else %s))" % e_wat]
        ins = inputs_for(ty, [f1[1] & m, f2[1] & m, c & m])
        model = lambda v, a1=a1, a2=a2, t_mod=t_mod, e_mod=e_mod: (t_mod if a1[1](v) and a2[1](v) else e_mod)(v)
        emit("nest%d" % i, ty, rty, "(param $x %s)" % ty, body, [(v,) for v in ins], model,
             "%s %s and %s: %s" % (ty, f1, f2, op))
    # the ands the tests leave: the arms where both hold have none
    shapes.append("nest0 \\sand\\s=2")
    shapes.append("nest2 \\sand\\s=1")

    # two ways that meet, each tested its own way: what either says
    for i, (ty, f1, f2, op, c) in enumerate([
            ("i32", ("lt_u", 16), ("lt_u", 8), "and", 0xf),
            ("i32", ("lt_u", 16), ("lt_u", 32), "and", 0xf),
            ("i32", ("ge_s", 0), ("eq", 5), "lt_s", 0),
            ("i32", ("eq", 3), ("eq", 3), "mul", 7),
            ("i64", ("lt_u", 256), ("lt_u", 100), "rem_u", 256)]):
        m = mk(ty)
        a1 = fact_rel(ty, f1[0], f1[1] & m)
        a2 = fact_rel(ty, f2[0], f2[1] & m)
        t_wat, rty, t_mod = arm(ty, op, c & m, True, 0x5a)
        body = ["(block $out",
                "  (if (local.get $s)",
                "    (then (br_if $out (i32.eqz %s)))" % a1[0],
                "    (else (br_if $out (i32.eqz %s))))" % a2[0],
                "  (return %s))" % t_wat,
                "(%s.const 77)" % rty]
        ins = inputs_for(ty, [f1[1] & m, f2[1] & m, c & m])

        def model(v, s, a1=a1, a2=a2, t_mod=t_mod):
            ok = a1[1](v) if s & mk("i32") else a2[1](v)
            return t_mod(v) if ok else 77
        emit("merge%d" % i, ty, rty, "(param $x %s) (param $s i32)" % ty, body,
             [(v, s) for v in ins for s in (0, 1)], model,
             "%s meeting ways: %s or %s, then %s" % (ty, f1, f2, op))
    shapes.append("merge0 \\sand\\s=0")

    # a br_table's cases: one value each
    for i, (op, c) in enumerate([("and", 0x1ffffe), ("mul", 3), ("shl", 2), ("sub", 10), ("div_u", 0),
                                 ("lt_u", 2), ("popcnt", 0)]):
        ty, m = "i32", mk("i32")
        xfirst = op not in ("sub", "div_u")
        arms = [arm(ty, op, c, xfirst, 0x10 * j) for j in range(5)]
        rty = arms[0][1]
        body = ["(block $d (block $c3 (block $c2 (block $c1 (block $c0",
                "  (br_table $c0 $c1 $c2 $c3 $d (local.get $x)))",
                "  (return %s))" % arms[0][0],
                "  (return %s))" % arms[1][0],
                "  (return %s))" % arms[2][0],
                "  (return %s))" % arms[3][0],
                arms[4][0]]
        ok, traps = [], []
        for v in range(-1, 7):
            try:
                arms[min(v & m, 4)][2](v & m)
                ok.append((v & m,))
            except Trap as tr:
                traps.append(((v & m,), tr.why))
        emit("table%d" % i, ty, rty, "(param $x i32)", body, ok,
             lambda v, arms=arms: arms[min(v, 4)][2](v),
             "a br_table's cases, then %s %s" % (op, "x first" if xfirst else "the constant first"), traps)

    # a byte loaded with its sign, found not negative: the byte (what it
    # can be for its shape and what the test says, together)
    emit("ascii", "i32", "i32", "(param $p i32)",
         ["(local $b i32)",
          "(local.set $b (i32.load8_s offset=256 (i32.and (local.get $p) (i32.const 7))))",
          "(if (result i32) (i32.ge_s (local.get $b) (i32.const 0))",
          "  (then (i32.xor (i32.and (local.get $b) (i32.const 0xff)) (i32.const 0x5a)))",
          "  (else (i32.xor (i32.and (local.get $b) (i32.const 0xff)) (i32.const 0x3c))))"],
         [(p,) for p in range(8)],
         lambda p: (BYTES[p & 7] ^ (0x5a if BYTES[p & 7] < 0x80 else 0x3c)),
         "a signed byte not negative masked to the byte")
    shapes.append("ascii \\sand\\s=2")
    # bits every value has: or with them is the value, which needs the
    # value's shape (at most 0x3ff) and the test (at least 0x200)
    emit("orbits", "i32", "i32", "(param $y i32)",
         ["(local $x i32)",
          "(local.set $x (i32.and (local.get $y) (i32.const 0x3ff)))",
          "(if (result i32) (i32.ge_u (local.get $x) (i32.const 0x200))",
          "  (then (i32.xor (i32.or (local.get $x) (i32.const 0x200)) (i32.const 0x5a)))",
          "  (else (i32.xor (i32.or (local.get $x) (i32.const 0x200)) (i32.const 0x3c))))"],
         [(y,) for y in (0, 0x1ff, 0x200, 0x201, 0x3ff, 0x400, 0x5ff, 0x600, -1)],
         lambda y: ((y & 0x3ff) | 0x200) ^ (0x5a if (y & 0x3ff) >= 0x200 else 0x3c),
         "an or of bits the value has")
    shapes.append("orbits \\sorr\\s=1")
    # a test of a value made again past where the ways meet (kt_same)
    emit("again", "i32", "i32", "(param $x i32) (param $s i32)",
         ["(block $out",
          "  (if (local.get $s)",
          "    (then (i32.store (i32.const 64) (local.get $s))",
          "          (br_if $out (i32.ge_u (i32.mul (local.get $x) (i32.const 3)) (i32.const 16))))",
          "    (else (i32.store (i32.const 68) (local.get $x))",
          "          (br_if $out (i32.ge_u (i32.mul (local.get $x) (i32.const 3)) (i32.const 16)))))",
          "  (return (i32.and (i32.mul (local.get $x) (i32.const 3)) (i32.const 15))))",
          "(i32.const 77)"],
         [(x, s_) for x in (0, 1, 5, 6, 0x55555556, -1) for s_ in (0, 1)],
         lambda x, s_: ((x * 3) & M32 & 15) if ((x * 3) & M32) < 16 else 77,
         "the ways test x * 3, made again past their meeting")
    shapes.append("again \\sand\\s=0")

    # a value tested before a loop, read in it: the way round says nothing,
    # the way out what the exit's test does
    emit("loop0", "i32", "i32", "(param $x i32) (param $n i32)",
         ["(local $i i32) (local $acc i32)",
          "(if (i32.ge_u (local.get $x) (i32.const 16)) (then (return (i32.const -1))))",
          "(loop $l",
          "  (local.set $acc (i32.add (local.get $acc) (i32.and (local.get $x) (i32.const 15))))",
          "  (local.set $x (i32.add (local.get $x) (i32.const 1)))",
          "  (local.set $i (i32.add (local.get $i) (i32.const 1)))",
          "  (br_if $l (i32.lt_u (local.get $i) (local.get $n))))",
          "(i32.add (local.get $acc) (i32.and (local.get $x) (i32.const 0xff)))"],
         [(x, nn) for x in (0, 5, 15, 16, 200) for nn in (1, 3, 20, 300)],
         lambda x, nn: _loop0(x, nn),
         "a value the loop changes: its test before the loop holds the first time round only")

    out = []
    w = out.append
    w(";; opt.c pass_bit_facts: a test of one bit, (1 << n) & v read as a truth")
    w(";; value, made (v >> n) & 1; and instructions whose operand the ways into")
    w(";; their block decide made that operand or a constant -- every integer")
    w(";; operation the pass works out, under every kind of fact (a compare with a")
    w(";; constant, eqz, a compare of the operand plus a constant, two tests one in")
    w(";; the other, two ways that meet, a br_table's cases), and a value a loop")
    w(";; changes.  Each function's driver calls it on inputs either side of each")
    w(";; constant, its parameter plus a constant, and hashes what comes back; the")
    w(";; traps are called alone.  Made by test/gen_way_facts.py, which works out")
    w(";; every answer.")
    w(";;")
    w(";; stats -O2: bit_tests=%s way_folds=%s blocks_merged=%s" % (BIT_TESTS, WAY_FOLDS, BLOCKS_MERGED))
    w(";;")
    for sh in shapes:
        w(";; shape: %s" % sh)
    w(";;")
    out.extend(calls)
    w("(module")
    w("  (memory 1)")
    w('  (data (i32.const 256) "%s")' % "".join("\\%02x" % b for b in BYTES))
    out.extend(funcs)
    w(")")
    open(sys.argv[1], "w").write("\n".join(out) + "\n")


def _loop0(x, n):
    m = mk("i32")
    x &= m
    n &= m
    if x >= 16:
        return -1 & m
    acc, i = 0, 0
    while True:
        acc = (acc + (x & 15)) & m
        x = (x + 1) & m
        i += 1
        if not i < n:
            break
    return (acc + (x & 0xff)) & m


# the bytes ascii loads, either side of the sign
BYTES = [0x41, 0xc3, 0x7f, 0x80, 0x00, 0xff, 0x20, 0xe2]

# what warmc's --stats says at -O2: the bits tested and the folds made
BIT_TESTS = 1380
WAY_FOLDS = 3128
# and the blocks merge_blocks makes one with the block that jumps to them
BLOCKS_MERGED = 29484

main()
