"""test/cases_opt/tbit_sign.wat: branches arm64 makes tbz or tbnz of a sign
or of a bit followed back through what the test alone reads (isel.c
tbit_sign, tbit_chain) -- every signed and unsigned compare that tests a
sign, i32 and i64, each way round; each step of a chain (shifts, rotates,
sign and zero extensions, wraps, two steps together) under a sign test and
under a test of one bit; and the near misses: a compare one off a sign
test, a step whose value something else reads too, one made in another
block, a shift by a variable, a bit the chain cannot follow.  Each branch
stores 70 and the function's number at 0, or 9 at 4, and returns the sum,
so that no if-conversion takes it and no two functions are one.  Every answer is worked out here.

  python3 test/gen_tbit_sign.py test/cases_opt/tbit_sign.wat
"""
import sys

M = {"i32": (1 << 32) - 1, "i64": (1 << 64) - 1}
W = {"i32": 32, "i64": 64}


def sg(v, w):
    v &= (1 << w) - 1
    return v - (1 << w) if v >> (w - 1) else v


def rotl(v, k, w):
    k %= w
    m = (1 << w) - 1
    return ((v << k) | (v >> (w - k))) & m if k else v & m


# the compares: name, its value of l and r in width w
CMP = {
    "lt_s": lambda l, r, w: sg(l, w) < sg(r, w), "le_s": lambda l, r, w: sg(l, w) <= sg(r, w),
    "gt_s": lambda l, r, w: sg(l, w) > sg(r, w), "ge_s": lambda l, r, w: sg(l, w) >= sg(r, w),
    "lt_u": lambda l, r, w: l < r, "le_u": lambda l, r, w: l <= r,
    "gt_u": lambda l, r, w: l > r, "ge_u": lambda l, r, w: l >= r,
}


def vals(ty):
    w = W[ty]
    h = 1 << (w - 1)
    out = [0, 1, 5, -1, -2, h - 1, h, h + 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0x12345678,
           -0x12345678, 1 << 31, (1 << 31) - 1]
    if w == 64:
        out += [1 << 40, (1 << 40) | 0x80, -(1 << 40)]
    return sorted(set(sg(v, w) for v in out))


