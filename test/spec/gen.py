"""Turn one .wast file's command list into a C driver table.

The commands a spec file gives are a small program -- instantiate, register,
invoke, check -- and for an ahead-of-time compiler the natural shape is a
linked executable with every module compiled into it.  What varies per file
is only the data, so that is all this generates; the logic that runs it lives
in test/spec/driver.c.

Every file's tables go into one executable, so every symbol this emits carries
the file's index: `spec_cmds_12`, and modules named `f12m1_module` to match the
`--symbol-prefix` the harness compiled them with.  test/spec/index.c -- also
generated -- gathers them into the `spec_files[]` the driver walks.
"""

SV = {
    "i32": 0, "i64": 1, "f32": 2, "f64": 3,
    "nullref": 4, "externref": 5, "ref_nonnull": 6,
    "f32_nan_canon": 7, "f32_nan_arith": 8,
    "f64_nan_canon": 9, "f64_nan_arith": 10,
    "v128": 11,
    "either": 12,
    "v128_nan": 13,
}

VREG = {SV["f32"], SV["f64"], SV["v128"], SV["v128_nan"],
        SV["f32_nan_canon"], SV["f32_nan_arith"],
        SV["f64_nan_canon"], SV["f64_nan_arith"]}

NULL_ONLY = {"refnull", "nullref", "nullfuncref", "nullexternref",
             "nullexnref", "nullcontref"}

REF_KINDS = {"funcref", "externref", "anyref", "eqref", "i31ref",
             "structref", "arrayref", "exnref"}

LP_EXACT, LP_CANON, LP_ARITH = 0, 1, 2

MASK64 = 0xFFFFFFFFFFFFFFFF
LANE_BITS = {"i8": 8, "i16": 16, "i32": 32, "i64": 64, "f32": 32, "f64": 64}

CMD = {"module": 0, "register": 1, "invoke": 2, "get": 3}
EXPECT = {"ok": 0, "trap": 1, "exhaustion": 2, "fail_instantiate": 3,
          "exception": 4}

class Unsupported(Exception):
    pass

def value(v):
    """One spec value as (kind, lo, hi, nlanes, pat).  Raises for anything
    not modelled.

    `hi` is the top half of a v128 and zero for everything else.  `nlanes`
    and `pat` describe a v128 whose lanes are not all exact values: two bits
    per lane saying which of them must instead be a NaN of a given class.
    """
    t = v["type"]
    val = v.get("value")
    if t == "i32":
        return SV["i32"], int(val) & 0xFFFFFFFF, 0, 0, 0
    if t == "i64":
        return SV["i64"], int(val) & MASK64, 0, 0, 0
    if t in ("f32", "f64"):
        wide = t == "f64"
        if val == "nan:canonical":
            return SV["f64_nan_canon" if wide else "f32_nan_canon"], 0, 0, 0, 0
        if val == "nan:arithmetic":
            return SV["f64_nan_arith" if wide else "f32_nan_arith"], 0, 0, 0, 0

        return SV[t], int(val) & (MASK64 if wide else 0xFFFFFFFF), 0, 0, 0
    if t == "v128":

        lane = v.get("lane_type")
        if lane not in LANE_BITS:
            raise Unsupported("v128 lane type %s" % lane)
        w = LANE_BITS[lane]
        lanes = val if isinstance(val, list) else [val]
        if len(lanes) * w != 128:
            raise Unsupported("v128 with %d lanes of %s" % (len(lanes), lane))
        bits = 0
        pat = 0
        for i, s in enumerate(lanes):
            s = str(s)
            if s in ("nan:canonical", "nan:arithmetic"):
                if lane not in ("f32", "f64"):
                    raise Unsupported("NaN pattern in a %s lane" % lane)
                pat |= (LP_CANON if s == "nan:canonical" else LP_ARITH) << (2 * i)
                continue
            if not s.lstrip("-").isdigit():
                raise Unsupported("v128 lane pattern %s" % s)
            bits |= (int(s) & ((1 << w) - 1)) << (i * w)
        lo, hi = bits & MASK64, (bits >> 64) & MASK64
        if pat:
            return SV["v128_nan"], lo, hi, len(lanes), pat
        return SV["v128"], lo, hi, 0, 0

    if t in NULL_ONLY:
        return SV["nullref"], 0, 0, 0, 0
    if t in REF_KINDS:
        if val == "null":
            return SV["nullref"], 0, 0, 0, 0
        if val is None:
            return SV["ref_nonnull"], 0, 0, 0, 0

        if t in ("externref", "anyref", "eqref", "structref", "arrayref",
                 "i31ref"):
            return SV["externref"], ((int(val) + 1) << 2) | 2, 0, 0, 0
        raise Unsupported("%s with identity %r" % (t, val))
    if t == "either":

        raise Unsupported("either outside a result position")
    raise Unsupported("value type %s" % t)

