"""test/cases_opt/cinc.wat: adds of a compare's 0 or 1, which arm64 makes as
the compare and a cinc of the other operand (isel.c find_cincs) -- every
integer compare and eqz with its right operand a register, an immediate, a
negative one, a constant no immediate takes and a constant on its left;
i64 compares through an i64 add of their extension; float compares, NaN
among their operands; and the near misses that keep their csets.  Every
answer is worked out here.

  python3 test/gen_cinc.py test/cases_opt/cinc.wat
"""
import struct
import sys

M32, M64 = (1 << 32) - 1, (1 << 64) - 1
PARAMS = [("a", "i32"), ("b", "i32"), ("k", "i32"),
          ("x", "i64"), ("y", "i64"), ("z", "i64"),
          ("p", "f64"), ("q", "f64")]
SIG = " ".join("(param $%s %s)" % p for p in PARAMS)
NAN = float("nan")


def signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


CMPS = {
    "eq": lambda l, r, w: l == r, "ne": lambda l, r, w: l != r,
    "lt_s": lambda l, r, w: signed(l, w) < signed(r, w), "lt_u": lambda l, r, w: l < r,
    "gt_s": lambda l, r, w: signed(l, w) > signed(r, w), "gt_u": lambda l, r, w: l > r,
    "le_s": lambda l, r, w: signed(l, w) <= signed(r, w), "le_u": lambda l, r, w: l <= r,
    "ge_s": lambda l, r, w: signed(l, w) >= signed(r, w), "ge_u": lambda l, r, w: l >= r,
}
FCMPS = {"lt": lambda l, r: l < r, "gt": lambda l, r: l > r, "le": lambda l, r: l <= r,
         "ge": lambda l, r: l >= r, "eq": lambda l, r: l == r, "ne": lambda l, r: not (l == r)}

# the operand pairs of an integer compare: names, or a constant either side
FORMS32 = [("a", "b"), ("a", 7), ("a", -9), ("a", 0x12345), (5, "b")]
FORMS64 = [("x", "y"), ("x", 3), ("x", -0x100000000)]
# the values an i32 or i64 operand takes, which between them make every
# compare go both ways: equal, apart by one, either side of the sign bit
VALS32 = [0, 1, 5, 6, 7, 8, -9, -10, 0x12345, 0x12346, -1, 0x7fffffff, -0x80000000]
VALS64 = [0, 3, 2, 4, -1, -0x100000000, -0xffffffff, 0x7fffffffffffffff, -0x8000000000000000, 1 << 40]
FVALS = [(1.0, 2.0), (2.0, 1.0), (3.0, 3.0), (NAN, 1.0), (1.0, NAN), (-0.0, 0.0)]
# addends, a wrap among them
K32 = [100, -1, 0x7fffffff]
K64 = [100, -1, 0x7fffffffffffffff]


def operand(o, ty):
    return "(local.get $%s)" % o if isinstance(o, str) else "(%s.const %d)" % (ty, o)


def cmp_wat(op, ty, l, r):
    if op == "eqz":
        return "(%s.eqz %s)" % (ty, operand(l, ty))
    return "(%s.%s %s %s)" % (ty, op, operand(l, ty), operand(r, ty))


def cmp_val(op, ty, l, r, env):
    w = 32 if ty == "i32" else 64
    m = (1 << w) - 1
    lv = (env[l] if isinstance(l, str) else l) & m
    if op == "eqz":
        return int(lv == 0)
    rv = (env[r] if isinstance(r, str) else r) & m
    return int(CMPS[op](lv, rv, w))


def fbits(v):
    return "0x%016x" % struct.unpack("<Q", struct.pack("<d", v))[0]


def args_line(fn, env):
    out = [fn]
    for n, t in PARAMS:
        v = env.get(n, 0)
        out.append(fbits(v) if t == "f64" else str(v))
    return " ".join(out)