def main():
    shapes, calls, funcs = [], [], []
    nbr = [0]
    nfn = [0]  # the taken way stores 70 + this, so that no two functions are one

    def case(name, ty, body, cond, n_tb, why, extra=(), inputs=None, yty=None):
        """a function of $y (yty, ty by default) branching on the condition
        body, cond(y) its truth; n_tb tbz/tbnz expected (and counted as
        bit_brs when n_tb), extra more shapes"""
        yty = yty or ty
        funcs.append("  ;; %s" % why)
        funcs.append("  (func $%s (export \"%s\") (param $y %s) (result i32)" % (name, name, yty))
        funcs.append("    (if %s" % body)
        funcs.append("      (then (i32.store (i32.const 0) (i32.const %d)))" % (70 + nfn[0]))
        funcs.append("      (else (i32.store (i32.const 4) (i32.const 9))))")
        funcs.append("    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))")
        shapes.append("%s \\stb(n)?z\\s=%d" % (name, n_tb))
        shapes.extend("%s %s" % (name, e) for e in extra)
        for v in (inputs or vals(yty)):
            calls.append(";; run: %s %d => i32:%d" % (name, v, 70 + nfn[0] if cond(v & M[yty]) else 9))
        nfn[0] += 1

    Y = "(local.get $y)"

    # signs: every compare that tests one, each width, each way round
    for ty in ("i32", "i64"):
        w, m = W[ty], M[ty]
        h = 1 << (w - 1)
        sign_tests = [("lt_s", 0), ("le_s", -1), ("ge_s", 0), ("gt_s", -1),
                      ("ge_u", h), ("gt_u", h - 1), ("lt_u", h), ("le_u", h - 1)]
        for op, k in sign_tests:
            nm = "%s_%s_%s" % (ty, op, "m1" if k == -1 else "h" if k == h else "hm1" if k == h - 1 else str(k))
            case(nm, ty, "(%s.%s %s (%s.const %d))" % (ty, op, Y, ty, sg(k, w)),
                 lambda y, op=op, k=k, w=w, m=m: CMP[op](y, k & m, w), 1,
                 "%s.%s against %d: a test of the sign" % (ty, op, sg(k, w)),
                 ("\\scm(p|n)\\s=0", "\\stb(n)?z\\s+%s\\d+, #0x%x=1" % ("w" if w == 32 else "x", w - 1)))
            nbr[0] += 1
        # one off: a compare of its own
        for op, k in [("lt_s", 1), ("le_s", 0), ("gt_s", 0), ("ge_s", -2), ("lt_u", h + 1),
                      ("ge_u", h - 1), ("gt_u", h), ("le_u", h)]:
            nm = "%s_%s_miss%d" % (ty, op, len(funcs))
            case(nm, ty, "(%s.%s %s (%s.const %d))" % (ty, op, Y, ty, sg(k, w)),
                 lambda y, op=op, k=k, w=w, m=m: CMP[op](y, k & m, w), 0,
                 "%s.%s against %d: not a sign" % (ty, op, sg(k, w)))

    # chains under a sign test: the bit each step's operand gives
    m32, m64 = M["i32"], M["i64"]
    chains = [
        ("c_ext8", "i32", "(i32.lt_s (i32.extend8_s %s) (i32.const 0))" % Y,
         lambda y: (y >> 7) & 1, 7, "\\ssxtb\\s=0"),
        ("c_ext16", "i32", "(i32.ge_s (i32.extend16_s %s) (i32.const 0))" % Y,
         lambda y: not (y >> 15) & 1, 15, "\\ssxth\\s=0"),
        ("c_shl24", "i32", "(i32.lt_s (i32.shl %s (i32.const 24)) (i32.const 0))" % Y,
         lambda y: (y >> 7) & 1, 7, "\\slsl\\s=0"),
        ("c_shl31", "i32", "(i32.lt_s (i32.shl %s (i32.const 31)) (i32.const 0))" % Y,
         lambda y: y & 1, 0, "\\slsl\\s=0"),
        ("c_shl35", "i32", "(i32.lt_s (i32.shl %s (i32.const 35)) (i32.const 0))" % Y,
         lambda y: (y >> 28) & 1, 28, "\\slsl\\s=0"),
        ("c_rotl1", "i32", "(i32.lt_s (i32.rotl %s (i32.const 1)) (i32.const 0))" % Y,
         lambda y: (y >> 30) & 1, 30, "\\sror\\s=0"),
        ("c_rotr1", "i32", "(i32.lt_s (i32.rotr %s (i32.const 1)) (i32.const 0))" % Y,
         lambda y: y & 1, 0, "\\sror\\s=0"),
        ("c_shrs4", "i32", "(i32.lt_s (i32.shr_s %s (i32.const 4)) (i32.const 0))" % Y,
         lambda y: (y >> 31) & 1, 31, "\\sasr\\s=0"),
        ("c_twostep", "i32", "(i32.lt_s (i32.extend8_s (i32.shr_u %s (i32.const 8))) (i32.const 0))" % Y,
         lambda y: (y >> 15) & 1, 15, "\\ssxtb\\s=0"),
        ("c_unsigned", "i32", "(i32.ge_u (i32.shl %s (i32.const 20)) (i32.const 0x80000000))" % Y,
         lambda y: (y >> 11) & 1, 11, "\\slsl\\s=0"),
        ("c64_ext32", "i64", "(i64.lt_s (i64.extend32_s %s) (i64.const 0))" % Y,
         lambda y: (y >> 31) & 1, 31, "\\ssxtw\\s=0"),
        ("c64_shl40", "i64", "(i64.lt_s (i64.shl %s (i64.const 40)) (i64.const 0))" % Y,
         lambda y: (y >> 23) & 1, 23, "\\slsl\\s=0"),
        ("c64_shl20", "i64", "(i64.ge_s (i64.shl %s (i64.const 20)) (i64.const 0))" % Y,
         lambda y: not (y >> 43) & 1, 43, "\\slsl\\s=0"),
    ]
    for nm, ty, body, cond, bit, nomore in chains:
        case(nm, ty, body, lambda y, cond=cond: bool(cond(y)), 1,
             "a sign through a chain: bit %d of y" % bit,
             ("\\stb(n)?z\\s+%s\\d+, #0x%x\\b=1" % ("w" if bit < 32 else "x", bit), nomore))
        nbr[0] += 1
    # i64 sign of an i32's extension, i32 sign of an i64's wrap
    case("c64_extends", "i64", "(i64.lt_s (i64.extend_i32_s %s) (i64.const 0))" % Y,
         lambda y: (y >> 31) & 1, 1, "an i64 sign of an i32 widened with its sign: bit 31 of the i32",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1", "\\ssxtw\\s=0"), yty="i32")
    nbr[0] += 1
    case("c_wrap", "i32", "(i32.lt_s (i32.wrap_i64 %s) (i32.const 0))" % Y,
         lambda y: (y >> 31) & 1, 1, "an i32 sign of an i64 wrapped: bit 31 of the i64",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1",), yty="i64")
    nbr[0] += 1

    # chains under a test of one bit
    bits = [
        ("b_shru5", "i32", "(i32.and (i32.shr_u %s (i32.const 5)) (i32.const 1))" % Y,
         lambda y: (y >> 5) & 1, 5, "\\slsr\\s=0"),
        ("b_shru3_4", "i32", "(i32.ne (i32.and (i32.shr_u %s (i32.const 3)) (i32.const 4)) (i32.const 0))" % Y,
         lambda y: (y >> 5) & 1, 5, "\\slsr\\s=0"),
        ("b_eqz_shl", "i32", "(i32.eqz (i32.and (i32.shl %s (i32.const 2)) (i32.const 8)))" % Y,
         lambda y: not (y >> 1) & 1, 1, "\\slsl\\s=0"),
        ("b_shrs", "i32", "(i32.and (i32.shr_s %s (i32.const 20)) (i32.const 0x4000))" % Y,
         lambda y: (y >> 31) & 1, 31, "\\sasr\\s=0"),
        ("b_rotr", "i32", "(i32.and (i32.rotr %s (i32.const 4)) (i32.const 0x80000000))" % Y,
         lambda y: (y >> 3) & 1, 3, "\\sror\\s=0"),
        ("b_two", "i32", "(i32.and (i32.shr_u (i32.shl %s (i32.const 4)) (i32.const 8)) (i32.const 1))" % Y,
         lambda y: (y >> 4) & 1, 4, "\\sls(l|r)\\s=0"),
        ("b64_shru40", "i64", "(i64.ne (i64.and (i64.shr_u %s (i64.const 40)) (i64.const 1)) (i64.const 0))" % Y,
         lambda y: (y >> 40) & 1, 40, "\\slsr\\s=0"),
        ("b64_shl", "i64", "(i64.eqz (i64.and (i64.shl %s (i64.const 33)) (i64.const 0x8000000000000000)))" % Y,
         lambda y: not (y >> 30) & 1, 30, "\\slsl\\s=0"),
    ]
    for nm, ty, body, cond, bit, nomore in bits:
        case(nm, ty, body, lambda y, cond=cond: bool(cond(y)), 1,
             "one bit through a chain: bit %d of y" % bit,
             ("\\stb(n)?z\\s+%s\\d+, #0x%x\\b=1" % ("w" if bit < 32 else "x", bit), nomore))
        nbr[0] += 1
    # a bit of an i32's extension, of an i64's wrap
    case("b64_extends", "i64", "(i64.ne (i64.and (i64.extend_i32_s %s) (i64.const 0x10000000000)) (i64.const 0))" % Y,
         lambda y: (y >> 31) & 1, 1, "bit 40 of an i32 widened with its sign: its bit 31",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1", "\\ssxtw\\s=0"), yty="i32")
    nbr[0] += 1
    case("b64_extendu", "i64", "(i64.ne (i64.and (i64.extend_i32_u %s) (i64.const 0x40)) (i64.const 0))" % Y,
         lambda y: (y >> 6) & 1, 1, "bit 6 of an i32 widened without its sign: its bit 6",
         ("\\stb(n)?z\\s+w\\d+, #0x6\\b=1",), yty="i32")
    nbr[0] += 1
    case("b_wrap", "i32", "(i32.and (i32.wrap_i64 %s) (i32.const 0x200))" % Y,
         lambda y: (y >> 9) & 1, 1, "bit 9 of an i64 wrapped: the i64's bit 9",
         ("\\stb(n)?z\\s+w\\d+, #0x9\\b=1",), yty="i64")
    nbr[0] += 1

    # a value of one bit, the rest known 0: a test of that bit
    case("o_shru31", "i32", "(i32.shr_u %s (i32.const 31))" % Y,
         lambda y: (y >> 31) & 1, 1, "a branch on y >> 31: its sign",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1", "\\slsr\\s=0"))
    nbr[0] += 1
    case("o_eqz_shru31", "i32", "(i32.eqz (i32.shr_u %s (i32.const 31)))" % Y,
         lambda y: not (y >> 31) & 1, 1, "a branch on eqz of y >> 31",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1", "\\slsr\\s=0"))
    nbr[0] += 1
    case("o_bfx", "i32", "(i32.shr_u (i32.shl %s (i32.const 26)) (i32.const 31))" % Y,
         lambda y: (y >> 5) & 1, 1, "a branch on a one-bit field: bit 5 of y",
         ("\\stb(n)?z\\s+w\\d+, #0x5\\b=1", "\\subfx\\s=0"))
    nbr[0] += 1
    case("o_sbfx", "i32", "(i32.lt_s (i32.shr_s (i32.shl %s (i32.const 20)) (i32.const 28)) (i32.const 0))" % Y,
         lambda y: (y >> 11) & 1, 1, "the sign of a signed field, bits 8 to 11: bit 11 of y",
         ("\\stb(n)?z\\s+w\\d+, #0xb\\b=1", "\\ssbfx\\s=0"))
    nbr[0] += 1
    case("o_sbfx_top", "i32", "(i32.and (i32.shr_s (i32.shl %s (i32.const 20)) (i32.const 28)) (i32.const 16))" % Y,
         lambda y: (y >> 11) & 1, 1, "bit 4 of a signed field of bits 8 to 11, one past its top: its sign, bit 11 of y",
         ("\\stb(n)?z\\s+w\\d+, #0xb\\b=1", "\\ssbfx\\s=0"))
    nbr[0] += 1
    case("o_ubfx_sign", "i32", "(i32.lt_s (i32.shr_u (i32.shl %s (i32.const 20)) (i32.const 28)) (i32.const 0))" % Y,
         lambda y: False, 1, "the sign of an unsigned field of bits 8 to 11: 0, the field's value tested")
    nbr[0] += 1
    case("o_xor", "i32", "(i32.xor (i32.and %s (i32.const 1)) (i32.const 1))" % Y,
         lambda y: not y & 1, 0, "a value of one bit no chain makes: its own value tested")
    case("o64_shru63", "i64", "(i32.wrap_i64 (i64.shr_u %s (i64.const 63)))" % Y,
         lambda y: (y >> 63) & 1, 1, "a branch on the wrap of an i64 >> 63",
         ("\\stb(n)?z\\s+x\\d+, #0x3f\\b=1", "\\slsr\\s=0"))
    nbr[0] += 1

    # near misses: the chain stops where it must, the test still a tbz
    case("n_extendu_hi", "i64",
         "(i64.ne (i64.and (i64.extend_i32_u %s) (i64.const 0x10000000000)) (i64.const 0))" % Y,
         lambda y: False, 1, "bit 40 of an i32 widened without its sign: no bit of the i32, "
         "the widened value tested", ("\\stb(n)?z\\s+x\\d+, #0x28\\b=1",), yty="i32")
    # the same, its i32 an i64 wrapped, whose high half the wrap leaves in the
    # register: no test of the i64's bit 40
    funcs.append("  ;; an i32 made by a wrap widened again without its sign: its bit 40 is 0")
    funcs.append("  (func $n_extendu_wrap (export \"n_extendu_wrap\") (param $y i64) (result i32)")
    funcs.append("    (i32.add (call $n_extendu_hi (i32.wrap_i64 (local.get $y))) (i32.const 1)))")
    for v in vals("i64"):
        calls.append(";; run: n_extendu_wrap %d => i32:10" % v)
    case("n_shru_past", "i32", "(i32.and (i32.shr_u %s (i32.const 28)) (i32.const 16))" % Y,
         lambda y: False, 1, "bit 4 of y >> 28: past y's bits, the shift's value tested",
         ("\\stb(n)?z\\s+w\\d+, #0x4\\b=1", "\\slsr\\s=1"))
    case("n_shl_low", "i32", "(i32.and (i32.shl %s (i32.const 8)) (i32.const 16))" % Y,
         lambda y: False, 1, "bit 4 of y << 8: a zero the shift put in, the shift's value tested",
         ("\\stb(n)?z\\s+w\\d+, #0x4\\b=1", "\\slsl\\s=1"))
    case("n_varshift", "i32",
         "(i32.lt_s (i32.shl %s (i32.and %s (i32.const 3))) (i32.const 0))" % (Y, Y),
         lambda y: (((y << (y & 3)) & m32) >> 31) & 1, 1,
         "a sign of a shift by a variable: the shift's value tested",
         ("\\stb(n)?z\\s+w\\d+, #0x1f\\b=1", "\\slsl\\s=1"))
    nbr[0] += 1
    funcs.append("  ;; an extension something else reads too: the sign of its own value")
    funcs.append("  (func $n_twouse (export \"n_twouse\") (param $y i32) (result i32)")
    funcs.append("    (local $e i32)")
    funcs.append("    (if (i32.lt_s (local.tee $e (i32.extend8_s (local.get $y))) (i32.const 0))")
    funcs.append("      (then (i32.store (i32.const 0) (i32.const 70)))")
    funcs.append("      (else (i32.store (i32.const 4) (i32.const 9))))")
    funcs.append("    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (local.get $e)))")
    shapes.append("n_twouse \\stb(n)?z\\s+w\\d+, #0x1f\\b=1")
    shapes.append("n_twouse \\ssxtb\\s=1")
    nbr[0] += 1
    for v in vals("i32"):
        e = sg(v, 8)
        calls.append(";; run: n_twouse %d => i32:%d" % (v, sg((70 if e < 0 else 9) + e, 32)))
    funcs.append("  ;; an extension made in another block: the sign of its own value")
    funcs.append("  (func $n_otherblock (export \"n_otherblock\") (param $y i32) (param $z i32) (result i32)")
    funcs.append("    (local $e i32)")
    funcs.append("    (local.set $e (i32.extend8_s (local.get $y)))")
    funcs.append("    (if (local.get $z) (then (i32.store (i32.const 8) (local.get $z))))")
    funcs.append("    (if (i32.lt_s (local.get $e) (i32.const 0))")
    funcs.append("      (then (i32.store (i32.const 0) (i32.const 70)))")
    funcs.append("      (else (i32.store (i32.const 4) (i32.const 9))))")
    funcs.append("    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))")
    shapes.append("n_otherblock \\ssxtb\\s=1")
    nbr[0] += 1
    for v in vals("i32"):
        for z in (0, 3):
            calls.append(";; run: n_otherblock %d %d => i32:%d" % (v, z, 70 if sg(v, 8) < 0 else 9))

    out = []
    w = out.append
    w(";; arm64 makes a branch on a sign a tbz or tbnz of the sign bit -- every")
    w(";; signed and unsigned compare that tests one, i32 and i64, each way round")
    w(";; -- and follows the bit a sign test or a one-bit test reads back through")
    w(";; the shifts, rotates, extensions and wraps made for the test alone, each")
    w(";; then no instruction (isel.c tbit_sign, tbit_chain).  The near misses:")
    w(";; compares one off a sign test, a bit no bit of the chain's start gives, a")
    w(";; shift by a variable, an extension something else reads or made in")
    w(";; another block.  Each branch stores 70 and the function's number at 0, or")
    w(";; 9 at 4, and returns the sum, so that no if-conversion takes it and no two")
    w(";; functions are one.")
    w(";; Made by test/gen_tbit_sign.py, which works out every answer.")
    w(";;")
    w(";; stats -O2 a64: bit_brs=%d" % nbr[0])
    w(";;")
    for sh in shapes:
        w(";; shape: %s" % sh)
    w(";;")
    out.extend(calls)
    w("(module")
    w("  (memory 1)")
    out.extend(funcs)
    w(")")
    open(sys.argv[1], "w").write("\n".join(out) + "\n")


main()