def result_values(v):
    """One expected *result* as a list of SpecVal tuples.

    Almost always one entry.  A relaxed vector instruction is allowed several
    outcomes and a .wast file states them as `(either A B ...)`; that becomes
    a header carrying the count, followed by the alternatives themselves, and
    the driver walks the result list rather than indexing it.

    Reading `either` as "unconstrained" -- which is what this did until the
    relaxed instructions landed -- makes every assertion in the seven
    relaxed-simd files pass whatever the answer is.  Nothing else in the
    corpus writes one, so that was never executed and never noticed.
    """
    if v["type"] != "either":
        return [value(v)]
    alts = [value(a) for a in v["values"]]
    if not alts:
        raise Unsupported("either with no alternatives")
    return [(SV["either"], len(alts), 0, 0, 0)] + alts

def c_string(s):
    out = '"'
    for ch in s.encode("utf-8"):
        if ch in (0x22, 0x5C):
            out += "\\" + chr(ch)
        elif 0x20 <= ch < 0x7F:
            out += chr(ch)
        else:
            out += "\\%03o" % ch
    return out + '"'

def generate(spec, module_files, stem, idx):
    """spec: the parsed json.  module_files: [(index, filename)] in order.

    `idx` is the file's slot in the one executable; every emitted symbol is
    suffixed with it, and the modules are `f<idx>m<i>_module`.

    Returns (C source, number of commands, [skipped reasons]).
    """
    sfx = "_%d" % idx
    index_of = {fn: i for i, fn in module_files}
    named = {}
    defs = {}
    lines = []
    vals = []
    cmds = []
    skipped = []
    last_index = None

    extra = []
    nslots = len(module_files) + 1

    def new_slot(mod):
        extra.append(mod)
        return nslots + len(extra) - 1

    def val_array(items):
        if not items:
            return "0"
        label = "v%d%s" % (len(vals), sfx)
        vals.append((label, items))
        return label

    def target_of(action):
        name = action.get("module")
        if name is None:
            return 0xFFFF
        if name not in named:
            raise Unsupported("action names an unknown module %s" % name)
        return named[name]

    def action_cmd(action, expect, line, expected):

        if action.get("module") is None and last_index is None:
            raise Unsupported("no current module")
        if action["type"] == "invoke":
            op = CMD["invoke"]
            args = [value(a) for a in action.get("args", [])]

            if sum(1 for a in args if a[0] in VREG) > 8:
                raise Unsupported("more vector arguments than the trampoline passes")
            if sum(1 for a in args if a[0] not in VREG) > 7:
                raise Unsupported("more integer arguments than the trampoline passes")
        elif action["type"] == "get":
            op = CMD["get"]
            args = []
        else:
            raise Unsupported("action %s" % action["type"])

        res = []
        for r in (expected or []):
            res.extend(result_values(r))
        return (op, expect, target_of(action), action["field"],
                args, res, line, len(expected or []))

    for cmd in spec["commands"]:
        kind = cmd["type"]
        line = cmd.get("line", 0)
        try:
            if kind == "module":
                fn = cmd.get("filename")
                if fn not in index_of:

                    last_index = None
                    raise Unsupported("module not compiled")

                mod = index_of[fn]
                last_index = mod
                if cmd.get("name"):
                    named[cmd["name"]] = mod
                cmds.append((CMD["module"], EXPECT["ok"], mod, "", [], [], line, 0))
            elif kind == "module_definition":

                fn = cmd.get("filename")
                if fn in index_of and cmd.get("name"):
                    defs[cmd["name"]] = index_of[fn]
            elif kind == "module_instance":
                src = defs.get(cmd.get("module"))
                if src is None:
                    raise Unsupported("instance of an unknown definition")
                slot = new_slot(src)
                last_index = slot
                if cmd.get("instance"):
                    named[cmd["instance"]] = slot
                cmds.append((CMD["module"], EXPECT["ok"], slot, "", [], [], line, 0))
            elif kind == "register":
                mod = named.get(cmd.get("name"), last_index)
                if mod is None:
                    raise Unsupported("register with no module")
                cmds.append((CMD["register"], EXPECT["ok"], mod, cmd["as"], [], [], line, 0))
            elif kind == "action":
                cmds.append(action_cmd(cmd["action"], EXPECT["ok"], line, []))
            elif kind == "assert_return":
                cmds.append(action_cmd(cmd["action"], EXPECT["ok"], line, cmd.get("expected")))
            elif kind == "assert_trap":
                cmds.append(action_cmd(cmd["action"], EXPECT["trap"], line, []))
            elif kind == "assert_exhaustion":
                cmds.append(action_cmd(cmd["action"], EXPECT["exhaustion"], line, []))
            elif kind == "assert_exception":
                cmds.append(action_cmd(cmd["action"], EXPECT["exception"], line, []))
            elif kind in ("assert_uninstantiable", "assert_unlinkable"):
                fn = cmd.get("filename")
                if fn not in index_of:
                    raise Unsupported("module not compiled")
                cmds.append((CMD["module"], EXPECT["fail_instantiate"],
                             index_of[fn], "", [], [], line, 0))
            else:
                continue
        except Unsupported as e:
            skipped.append("line %s: %s" % (line, e))

    lines.append('/* Generated from %s -- do not edit. */' % stem)
    lines.append('#include "spec_tables.h"')
    lines.append("")

    lines.append("extern const ModuleDesc spectest_module;")
    for i, fn in module_files:
        lines.append("extern const ModuleDesc f%dm%d_module;" % (idx, i))
    lines.append("const ModuleDesc *const spec_modules%s[] = {" % sfx)
    lines.append("  &spectest_module,")
    for i, fn in module_files:
        lines.append("  &f%dm%d_module," % (idx, i))

    for mod in extra:
        lines.append("  &f%dm%d_module,   /* another instance */" % (idx, mod))
    lines.append("  0")
    lines.append("};")
    lines.append('const char *const spec_module_names%s[] = {' % sfx)
    for _ in range(nslots + len(extra)):
        lines.append('  "",')
    lines.append('  ""')
    lines.append("};")
    lines.append("")

    body = ['  { %d, %d, 0, 0, 0, 0, 0, 0, 0, 0 },' % (CMD["module"], EXPECT["ok"]),
            '  { %d, %d, 0, "spectest", 8, 0, 0, 0, 0, 0 },'
            % (CMD["register"], EXPECT["ok"])]
    for op, expect, mod, name, args, res, line, nres in cmds:
        a = val_array(args)
        r = val_array(res)

        body.append("  { %d, %d, %d, %s, %d, %d, %d, %s, %s, %d }," %
                    (op, expect, mod,
                     c_string(name) if name is not None else "0",
                     len(name.encode("utf-8")) if name is not None else 0,
                     len(args), nres,
                     a if a == "0" else a, r if r == "0" else r, line))

    for label, items in vals:
        lines.append("static const SpecVal %s[] = { %s };" %
                     (label, ", ".join("{%d,%d,%du,0x%xull,0x%xull}"
                                       % (k, nl, pat, lo, hi)
                                       for k, lo, hi, nl, pat in items)))
    lines.append("")
    lines.append("const SpecCmd spec_cmds%s[] = {" % sfx)
    lines.extend(body)
    lines.append("  { 0, 0, 0, 0, 0, 0, 0, 0, 0 }")
    lines.append("};")
    lines.append("")
    return "\n".join(lines) + "\n", len(cmds), skipped

def generate_index(entries):
    """entries: [(idx, wast_name, ncmds)] for the files that built.

    The command counts are baked in here rather than exported as
    `const unsigned` from each table: a `const` object is not a constant
    expression in C, so it cannot initialise the static array below.
    """
    lines = ['/* Generated -- do not edit. */', '#include "spec_tables.h"', ""]
    for idx, _name, _n in entries:
        lines.append("extern const ModuleDesc *const spec_modules_%d[];" % idx)
        lines.append("extern const char *const spec_module_names_%d[];" % idx)
        lines.append("extern const SpecCmd spec_cmds_%d[];" % idx)
    lines.append("")
    lines.append("const SpecFile spec_files[] = {")
    for idx, name, n in entries:
        lines.append("  { %s, spec_modules_%d, spec_module_names_%d, spec_cmds_%d, %d },"
                     % (c_string(name), idx, idx, idx, n))
    lines.append("};")
    lines.append("const unsigned spec_nfiles = %d;" % len(entries))
    lines.append("")
    return "\n".join(lines) + "\n"