def main():
    funcs, calls, shapes = [], [], []
    ncinc = 0

    def case(fn, body, results, cinc, cset, why=None, more=()):
        nonlocal ncinc
        if why:
            funcs.append("  ;; %s" % why)
        rt = "i64" if results and results[0][1].startswith("i64") else "i32"
        funcs.append("  (func $%s (export \"%s\") %s (result %s)\n%s)" % (fn, fn, SIG, rt, body))
        for env, ans in results:
            calls.append(";; run: %s => %s" % (args_line(fn, env), ans))
        shapes.append("%s \\scinc\\s=%d" % (fn, cinc))
        shapes.append("%s \\scset\\s=%d" % (fn, cset))
        shapes.extend("%s %s" % (fn, m) for m in more)
        ncinc += cinc

    # i32: k + cmp, and cmp + k for the register pair
    for op in list(CMPS) + ["eqz"]:
        forms = [("a", None)] if op == "eqz" else FORMS32
        for fi, (l, r) in enumerate(forms):
            for order in ("l", "r") if fi == 0 else ("l",):
                fn = "%s_%s_%d" % (order, op, fi)
                c = cmp_wat(op, "i32", l, r)
                # the add's operands go in the order they are made, so
                # the compare's comes second unless the other is made after
                body = ("    (i32.add (local.get $k) %s)" % c if order == "l"
                        else "    (i32.add %s (i32.xor (local.get $k) (i32.const 0x55)))" % c)
                res, seen = [], set()
                used = [o for o in (l, r) if isinstance(o, str)]
                for i, va in enumerate(VALS32):
                    for vb in (VALS32 if len(used) == 2 else [0]):
                        env = {used[0]: va}
                        if len(used) == 2:
                            env[used[1]] = vb
                        env["k"] = K32[(i + len(res)) % len(K32)]
                        cv = cmp_val(op, "i32", l, r, env)
                        key = (cv, env["k"])
                        if key in seen and len(res) >= 6:
                            continue
                        seen.add(key)
                        kk = env["k"] if order == "l" else env["k"] ^ 0x55
                        res.append((env, "i32:%d" % signed(kk + cv, 32)))
                case(fn, body, res[:10], 1, 0)

    # i64: z + extend_u(cmp)
    for op in list(CMPS) + ["eqz"]:
        forms = [("x", None)] if op == "eqz" else FORMS64
        for fi, (l, r) in enumerate(forms):
            fn = "w_%s_%d" % (op, fi)
            body = ("    (i64.add (local.get $z) (i64.extend_i32_u %s))"
                    % cmp_wat(op, "i64", l, r))
            res, seen = [], set()
            used = [o for o in (l, r) if isinstance(o, str)]
            for i, vx in enumerate(VALS64):
                for vy in (VALS64 if len(used) == 2 else [0]):
                    env = {used[0]: vx}
                    if len(used) == 2:
                        env[used[1]] = vy
                    env["z"] = K64[(i + len(res)) % len(K64)]
                    cv = cmp_val(op, "i64", l, r, env)
                    key = (cv, env["z"])
                    if key in seen and len(res) >= 6:
                        continue
                    seen.add(key)
                    res.append((env, "i64:%d" % signed(env["z"] + cv, 64)))
            case(fn, body, res[:10], 1, 0)

    # f64 compares, the unordered ones among them
    for op, fnc in FCMPS.items():
        fn = "f_%s" % op
        body = "    (i32.add (local.get $k) (f64.%s (local.get $p) (local.get $q)))" % op
        res = []
        for i, (p, q) in enumerate(FVALS):
            env = {"p": p, "q": q, "k": K32[i % len(K32)]}
            res.append((env, "i32:%d" % signed(env["k"] + int(fnc(p, q)), 32)))
        case(fn, body, res, 1, 0)

    def ab_runs(f):
        res = []
        for i, (va, vb) in enumerate([(1, 2), (2, 1), (5, 5), (-1, 1), (-0x80000000, 0x7fffffff)]):
            env = {"a": va, "b": vb, "k": K32[i % len(K32)]}
            res.append((env, f(env)))
        return res

    lt = lambda e: int(signed(e["a"], 32) < signed(e["b"], 32))
    # a count carried round a loop, each element stored where the count so
    # far says, as a partition does: i * 7 - a against b, sixteen of them,
    # the loop unrolled four times; the answer is the count and the first
    # slot's last value
    def count(e):
        n, mem = 0, {}
        for i in range(16):
            v = signed(i * 7 - e["a"], 32)
            mem[n] = v
            n += int(v < signed(e["b"], 32))
        return mem[0] * 64 + n
    funcs.append("  (memory 1)")
    case("count", "    (local $i i32) (local $n i32) (local $v i32)\n"
         "    (loop $l\n"
         "      (local.set $v (i32.sub (i32.mul (local.get $i) (i32.const 7)) (local.get $a)))\n"
         "      (i32.store (i32.add (i32.const 256) (i32.shl (local.get $n) (i32.const 2))) (local.get $v))\n"
         "      (local.set $n (i32.add (local.get $n) (i32.lt_s (local.get $v) (local.get $b))))\n"
         "      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))\n"
         "                          (i32.const 16))))\n"
         "    (i32.add (i32.mul (i32.load (i32.const 256)) (i32.const 64)) (local.get $n))",
         [(e, "i32:%d" % signed(count(e), 32)) for e, _ in ab_runs(lambda e: 0)],
         4, 0, "a count carried round a loop, which addresses the next store")

    gtu = lambda e: int((e["b"] & M32) > (e["k"] & M32))
    case("between", "    (local $t i32)\n"
         "    (local.set $t (i32.lt_s (local.get $a) (local.get $b)))\n"
         "    (i32.add (i32.add (i32.add (local.get $k) (i32.gt_u (local.get $b) (local.get $k)))\n"
         "                      (select (i32.const 7) (i32.const 9) (local.get $t)))\n"
         "             (select (i32.const 11) (i32.const 13) (local.get $t)))",
         ab_runs(lambda e: "i32:%d" % signed(e["k"] + gtu(e) + (7 if lt(e) else 9) +
                                            (11 if lt(e) else 13), 32)),
         1, 1, "two selects on a compare made before an add that makes another for a "
         "cinc: they test the first's cset, the flags being the second's")

    # bytes 1024 + i, 3 + 7 i
    funcs.append('  (data (i32.const 1024) "%s")'
                 % "".join("\\%02x" % ((3 + 7 * i) & 255) for i in range(257)))
    case("addr", "    (i32.load8_u offset=1024\n"
         "      (i32.add (i32.and (local.get $k) (i32.const 255)) (i32.lt_s (local.get $a) (local.get $b))))",
         ab_runs(lambda e: "i32:%d" % ((3 + 7 * ((e["k"] & 255) + lt(e))) & 255)),
         1, 0, "an add a load's address reads, its byte offset folded")
    # near misses
    case("extuse", "    (local $w i64)\n"
         "    (i64.add (i64.add (local.get $z)\n"
         "                      (local.tee $w (i64.extend_i32_u (i32.lt_s (local.get $a) (local.get $b)))))\n"
         "             (i64.shl (local.get $w) (i64.const 8)))",
         [(dict(e, z=K64[i % 3]), "i64:%d" % signed(K64[i % 3] + lt(e) + (lt(e) << 8), 64))
          for i, (e, _) in enumerate(ab_runs(lambda e: 0))],
         0, 1, "an extension something else reads too")
    # two limbs of mpn_add_n with no carry into the first: the second's
    # carry in is the first's compare, and the add a full adder's adcs
    def limbs(e):
        u0, v0, u1 = e["a"] & M32, e["b"] & M32, e["k"] & M32
        v1 = (e["a"] ^ e["k"]) & M32
        s0 = (u0 + v0) & M32
        c0 = int(s0 < u0)
        s1 = (u1 + v1) & M32
        r1 = (s1 + c0) & M32
        c1 = int(s1 < u1) | int(r1 < s1)
        return signed((r1 ^ s0) + c1, 32)
    case("limbs", "    (local $s0 i32) (local $s1 i32) (local $r1 i32)\n"
         "    (local.set $s0 (i32.add (local.get $a) (local.get $b)))\n"
         "    (local.set $s1 (i32.add (local.get $k) (i32.xor (local.get $a) (local.get $k))))\n"
         "    (local.set $r1 (i32.add (local.get $s1) (i32.lt_u (local.get $s0) (local.get $a))))\n"
         "    (i32.add (i32.xor (local.get $r1) (local.get $s0))\n"
         "             (i32.or (i32.lt_u (local.get $s1) (local.get $k))\n"
         "                     (i32.lt_u (local.get $r1) (local.get $s1))))",
         ab_runs(lambda e: "i32:%d" % limbs(e)),
         0, 2, "a full adder's carry in that is a compare: its cset and an adcs, whose "
         "flag is the carry out", ("\\sadcs\\s=1",))
    case("twouse", "    (local $t i32)\n"
         "    (local.set $t (i32.lt_s (local.get $a) (local.get $b)))\n"
         "    (i32.add (i32.add (local.get $k) (local.get $t)) (i32.shl (local.get $t) (i32.const 4)))",
         ab_runs(lambda e: "i32:%d" % signed(e["k"] + lt(e) + (lt(e) << 4), 32)),
         0, 1, "a compare something else reads too keeps its cset")
    case("cross", "    (local $t i32)\n"
         "    (local.set $t (i32.lt_s (local.get $a) (local.get $b)))\n"
         "    (if (i32.lt_s (local.get $k) (i32.const 0))\n"
         "      (then (i32.store (i32.const 64) (local.get $k))))\n"
         "    (i32.add (local.get $k) (local.get $t))",
         ab_runs(lambda e: "i32:%d" % signed(e["k"] + lt(e), 32)),
         0, 1, "a compare made in the block before the add's")
    case("const", "    (i32.add (i32.const 41) (i32.lt_s (local.get $a) (local.get $b)))",
         ab_runs(lambda e: "i32:%d" % (41 + lt(e))),
         0, 1, "a constant addend: cset and an add of the immediate")
    case("sub", "    (i32.sub (local.get $k) (i32.lt_s (local.get $a) (local.get $b)))",
         ab_runs(lambda e: "i32:%d" % signed(e["k"] - lt(e), 32)),
         0, 1, "a subtract takes no cinc")
    case("shifted", "    (i32.add (i32.shl (local.get $k) (i32.const 3)) (i32.lt_s (local.get $a) (local.get $b)))",
         ab_runs(lambda e: "i32:%d" % signed((e["k"] << 3) + lt(e), 32)),
         0, 1, "a shifted addend the add takes as its operand: the shift stays the add's")
    case("sext", "    (i64.add (local.get $z) (i64.extend_i32_s (i32.lt_s (local.get $a) (local.get $b))))",
         [(dict(e, z=K64[i % 3]), "i64:%d" % signed(K64[i % 3] + lt(e), 64))
          for i, (e, _) in enumerate(ab_runs(lambda e: 0))],
         0, 1, "a sign extension, which no cinc takes")

    out = []
    w = out.append
    w(";; arm64 makes an add of a compare's 0 or 1 as the compare and a cinc of")
    w(";; the other operand (isel.c find_cincs), where cset made the 0 or 1 and")
    w(";; an add added it: every integer compare and eqz, its right operand a")
    w(";; register, an immediate, a negative one, a constant no immediate takes")
    w(";; and a constant on the left; both orders of the add; an i64's add of the")
    w(";; compare's extension; float compares, NaN among their operands; a count")
    w(";; carried round a loop; and the near misses that keep their csets.")
    w(";; Made by test/gen_cinc.py, which works out every answer.")
    w(";;")
    w(";; stats -O2 a64: cincs=%d" % ncinc)
    w(";;")
    for sh in shapes:
        w(";; shape: %s" % sh)
    w(";;")
    out.extend(calls)
    w("(module")
    out.extend(funcs)
    w(")")
    open(sys.argv[1], "w").write("\n".join(out) + "\n")


main()
