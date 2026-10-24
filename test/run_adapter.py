"""Run the canonical ABI's conversion code against the specification's own.

`warmc --dump-adapter` emits, per value type, the directions the canonical ABI
moves a value in: `load_N` reads one out of linear memory and returns it flat,
`store_N` takes it flat and writes it back, and `move_N` takes it flat and
returns it flat -- which is not the identity, because whatever the value owns
belongs to the side it came from and the other side has to be given storage of
its own.  This checks that code by *executing* it.

How.  For each type, a value is generated and handed to the reference
implementation, which says three things about it: the bytes `store` lays down,
the flat values `lower_flat` produces, and what comes back out of
`lower_flat(lift_flat(...))`.  All three become constants in a generated
module that calls warmc's adapter and traps on any disagreement -- so the
module is self-checking, needs no host bindings, and a pass means the emitted
code agreed with the specification on every byte and every flat value.

The three run in one order and share one cursor, because the reference's do:
load, then store, then move, each allocating above the last.  Reordering them
would leave every pointer downstream somewhere else.

Why this and not a golden file.  test/run_canon.py already checks what warmc
*computes* about a type -- size, alignment, the flat signature.  None of that
says the emitted loads and stores are at the right offsets with the right
widths, and a wrong offset is exactly the kind of mistake that leaves every
computed number correct.

Lists allocate, which is the whole difficulty.  A list's two representations
are not the same bytes in two places: moving one means new storage and a copy,
so the pointer the reference computes and the pointer warmc computes have to be
the same pointer or nothing can be compared.  They are, because both sides run
the *same* bump allocator from the same cursor, reset before each value -- and
the region it hands out is compared as well as the value itself, which is what
catches a copy that wrote the right pointer and the wrong bytes.

One check is emitted and cannot be tested here: a list whose element count
times element size overflows 32 bits.  The specification traps on it, and the
emitted code does, but in a test memory of a megabyte any length large enough
to reach it also walks off the end of memory a moment later -- which traps too.
Removing the check is therefore invisible to this harness.  It stays because a
four-gigabyte memory would need it; it is named here so it is not mistaken for
something the pass count covers.

What is not covered.  A type the emitter still refuses is named by
`--dump-adapter` rather than skipped silently, and this counts them, which is
what stops the coverage from quietly shrinking.  NaNs are not generated: the
reference's `maybe_scramble_nan` may rewrite one under the deterministic
profile and the emitted code does a plain load, which is a real question about
adapters and a separate one from layout.

Nor is a move between two *different* string encodings, which is the one thing
`emit_move` takes two sets of options for -- and the reason is worth stating,
because it decided the shape of the whole-adapter check below.  A transcode
does not land where the reference lands: the reference allocates a worst case
and shrinks it, the emitted code decodes into a scalar array and encodes out
of that, and the specification requires neither sequence.  So the pointer is
not comparable, and a pointer is the only handle this harness has on what is
inside a nested value.  The nine movers are checked one level down, as
transcodings, by content.

`adapt_N` is a whole one: a lower fused with a lift, the callee's own core
function, a lift fused with a lower, and post-return, in that order because
post-return is what frees the memory the results were still sitting in.  It
comes in two shapes and the second is where the checking is.  When the value
is one flat value on each side there is nothing to allocate and nothing in
memory to compare, so all that can be said is that it came back unchanged.
When it spills -- which is every type of more than one flat value, since
MAX_FLAT_RESULTS is one -- the answer is bytes at a pointer the caller
supplied, and those are compared against the reference's own, together with
everything the call allocated on the way.

That shape is also what makes the two conversions separable, which the round
trip alone never was: with an identity callee, breaking either conversion
leaves the other to range-check the discriminant and trap anyway, so an
ablation of either was invisible.  A spilled callee is not the identity -- it
stores into memory of its own and hands back a pointer, which the argument
path cannot have produced -- and now removing either direction is caught.

Four pointers cross a spilled call and each gets the two checks
`lift_flat_values` and `lower_flat_values` state: the arguments the caller
points at, what the callee returned, and where the caller wants the results.
Neither check happens by itself.  wasm loads and stores through a misaligned
address without complaint, and a block can end past the end of memory while
every byte the value occupies is still inside it -- which is what trailing
padding leaves room for, and the only geometry where the bounds trap is
distinguishable from wasm's own.  The `atrap_` probes supply each bad address
in turn; the one address neither side of a call can produce is a bad *result*
pointer, so the module carries a `$skew` its callee adds, rather than a second
callee built to be broken.

A rule this checked only by accident, until it was ablated and nothing
noticed: a variant case narrower than the join leaves payload slots unused,
and a lowering must write zero into them.  Every input above came from the
reference, which had already zeroed them, so passing them straight through
agreed with all of it.  `pads` is the probe with garbage in those slots.
"""

import os
import random
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_canon as rc

WASMTIME = os.environ.get("WASMTIME", "wasmtime")
VALUES = int(os.environ.get("ADAPTER_VALUES", "6"))

failures = 0

moves = 0
padprobes = 0
adapted = 0
spilled = 0
atraps = 0

normprobes = 0

CALLEE_ENC = "utf8"

def fail(what):
    global failures
    failures += 1
    print("FAIL[adapter]: %s" % what)

def gen(d, t, rng):
    """A random value of `t`, in the shape the reference expects: a record is a
    dict by label, a variant is a one-entry dict, flags are a dict of bools."""
    t = d.despecialize(t)
    if isinstance(t, d.BoolType):
        return rng.random() < 0.5
    for ty, lo, hi in ((d.U8Type, 0, 255), (d.U16Type, 0, 65535),
                       (d.U32Type, 0, 2**32 - 1), (d.U64Type, 0, 2**64 - 1),
                       (d.S8Type, -128, 127), (d.S16Type, -32768, 32767),
                       (d.S32Type, -2**31, 2**31 - 1),
                       (d.S64Type, -2**63, 2**63 - 1)):
        if isinstance(t, ty):

            return rng.choice([lo, hi, 0, 1, rng.randint(lo, hi)])
    if isinstance(t, d.F32Type):
        return rng.choice([0.0, -0.0, 1.5, -2.25, float("inf"),
                           float("-inf"), struct.unpack("<f", struct.pack(
                               "<I", rng.randrange(2**32)))[0]])
    if isinstance(t, d.F64Type):
        return rng.choice([0.0, -0.0, 1.5, -2.25, float("inf"),
                           float("-inf"), struct.unpack("<d", struct.pack(
                               "<Q", rng.randrange(2**64)))[0]])
    if isinstance(t, d.CharType):
        c = rng.randrange(0x110000)
        while 0xD800 <= c <= 0xDFFF:
            c = rng.randrange(0x110000)
        return chr(c)
    if isinstance(t, d.RecordType):
        return {f.label: gen(d, f.t, rng) for f in t.fields}
    if isinstance(t, d.VariantType):

        n = len(t.cases)
        i = rng.choice([0, n - 1] + [k for k in (255, 256, 65535, 65536)
                                     if k < n] + [rng.randrange(n)])
        c = t.cases[i]
        return {c.label: (gen(d, c.t, rng) if c.t is not None else None)}
    if isinstance(t, d.FlagsType):
        return {l: rng.random() < 0.5 for l in t.labels}
    if isinstance(t, (d.OwnType, d.BorrowType)):

        return rng.choice([0, 1, 7, rng.randrange(1 << 30)])
    if isinstance(t, d.StringType):

        pool = [0x00, 0x41, 0x7F, 0x80, 0x7FF, 0x800, 0xFFF, 0xD7FF, 0xE000,
                0xFFFF, 0x10000, 0x10FFFF]
        n = rng.choice([0, 1, 3, 9])
        cs = []
        for _ in range(n):
            c = rng.choice(pool) if rng.random() < 0.6 else rng.randrange(0x110000)
            while 0xD800 <= c <= 0xDFFF:
                c = rng.randrange(0x110000)
            cs.append(chr(c))
        v = "".join(cs)
        return (v, "utf8", len(v.encode("utf-8")))
    if isinstance(t, d.ListType):

        n = t.l if t.l is not None else rng.choice([0, 1, 2, 5, 257])
        return [gen(d, t.t, rng) for _ in range(n)]
    raise ValueError("no generator for %r" % t)

def has_handle(d, t):
    t = d.despecialize(t)
    if isinstance(t, (d.OwnType, d.BorrowType)):
        return True
    return any(has_handle(d, x) for x in
               [f.t for f in getattr(t, "fields", [])] +
               [c.t for c in getattr(t, "cases", []) if c.t is not None] +
               ([t.t] if isinstance(t, d.ListType) else []))

def allocates(d, t):
    """Whether moving a value of this type has to allocate."""
    t = d.despecialize(t)
    if isinstance(t, (d.ListType, d.StringType)):
        return True
    return any(allocates(d, x) for x in
               [f.t for f in getattr(t, "fields", [])] +
               [c.t for c in getattr(t, "cases", []) if c.t is not None])

def has_nan(v):
    if isinstance(v, float):
        return v != v
    if isinstance(v, dict):
        return any(has_nan(x) for x in v.values())
    if isinstance(v, list):
        return any(has_nan(x) for x in v)
    return False

ENCS = {"utf8": "utf8", "utf16": "utf16", "l1u16": "latin1+utf16"}

def encode_source(s, enc):
    """A string in one encoding's memory form: the bytes, and the tagged count
    the canonical ABI stores beside them."""
    if enc == "utf8":
        b = s.encode("utf-8")
        return b, len(b)
    if enc == "utf16":
        b = s.encode("utf-16-le")
        return b, len(b) // 2
    if all(ord(c) < 0x100 for c in s):
        b = s.encode("latin-1")
        return b, len(b)
    b = s.encode("utf-16-le")
    return b, (len(b) // 2) | 0x80000000

def utf16_probes(rng, n):
    """Code-unit sequences for the utf16 check, with Python's verdict.

    All about surrogates: a high one with nothing after it, a low one alone, a
    high one followed by another high one, and the pairs either side of the
    range that are fine."""
    fixed = [
        [], [0x41], [0xD7FF], [0xE000], [0xFFFF],
        [0xD800, 0xDC00], [0xDBFF, 0xDFFF],
        [0xD800], [0xDC00], [0xD800, 0xD800], [0xDC00, 0xDC00],
        [0xD800, 0x41], [0x41, 0xDFFF], [0xD800, 0xDBFF],
    ]
    out = []
    for u in fixed:
        raw = b"".join(x.to_bytes(2, "little") for x in u)
        try:
            raw.decode("utf-16-le")
            out.append((raw, len(u), 1))
        except UnicodeError:
            out.append((raw, len(u), 0))
    for _ in range(n):
        u = []
        for _ in range(rng.randrange(1, 5)):
            if rng.random() < 0.4:
                u.append(rng.randrange(0xD800, 0xE000))
            else:
                u.append(rng.randrange(0x10000))
        raw = b"".join(x.to_bytes(2, "little") for x in u)
        try:
            raw.decode("utf-16-le")
            out.append((raw, len(u), 1))
        except UnicodeError:
            out.append((raw, len(u), 0))
    return out

def xcode_strings(rng, n):
    """Strings that make the nine transcodings differ from one another."""
    fixed = ["", "a", "hello", "\x7f", "\x80", "\xff",
             "\u0100", "\u07ff", "\u0800", "\uffff",
             "\U00010000", "\U0010ffff",
             "a\xffb", "a\u0100b", "a\U0001f600b"]
    out = list(fixed)
    for _ in range(n):
        cs = []
        for _ in range(rng.randrange(0, 6)):
            c = rng.choice([rng.randrange(0x100), rng.randrange(0x800),
                            rng.randrange(0x10000), rng.randrange(0x110000)])
            while 0xD800 <= c <= 0xDFFF:
                c = rng.randrange(0x110000)
            cs.append(chr(c))
        out.append("".join(cs))
    return out

def _lent_then_taken(t):
    return ["i32.const %d" % t, "i32.const 5", "i32.const 1",
            "call $lower_own", "local.set $q",
            "i32.const %d" % t, "local.get $q", "i32.const 1",
            "call $lift_borrow", "drop",

            "i32.const %d" % t, "local.get $q", "i32.const 1",
            "call $lift_own", "drop"]

def _borrowed_then_taken(t):
    return ["i32.const %d" % t, "i32.const 5", "i32.const 1", "i32.const 0",
            "call $lower_borrow", "local.set $q",

            "i32.const %d" % t, "local.get $q", "i32.const 1",
            "call $lift_own", "drop"]

def _wrong_resource(t):
    return ["i32.const %d" % t, "i32.const 5", "i32.const 1",
            "call $lower_own", "local.set $q",
            "i32.const %d" % t, "local.get $q", "i32.const 2",
            "call $lift_own", "drop"]

def _never_added(t):
    return ["i32.const %d" % t, "i32.const 3", "i32.const 1",
            "call $lift_own", "drop"]

def _taken_twice(t):
    return ["i32.const %d" % t, "i32.const 5", "i32.const 1",
            "call $lower_own", "local.set $q",
            "i32.const %d" % t, "local.get $q", "i32.const 1",
            "call $lift_own", "drop",
            "i32.const %d" % t, "local.get $q", "i32.const 1",
            "call $lift_own", "drop"]

def _index_zero(t):

    return ["i32.const %d" % t, "i32.const 0", "i32.const 1",
            "call $lift_own", "drop"]

HANDLE_TRAPS = [
    ("a handle that is out on loan", _lent_then_taken),
    ("a borrowed handle given away", _borrowed_then_taken),
    ("a handle of another resource", _wrong_resource),
    ("an index never handed out", _never_added),
    ("an index already taken", _taken_twice),
    ("the reserved entry", _index_zero),
]

def handle_ops(d, rc, rng, n, src_at, dst_at):
    """A sequence of handle operations, with what the reference makes of each.

    Which index comes back is the whole point: the free list is a stack, so a
    remove and an add give the same index back, and an implementation that
    used a queue or simply appended would be right about everything except the
    numbers -- which are what crosses the boundary.

    Both tables are driven, because a conversion takes from one and gives to
    the other, and an implementation that used one table for both would pass
    every test that only ever looked at one.
    """
    rts = [d.ResourceType(None, None), d.ResourceType(None, None)]
    for i, rt in enumerate(rts):
        rc._RESOURCES[900 + i] = rt
    tables = {"src": d.Table(), "dst": d.Table()}
    addrs = {"src": src_at, "dst": dst_at}
    live = {"src": [], "dst": []}
    ops = []
    for _ in range(n):
        side = rng.choice(["src", "dst"])
        t, a = tables[side], addrs[side]
        rt = rng.choice(rts)
        choices = ["lower_own", "resource_new"]
        mine = [x for x in live[side] if x[1] is rt]
        if mine:
            choices += ["lift_own", "lift_borrow", "resource_rep",
                        "resource_drop"]
        op = rng.choice(choices)
        if op in ("lower_own", "resource_new"):
            rep = rng.randrange(1 << 20)
            h = d.ResourceHandle(rt, rep, own=True)
            i = t.add(h)
            live[side].append((i, rt, True))
            ops.append((op, side, a, rep, rt_id(rc, rt), i))
        elif op == "lift_own":
            owned = [x for x in mine if x[2]]
            if not owned:
                continue
            idx, rt, _ = rng.choice(owned)
            h = t.array[idx]
            if h.num_lends:
                continue
            t.remove(idx)
            live[side].remove((idx, rt, True))
            ops.append((op, side, a, idx, rt_id(rc, rt), h.rep))
        elif op == "lift_borrow":
            idx, rt, own = rng.choice(mine)
            h = t.get(idx)
            h.num_lends += 1
            ops.append((op, side, a, idx, rt_id(rc, rt), h.rep))
        elif op == "resource_rep":
            idx, rt, own = rng.choice(mine)
            ops.append((op, side, a, idx, rt_id(rc, rt), t.get(idx).rep))
        else:
            idx, rt, own = rng.choice(mine)
            h = t.get(idx)
            if h.num_lends:
                continue
            t.remove(idx)
            live[side].remove((idx, rt, own))
            ops.append((op, side, a, idx, rt_id(rc, rt),
                        (1 if h.own else 0, h.rep)))
    return ops

def utf8_probes(rng, n):
    """Byte strings for the validator, with the answers Python gives.

    Mostly near-misses, because that is where a decoder is wrong: an overlong
    form, a surrogate encoded as if it were a scalar, a code point past
    U+10FFFF, a sequence cut off by the end of the buffer.  Each is a valid
    encoding of *something* under a decoder that only counts continuation
    bytes."""
    fixed = [
        b"", b"\x41", b"\x7f", b"\xc2\x80", b"\xdf\xbf", b"\xe0\xa0\x80",
        b"\xef\xbf\xbf", b"\xf0\x90\x80\x80", b"\xf4\x8f\xbf\xbf",
        b"\x80", b"\xbf", b"\xc0\x80", b"\xc1\xbf",
        b"\xe0\x80\x80", b"\xe0\x9f\xbf",
        b"\xed\xa0\x80", b"\xed\xbf\xbf",
        b"\xed\x9f\xbf",
        b"\xf0\x80\x80\x80", b"\xf0\x8f\xbf\xbf",
        b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80", b"\xff",
        b"\xe2\x82", b"\xf0\x9f\x98",
        b"\xc2", b"\xe2\x82\xac\xc2",
    ]
    out = [(b, True) for b in ()]
    for b in fixed:
        try:
            b.decode("utf-8")
            out.append((b, 1))
        except UnicodeError:
            out.append((b, 0))
    for _ in range(n):
        cs = []
        for _ in range(rng.randrange(1, 5)):
            c = rng.randrange(0x110000)
            while 0xD800 <= c <= 0xDFFF:
                c = rng.randrange(0x110000)
            cs.append(chr(c))
        raw = bytearray("".join(cs).encode("utf-8"))
        for _ in range(rng.randrange(0, 3)):
            if raw:
                raw[rng.randrange(len(raw))] = rng.randrange(256)
        if raw and rng.random() < 0.3:
            del raw[rng.randrange(len(raw)):]
        raw = bytes(raw)
        try:
            raw.decode("utf-8")
            out.append((raw, 1))
        except UnicodeError:
            out.append((raw, 0))
    return out

class Mem(bytearray):
    def ptr_type(self):
        return "i32"

    def ptr_size(self):
        return 4

class RefOpts:
    def __init__(self, mem):
        self.memory = mem
        self.string_encoding = "utf8"
        self.async_ = False
        self.callback = None

class FakeSubtask:
    """A borrow scope for the lifting side.

    `lift_borrow` asserts its scope is a Subtask and calls `add_lender`, which
    is what increments the lender count -- the thing that stops an owner from
    giving away a handle somebody else is holding.  Subclassing keeps the
    reference's own `add_lender` rather than restating it; only the task
    lifecycle it consults is stubbed."""

    def resolve_delivered(self):
        return False

    def resolved(self):
        return False

class FakeInst:
    """Just the handle table, which is all `lift_own` and `lower_own` reach."""

    def __init__(self, d):
        self.handles = d.Table()

TABLE_CAP = 256
TABLE_BYTES = 16 + TABLE_CAP * 16 + TABLE_CAP * 4

def rt_id(rc, rt):
    """The number the emitted code uses for a resource, which is its index in
    the component's type space plus one -- zero means an empty slot."""
    for i, obj in rc._RESOURCES.items():
        if obj is rt:
            return (i or 0) + 1
    return 1

def serialise_table(rc, table):
    """A reference `Table` in the layout the emitted code reads."""
    out = bytearray(TABLE_BYTES)
    out[0:4] = len(table.array).to_bytes(4, "little")
    out[4:8] = TABLE_CAP.to_bytes(4, "little")
    out[8:12] = len(table.free).to_bytes(4, "little")
    for i, h in enumerate(table.array):
        if h is None:
            continue
        o = 16 + i * 16
        out[o:o + 4] = rt_id(rc, h.rt).to_bytes(4, "little")
        out[o + 4:o + 8] = (h.rep & 0xFFFFFFFF).to_bytes(4, "little")
        out[o + 8:o + 12] = (1 if h.own else 0).to_bytes(4, "little")
        out[o + 12:o + 16] = h.num_lends.to_bytes(4, "little")
    base = 16 + TABLE_CAP * 16
    for k, i in enumerate(table.free):
        out[base + k * 4:base + k * 4 + 4] = i.to_bytes(4, "little")
    return bytes(out)

def table_init(mem, at):
    mem[at:at + TABLE_BYTES] = bytes(TABLE_BYTES)

    mem[at:at + 4] = (1).to_bytes(4, "little")
    mem[at + 4:at + 8] = TABLE_CAP.to_bytes(4, "little")

class RefCx:
    """Enough of the reference's context for `store` and `lower_flat`.

    `reallocate` is the bump allocator the emitted module also carries, to the
    letter -- if the two disagree by a byte every pointer downstream differs
    and nothing about the copy can be checked."""

    def __init__(self, mem, bump, inst=None):
        self.opts = RefOpts(mem)
        self.inst = inst
        self.borrow_scope = None
        self.bump = bump

    def reallocate(self, old, old_len, align, new_len):
        p = (self.bump + align - 1) // align * align
        self.bump = p + new_len

        if old and old_len:
            n = min(old_len, new_len)
            self.opts.memory[p:p + n] = self.opts.memory[old:old + n]
        return p

    def allocate(self, align, byte_length):
        return self.reallocate(0, 0, align, byte_length)

def const_for(flat, v):
    """The instructions that push `v` as a value of core type `flat`.

    Floats go through their bits.  Comparing them as floats would make the
    check disagree with itself on -0.0 and agree wrongly on NaN, and the point
    here is the bytes."""
    if flat == "i32":
        return ["i32.const %d" % (v - 2**32 if v >= 2**31 else v)]
    if flat == "i64":
        return ["i64.const %d" % (v - 2**64 if v >= 2**63 else v)]
    if flat == "f32":
        return ["i32.const %d" % struct.unpack("<i", struct.pack("<f", v))[0],
                "f32.reinterpret_i32"]
    return ["i64.const %d" % struct.unpack("<q", struct.pack("<d", v))[0],
            "f64.reinterpret_i64"]

def compare_to(flat, v, local):
    """Instructions that trap unless local `local` holds `v`."""
    if flat == "f32":
        bits = struct.unpack("<i", struct.pack("<f", v))[0]
        return ["local.get %s" % local, "i32.reinterpret_f32",
                "i32.const %d" % bits, "i32.ne", "if", "unreachable", "end"]
    if flat == "f64":
        bits = struct.unpack("<q", struct.pack("<d", v))[0]
        return ["local.get %s" % local, "i64.reinterpret_f64",
                "i64.const %d" % bits, "i64.ne", "if", "unreachable", "end"]
    op = "i32" if flat == "i32" else "i64"
    c = const_for(flat, v)
    return ["local.get %s" % local] + c + ["%s.ne" % op, "if", "unreachable", "end"]

LOAD_SIG = re.compile(r'\(func \(export "load_([0-9_]+)"\) \(param \$p i32\) '
                      r'\(result([^)]*)\)')

ADAPT_SIG = re.compile(r'\(func \(export "adapt_([0-9_]+)"\)([^\n]*)')

def written_span(d, v, t, size):
    """The highest byte of `t`'s stored form the reference actually writes.

    Two fills and not one sentinel: a byte the store leaves alone keeps what
    the fill put there, so the bytes that *differ* between two fills are
    exactly the ones it never touched.  A single sentinel would call a real
    0xAA a hole and stop looking too early.

    Trailing untouched bytes are what makes the specification's bounds trap
    separable from wasm's own.  A block can end past memory while every byte
    the value occupies is still inside it, and there the specification traps
    and a plain load does not."""
    outs = []
    for fill in (0xAA, 0x55):
        m = Mem(bytes([fill]) * (size + (1 << 16)))
        cx = RefCx(m, size + 16)
        d.store(cx, v, t, 0)
        outs.append(bytes(m[:size]))
    hi = -1
    for j in range(size):
        if outs[0][j] == outs[1][j]:
            hi = j
    return hi

def prim_members(d, t):
    """Every `bool` and `char` in `t`, as (flat slot, byte offset, which), or
    None when `t` is not a shape where one slot can be changed on its own.

    A record or tuple of primitives is that shape: one flat value and one
    field per member, at offsets the layout fixes.  A variant is not -- its
    cases share slots, so writing a slot changes what the others mean -- and
    neither is anything behind a pointer.  Narrowing to the shape where the
    probe is unambiguous is what makes it a check about `bool` and `char`
    rather than about the walk that reached them."""
    dt = d.despecialize(t)
    if isinstance(dt, d.BoolType):
        return [(0, 0, "bool")]
    if isinstance(dt, d.CharType):
        return [(0, 0, "char")]
    # Flags are not a primitive, but they normalise like one: the ABI keeps
    # only the low len(labels) bits, and every emitter here used to pass the
    # word through untouched.  The generator cannot reach that on its own --
    # it builds a flags value as a dict of bools, so the reference packs it
    # clean before warmc ever sees it -- so the probe has to set the spare
    # bits itself.
    if isinstance(dt, d.FlagsType) and 0 < len(dt.labels) < 32:
        return [(0, 0, "flags:%d" % len(dt.labels))]
    if isinstance(dt, d.RecordType):
        ts = [f.t for f in dt.fields]
    elif isinstance(dt, d.TupleType):
        ts = list(dt.ts)
    else:
        return None
    out, at, slot = [], 0, 0
    for mt in ts:
        mdt = d.despecialize(mt)

        if not isinstance(mdt, d.PrimValType) or isinstance(mdt, d.StringType):
            return None
        a = d.alignment(mt, "i32")
        at = (at + a - 1) // a * a
        for cls, which in ((d.BoolType, "bool"), (d.CharType, "char"),
                           (d.U8Type, "u8"), (d.S8Type, "s8"),
                           (d.U16Type, "u16"), (d.S16Type, "s16")):
            if isinstance(mdt, cls):
                out.append((slot, at, which))
                break
        at += d.elem_size(mt, "i32")
        slot += 1
    return out

def list_prim(d, t):
    """`bool` or `char` when `t` is a list of exactly that, else None.

    The only route to `emit_copy` carrying a primitive: every other shape
    converts through the flat form, and a list is the one whose elements go
    memory to memory one at a time."""
    dt = d.despecialize(t)
    if not isinstance(dt, d.ListType) or dt.l is not None:
        return None
    et = d.despecialize(dt.t)
    if isinstance(et, d.BoolType):
        return "bool"
    if isinstance(et, d.CharType):
        return "char"
    return None

PRIM_PROBES = {
    "bool": (7, 1, ("move", "load", "store")),
    "u8":   (0x1FF, 0xFF, ("move",)),
    "s8":   (0x1FF, -1, ("move",)),
    "u16":  (0x1FFFF, 0xFFFF, ("move",)),
    "s16":  (0x1FFFF, -1, ("move",)),
}

def past_end(memtop, align, size, lastb):
    """An aligned address whose block runs past `memtop` while every byte the
    value occupies is still below it, or None when the geometry has no room
    for one -- which is most types, since a stored form is padded up to its
    own alignment and the gap that leaves is always narrower than it."""
    hi = memtop - lastb - 1
    p = hi - hi % align
    return p if p > memtop - size else None

def build_module(adapter_wat, cases, bad, image, check_base, pages,
                 utf8=(), utf16=(), xcode=(), handles=(),
                 src_tbl=0, dst_tbl=0, pads=(), atrap=(), aok=(),
                 norms=(), ntrap=(), lprobe=()):
    """The adapter code plus a `check` export that exercises every case.

    The adapter's own exports are stripped: the checks call them directly, and
    leaving them exported would let a harness that runs every export call them
    with a null pointer."""
    body = re.sub(r'\(func \(export "(load|store|move|adapt)_([0-9_]+)"\)',
                  r'(func $\1_\2', adapter_wat)

    body = re.sub(r'\(memory \(export "(mem1?)"\) \d+\)',
                  lambda m: '(memory (export "%s") %d)'
                            % (m.group(1), pages + (m.group(1) == "mem1")),
                  body, count=2)

    def seg(addr, data):

        data = data.rstrip(b"\0")
        if not data:
            return []
        return ['  (data (i32.const %d) "%s")'
                % (addr, "".join("\\%02x" % b for b in data))]

    out = seg(0, image)
    for c in cases:
        out += seg(c["alloc_at"], c["alloc"])
        out += seg(c["alloc2_at"], c["alloc2"])
        out += seg(c["alloc3_at"], c["alloc3"])
        out += seg(c["alloc3b_at"], c["alloc3b"])
    for a in atrap:
        out += seg(a["at"], a["image"])
    for x in xcode:
        out += seg(x["want_at"], x["bytes"])
    out.append('  (func (export "check")')

    nloc = max((len(c["flat"]) for c in cases), default=0)
    for k in range(nloc):
        for ty in ("i32", "i64", "f32", "f64"):
            out.append("    (local $r%d_%s %s)" % (k, ty, ty))
            out.append("    (local $m%d_%s %s)" % (k, ty, ty))
    out.append("    (local $i i32)")
    out.append("    (local $q i32)")

    for a in (src_tbl, dst_tbl):
        out += ["    i32.const %d" % a, "    i32.const 0",
                "    i32.const %d" % TABLE_BYTES, "    memory.fill",
                "    i32.const %d" % a, "    i32.const 1", "    i32.store",
                "    i32.const %d" % a, "    i32.const %d" % TABLE_CAP,
                "    i32.store offset=4"]
    out += ["    i32.const %d" % src_tbl, "    global.set $src_table",
            "    i32.const %d" % dst_tbl, "    global.set $dst_table"]
    for op, side, a, arg, rt, want in handles:
        if op == "resource_drop":
            out += ["    i32.const %d" % a, "    i32.const %d" % arg,
                    "    i32.const %d" % rt, "    call $resource_drop",
                    "    i32.const %d" % want[1], "    i32.ne",
                    "    if", "    unreachable", "    end",
                    "    i32.const %d" % want[0], "    i32.ne",
                    "    if", "    unreachable", "    end"]
            continue
        call = {"lower_own": "$lower_own", "resource_new": "$resource_new",
                "lift_own": "$lift_own", "lift_borrow": "$lift_borrow",
                "resource_rep": "$resource_rep"}[op]
        out += ["    i32.const %d" % a, "    i32.const %d" % arg,
                "    i32.const %d" % rt, "    call %s" % call,
                "    i32.const %d" % want, "    i32.ne",
                "    if", "    unreachable", "    end"]
    for u in utf8:
        out += ["    i32.const %d" % u["at"], "    i32.const %d" % u["len"],
                "    call $utf8_valid_0", "    i32.const %d" % u["want"],
                "    i32.ne", "    if", "    unreachable", "    end"]
    for u in utf16:
        out += ["    i32.const %d" % u["at"], "    i32.const %d" % u["units"],
                "    call $utf16_valid", "    i32.const %d" % u["want"],
                "    i32.ne", "    if", "    unreachable", "    end"]
    for x in xcode:
        out += ["    i32.const %d" % check_base, "    global.set $bump_0",
                "    i32.const %d" % x["at"], "    i32.const %d" % x["units"],
                "    call $xcode_%s_%s" % (x["src"], x["dst"]),

                "    i32.const %d" % (x["tagged"] - 2**32
                                      if x["tagged"] >= 2**31 else x["tagged"]),
                "    i32.ne", "    if", "    unreachable", "    end",
                "    local.set $q"]
        for j, b in enumerate(x["bytes"]):
            out += ["    local.get $q", "    i32.load8_u offset=%d" % j,
                    "    i32.const %d" % b, "    i32.ne",
                    "    if", "    unreachable", "    end"]
    for c in cases:
        out.append("    ;; %s at %d" % (c["name"], c["expected_at"]))

        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_0")
        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_1")

        for m in ("", " 1"):
            out += ["    i32.const %d" % check_base, "    i32.const 0",
                    "    i32.const %d" % (1 << 16), "    memory.fill" + m]

        out.append("    i32.const %d" % c["expected_at"])
        out.append("    call $load_%s" % c["name"])
        for k in reversed(range(len(c["flat"]))):
            out.append("    local.set $r%d_%s" % (k, c["types"][k]))
        for k, (ty, v) in enumerate(zip(c["types"], c["flat"])):
            out += ["    " + x for x in compare_to(ty, v, "$r%d_%s" % (k, ty))]

        out.append("    i32.const %d" % c["scratch"])
        for ty, v in zip(c["types"], c["flat"]):
            out += ["    " + x for x in const_for(ty, v)]
        out.append("    call $store_%s" % c["name"])
        for j, b in enumerate(c["bytes"]):
            out += ["    i32.const %d" % (c["scratch"] + j), "    i32.load8_u",
                    "    i32.const %d" % b, "    i32.ne",
                    "    if", "    unreachable", "    end"]

        def region(n, at, mem=""):
            return ["    i32.const 0", "    local.set $i",
                    "    block", "    loop",
                    "    local.get $i", "    i32.const %d" % n,
                    "    i32.ge_u", "    br_if 1",
                    "    local.get $i", "    i32.const %d" % check_base,
                    "    i32.add", "    i32.load8_u" + mem,
                    "    local.get $i", "    i32.const %d" % at,
                    "    i32.add", "    i32.load8_u",
                    "    i32.ne", "    if", "    unreachable", "    end",
                    "    local.get $i", "    i32.const 1", "    i32.add",
                    "    local.set $i", "    br 0", "    end", "    end"]
        if c["alloc"]:
            out += region(len(c["alloc"]), c["alloc_at"])

        if c["moved"] is not None:
            for ty, v in zip(c["types"], c["flat"]):
                out += ["    " + x for x in const_for(ty, v)]
            out.append("    call $move_%s" % c["name"])
            for k in reversed(range(len(c["moved"]))):
                out.append("    local.set $m%d_%s" % (k, c["types"][k]))
            for k, (ty, v) in enumerate(zip(c["types"], c["moved"])):
                out += ["    " + x for x in compare_to(ty, v, "$m%d_%s" % (k, ty))]
            if c["alloc2"]:
                out += region(len(c["alloc2"]), c["alloc2_at"])

        if c["adapt"] and not c["rspill"]:

            for ty, v in zip(c["types"], c["flat"]):
                out += ["    " + x for x in const_for(ty, v)]
            out.append("    call $adapt_%s" % c["name"])
            for k in reversed(range(len(c["flat"]))):
                out.append("    local.set $m%d_%s" % (k, c["types"][k]))
            for k, (ty, v) in enumerate(zip(c["types"], c["flat"])):
                out += ["    " + x for x in compare_to(ty, v, "$m%d_%s" % (k, ty))]
        elif c["adapt"]:

            if c["pspill"]:
                out.append("    i32.const %d" % c["expected_at"])
            else:
                for ty, v in zip(c["types"], c["flat"]):
                    out += ["    " + x for x in const_for(ty, v)]
            out.append("    i32.const %d" % c["ret"])
            out.append("    call $adapt_%s" % c["name"])
            for j, b in enumerate(c["retbytes"]):
                out += ["    i32.const %d" % (c["ret"] + j), "    i32.load8_u",
                        "    i32.const %d" % b, "    i32.ne",
                        "    if", "    unreachable", "    end"]

            if c["alloc3"]:
                out += region(len(c["alloc3"]), c["alloc3_at"])
            if c["alloc3b"]:
                out += region(len(c["alloc3b"]), c["alloc3b_at"], " 1")

    for p in pads:
        out.append("    ;; %s: an empty case, with the join full of garbage"
                   % p["name"])
        for k, ty in enumerate(p["types"]):
            if k == 0:
                out += ["    " + x for x in const_for(ty, p["case"])]
            else:
                out.append("    %s.const %s" % (ty, "-1" if ty[0] == "i" else "1.5"))
        out.append("    call $move_%s" % p["name"])
        for k in reversed(range(len(p["types"]))):
            out.append("    local.set $m%d_%s" % (k, p["types"][k]))
        out += ["    " + x for x in compare_to(p["types"][0], p["case"],
                                               "$m0_%s" % p["types"][0])]
        for k, ty in enumerate(p["types"][1:], 1):
            out += ["    " + x for x in
                    compare_to(ty, 0.0 if ty[0] == "f" else 0,
                               "$m%d_%s" % (k, ty))]

    for p in norms:
        out.append("    ;; %s: a value the core type can hold and the "
                   "component type cannot, through %s" % (p["name"], p["kind"]))
        if p["kind"] == "store":

            out.append("    i32.const %d" % p["scratch"])
            for k, (ty, v) in enumerate(zip(p["types"], p["flat"])):
                out += ["    " + x for x in
                        const_for(ty, p["src"] if k == p["slot"] else v)]
            out.append("    call $store_%s" % p["name"])
            out += ["    i32.const %d" % (p["scratch"] + p["off"]),
                    "    i32.load8_u",
                    "    i32.const %d" % (p["wantslot"] & 0xFF),
                    "    i32.ne", "    if", "    unreachable", "    end"]

            out.append("    i32.const %d" % p["scratch"])
            for ty, v in zip(p["types"], p["flat"]):
                out += ["    " + x for x in const_for(ty, v)]
            out.append("    call $store_%s" % p["name"])
            continue
        if p["kind"] == "move":
            for k, (ty, v) in enumerate(zip(p["types"], p["flat"])):
                out += ["    " + x for x in
                        const_for(ty, p["src"] if k == p["slot"] else v)]
            out.append("    call $move_%s" % p["name"])
        else:

            out += ["    i32.const %d" % (p["scratch"] + p["off"]),
                    "    i32.const %d" % p["src"], "    i32.store8",
                    "    i32.const %d" % p["scratch"],
                    "    call $load_%s" % p["name"]]
        for k in reversed(range(len(p["types"]))):
            out.append("    local.set $m%d_%s" % (k, p["types"][k]))
        if p["kind"] == "load":
            out += ["    i32.const %d" % (p["scratch"] + p["off"]),
                    "    i32.const %d" % p["orig"], "    i32.store8"]
        for k, (ty, v) in enumerate(zip(p["types"], p["want"])):
            out += ["    " + x for x in
                    compare_to(ty, p["wantslot"] if k == p["slot"] else v,
                               "$m%d_%s" % (k, ty))]

    out += ["    global.get $posts",
            "    i32.const %d" % sum(1 for c in cases if c["adapt"]),
            "    i32.ne", "    if", "    unreachable", "    end"]
    out.append("  )")

    for k, a in enumerate(atrap):
        out.append('  (func (export "atrap_%d") (result i32)' % k)
        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_0")
        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_1")
        if a["kind"] == "skew":

            out += ["    i32.const 1", "    global.set $skew"]
        if a["kind"] == "adaptutf8":

            out += ["    i32.const %d" % a["p"], "    i32.const 44992",
                    "    i32.store16",
                    "    i32.const %d" % a["p"], "    i32.const 2"]
            out.append("    i32.const %d" % a["ret"])
            out.append("    call $adapt_%s" % a["name"])
            out.append("    i32.const 1")
            out.append("  )")
            continue
        if a["pspill"]:
            out.append("    i32.const %d" % a["p"])
        else:
            for ty, v in zip(a["types"], a["flat"]):
                out += ["    " + x for x in const_for(ty, v)]
        out.append("    i32.const %d" % a["ret"])
        out.append("    call $adapt_%s" % a["name"])
        out.append("    i32.const 1")
        out.append("  )")

    for k, a in enumerate(aok):
        out.append('  (func (export "aok_%d") (result i32)' % k)
        out += ["    i32.const %d" % check_base, "    global.set $bump_0",
                "    i32.const %d" % check_base, "    global.set $bump_1",
                "    i32.const %d" % a["skew"], "    global.set $skew"]
        for ty, v in zip(a["types"], a["flat"]):
            out += ["    " + x for x in const_for(ty, v)]
        out.append("    i32.const %d" % a["ret"])
        out.append("    call $adapt_%s" % a["name"])
        out.append("    i32.const 1")
        out.append("  )")

    for k, p in enumerate(ntrap):
        out.append('  (func (export "ntrap_%d") (result i32)' % k)
        out += ["    i32.const %d" % check_base, "    global.set $bump_0",
                "    i32.const %d" % check_base, "    global.set $bump_1"]
        if p["kind"] == "list":

            out += ["    i32.const %d" % p["scratch"],
                    "    i32.const %d" % p["bad"], "    i32.store",
                    "    i32.const %d" % p["scratch"], "    i32.const 1",
                    "    call $move_%s" % p["name"], "    drop", "    drop"]
            out += ["    i32.const 1", "  )"]
            continue
        if p["kind"] == "store":
            out.append("    i32.const %d" % p["scratch"])
            for j, (ty, v) in enumerate(zip(p["types"], p["flat"])):
                out += ["    " + x for x in
                        const_for(ty, p["bad"] if j == p["slot"] else v)]
            out.append("    call $store_%s" % p["name"])
        elif p["kind"] == "move":
            for j, (ty, v) in enumerate(zip(p["types"], p["flat"])):
                out += ["    " + x for x in
                        const_for(ty, p["bad"] if j == p["slot"] else v)]
            out.append("    call $move_%s" % p["name"])
            for _ in p["types"]:
                out.append("    drop")
        else:
            out += ["    i32.const %d" % (p["scratch"] + p["off"]),
                    "    i32.const %d" % p["bad"], "    i32.store",
                    "    i32.const %d" % p["scratch"],
                    "    call $load_%s" % p["name"]]
            for _ in p["types"]:
                out.append("    drop")
        out += ["    i32.const 1", "  )"]

    for k, p in enumerate(lprobe):
        out.append('  (func (export "lprobe_%d") (result i32) '
                   '(local $p i32) (local $n i32)' % k)
        out += ["    i32.const %d" % check_base, "    global.set $bump_0",
                "    i32.const %d" % check_base, "    global.set $bump_1",
                "    i32.const %d" % p["at"], "    i32.const 7",
                "    i32.store8",
                "    i32.const %d" % (p["at"] + 1), "    i32.const 0",
                "    i32.store8",
                "    i32.const %d" % p["at"], "    i32.const 2",
                "    call $move_%s" % p["name"],
                "    local.set $n", "    local.set $p",
                "    local.get $n", "    i32.const 2", "    i32.ne",
                "    if", "    unreachable", "    end",
                "    local.get $p", "    i32.load8_u",
                "    i32.const 1", "    i32.ne",
                "    if", "    unreachable", "    end",

                "    local.get $p", "    i32.load8_u offset=1",
                "    i32.const 0", "    i32.ne",
                "    if", "    unreachable", "    end",
                "    i32.const 1", "  )"]
    for k, (what, mk) in enumerate(HANDLE_TRAPS):
        out.append('  (func (export "htrap_%d") (result i32) (local $q i32)' % k)
        for a in (src_tbl, dst_tbl):
            out += ["    i32.const %d" % a, "    i32.const 0",
                    "    i32.const %d" % TABLE_BYTES, "    memory.fill",
                    "    i32.const %d" % a, "    i32.const 1", "    i32.store",
                    "    i32.const %d" % a, "    i32.const %d" % TABLE_CAP,
                    "    i32.store offset=4"]
        out += ["    " + x for x in mk(src_tbl)]
        out.append("    i32.const 1")
        out.append("  )")
    for k, b in enumerate(bad):
        out.append('  (func (export "bad_%d") (result i32)' % k)
        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_0")
        out.append("    i32.const %d" % check_base)
        out.append("    global.set $bump_1")
        out.append("    i32.const %d" % b["at"])
        if b["kind"] == "badutf8":

            out.append("    i32.const %d" % (b["at"] + 8))
            out.append("    i32.store")
            out.append("    i32.const %d" % (b["at"] + 4))
            out.append("    i32.const 2")
            out.append("    i32.store")
            out.append("    i32.const %d" % (b["at"] + 8))
            out.append("    i32.const 44992")
            out.append("    i32.store16")
            out.append("    i32.const %d" % b["at"])
            out.append("    call $load_%s" % b["name"])
            for _ in range(b["nflat"]):
                out.append("    drop")
            out.append("    i32.const 1")
            out.append("  )")
            continue
        if b["kind"] == "misaligned":

            out.append("    i32.const %d" % (check_base + 1))
            out.append("    i32.store")
            out.append("    i32.const %d" % (b["at"] + 4))
            out.append("    i32.const 1")
            out.append("    i32.store")
            out.append("    i32.const %d" % b["at"])
            out.append("    call $load_%s" % b["name"])
            for _ in range(b["nflat"]):
                out.append("    drop")
            out.append("    i32.const 1")
            out.append("  )")
            continue
        out.append("    i32.const %d" % b["ncases"])
        out.append("    %s" % {1: "i32.store8", 2: "i32.store16",
                               4: "i32.store"}[b["disc_size"]])
        out.append("    i32.const %d" % b["at"])
        out.append("    call $load_%s" % b["name"])
        for _ in range(b["nflat"]):
            out.append("    drop")
        out.append("    i32.const 1")
        out.append("  )")
    return body.rstrip().rstrip(")") + "\n".join(out) + "\n)\n"

def check_component(name, path, d, rng):
    rc_code, canon, err = rc.run([rc.WARMC, "--dump-canon", path])
    if rc_code != 0:
        fail("%s: --dump-canon failed" % name)
        return 0, 0, 0, 0, 0
    rc_code, adapter, err = rc.run([rc.WARMC, "--dump-adapter", path])
    if rc_code != 0:
        fail("%s: --dump-adapter failed: %s" % (name, err.strip()[:160]))
        return 0, 0, 0, 0, 0

    exprs = {}
    for line in canon.splitlines():
        m = rc.LINE_T.match(line)
        if m:
            exprs[m.group(1).replace(".", "_")] = m.group(2)

    adapts = dict(ADAPT_SIG.findall(adapter))
    plan, bad, pads, at = [], [], [], 16
    skipped = adapter.count(";; type ")
    for m in LOAD_SIG.finditer(adapter):
        key, flats = m.group(1), m.group(2).split()
        expr = exprs.get(key)
        if expr is None:
            fail("%s: load_%s has no type in --dump-canon" % (name, key))
            continue
        try:
            t = rc.parse_whole(expr, d)
        except ValueError as e:
            fail("%s: %s" % (name, e))
            continue
        size = d.elem_size(t, "i32")
        align = d.alignment(t, "i32")
        dt = d.despecialize(t)

        if isinstance(dt, d.StringType):

            at = (at + 7) // 8 * 8
            bad.append({"name": key, "at": at, "kind": "badutf8",
                        "nflat": len(flats), "size": size, "scratch": at})
            at += 16
        if (isinstance(dt, d.ListType) and dt.l is None
                and d.alignment(dt.t, "i32") > 1):
            at = (at + align - 1) // align * align
            bad.append({"name": key, "at": at, "kind": "misaligned",
                        "nflat": len(flats), "size": size, "scratch": at,
                        "align": d.alignment(dt.t, "i32")})
            at += max(size, 1)
        if isinstance(dt, d.VariantType):
            ds = d.elem_size(d.discriminant_type(dt.cases), "i32")

            if len(dt.cases) < 256 ** ds:
                at = (at + align - 1) // align * align
                bad.append({"name": key, "at": at, "kind": "discriminant",
                            "disc_size": ds, "ncases": len(dt.cases),
                            "nflat": len(flats), "size": size, "scratch": at})
                at += max(size, 1)

        if isinstance(dt, d.VariantType) and len(flats) > 1:
            empty = [i for i, c in enumerate(dt.cases) if c.t is None]
            if empty:
                pads.append({"name": key, "case": empty[0], "types": flats})
        if has_handle(d, t):

            continue
        for _ in range(VALUES):
            v = gen(d, t, rng)
            if has_nan(v):
                continue
            at = (at + align - 1) // align * align
            exp = at
            at += max(size, 1)
            at = (at + align - 1) // align * align
            scr = at
            at += max(size, 1)

            at = (at + align - 1) // align * align
            ret = at
            at += max(size, 1)
            plan.append({"name": key, "t": t, "v": v, "types": flats,
                         "size": size, "align": align, "expected_at": exp,
                         "scratch": scr, "ret": ret})
    if not plan:
        return 0, skipped, 0, 0, 0

    at = (at + 15) // 16 * 16
    src_tbl, dst_tbl = at, at + TABLE_BYTES
    at = dst_tbl + TABLE_BYTES

    handles = handle_ops(d, rc, rng, 300, src_tbl, dst_tbl)

    utf8 = []
    MEM = 1 << 20
    mem = Mem(MEM)

    callee_mem = Mem(MEM)
    for raw, want in utf8_probes(rng, 160):
        at = (at + 7) // 8 * 8
        mem[at:at + len(raw)] = raw

        mem[at + len(raw):at + len(raw) + 4] = b"\xbf" * 4
        utf8.append({"at": at, "len": len(raw), "want": want})
        at += max(len(raw), 1) + 4
    utf16 = []
    for raw, units, want in utf16_probes(rng, 120):
        at = (at + 7) // 8 * 8
        mem[at:at + len(raw)] = raw
        mem[at + len(raw):at + len(raw) + 4] = b"\x00\xd8\x00\xdc"
        utf16.append({"at": at, "units": units, "want": want})
        at += max(len(raw), 1) + 4

    xcode = []
    for text in xcode_strings(rng, 40):
        for se in ("utf8", "utf16", "l1u16"):
            raw, tagged = encode_source(text, se)
            at = (at + 7) // 8 * 8
            src_at = at
            mem[at:at + len(raw)] = raw
            at += max(len(raw), 1)
            for de in ("utf8", "utf16", "l1u16"):
                dcx = RefCx(mem, 0)
                dcx.opts.string_encoding = ENCS[de]

                probe = Mem(1 << 16)
                dcx.opts.memory = probe
                dcx.bump = 8
                ptr, out_tagged = d.store_string_into_range(
                    dcx, (text, ENCS[se], tagged))
                if out_tagged & 0x80000000:
                    nbytes = 2 * (out_tagged ^ 0x80000000)
                elif de == "utf16":
                    nbytes = 2 * out_tagged
                else:
                    nbytes = out_tagged
                xcode.append({"src": se, "dst": de, "at": src_at,
                              "units": tagged, "tagged": out_tagged,
                              "bytes": bytes(probe[ptr:ptr + nbytes])})

    setup = RefCx(mem, (at + 7) // 8 * 8)
    for c in plan:
        try:
            d.store(setup, c["v"], c["t"], c["expected_at"])
        except Exception as e:
            fail("%s type %s: the reference cannot store it (%s)"
                 % (name, c["name"], e))
            return 0, skipped, 0, 0, 0
    check_base = (setup.bump + 7) // 8 * 8
    image = bytes(mem[:check_base])

    ZSPAN = 1 << 16
    cases, top = [], check_base + ZSPAN
    for c in plan:
        mem[check_base:check_base + ZSPAN] = bytes(ZSPAN)
        callee_mem[check_base:check_base + ZSPAN] = bytes(ZSPAN)
        cx = RefCx(mem, check_base)
        flat = d.lower_flat(cx, c["v"], c["t"])
        if len(flat) != len(c["types"]):
            fail("%s type %s: %d flat values, the reference gives %d"
                 % (name, c["name"], len(c["types"]), len(flat)))
            return 0, skipped, 0, 0, 0
        d.store(cx, c["v"], c["t"], c["scratch"])
        c["flat"] = flat
        c["bytes"] = bytes(mem[c["scratch"]:c["scratch"] + c["size"]])
        c["alloc"] = bytes(mem[check_base:cx.bump])
        c["listy"] = allocates(d, c["t"])
        c["stringy"] = isinstance(d.despecialize(c["t"]), d.StringType)
        c["prims"] = prim_members(d, c["t"])

        try:
            c["moved"] = d.lower_flat(
                cx, d.lift_flat(cx, d.CoreValueIter(list(flat)), c["t"]), c["t"])
        except Exception:

            c["moved"] = None
        c["alloc2"] = bytes(mem[check_base:cx.bump])

        c["adapt"] = c["name"] in adapts and c["moved"] is not None
        c["pspill"] = c["adapt"] and "(param $p i32)" in adapts[c["name"]]
        c["rspill"] = c["adapt"] and "(param $ret i32)" in adapts[c["name"]]
        c["alloc3"], c["alloc3b"] = b"", b""
        c["retbytes"], c["lastb"] = b"", -1
        if c["rspill"]:

            def to_callee():
                cx.opts.string_encoding = CALLEE_ENC
                cx.opts.memory = callee_mem
                cx.bump = callee_bump[0]

            def to_caller():
                callee_bump[0] = cx.bump
                cx.opts.string_encoding = "utf8"
                cx.opts.memory = mem
                cx.bump = caller_bump[0]

            caller_bump, callee_bump = [cx.bump], [check_base]
            try:
                if c["pspill"]:

                    v1 = d.load(cx, c["expected_at"], c["t"])
                    caller_bump[0] = cx.bump
                else:

                    v1 = d.lift_flat(cx, d.CoreValueIter(list(flat)), c["t"])
                    caller_bump[0] = cx.bump
                    to_callee()
                    f1 = d.lower_flat(cx, v1, c["t"])
                    v1 = d.lift_flat(cx, d.CoreValueIter(list(f1)), c["t"])
                    callee_bump[0] = cx.bump
                to_callee()
                ap = cx.allocate(c["align"], c["size"])
                d.store(cx, v1, c["t"], ap)

                v2 = d.load(cx, ap, c["t"])
                to_caller()
                d.store(cx, v2, c["t"], c["ret"])
                c["retbytes"] = bytes(mem[c["ret"]:c["ret"] + c["size"]])
                c["alloc3"] = bytes(mem[check_base:cx.bump])
                c["alloc3b"] = bytes(callee_mem[check_base:callee_bump[0]])
                c["lastb"] = written_span(d, c["v"], c["t"], c["size"])
            except Exception:
                c["adapt"] = c["pspill"] = c["rspill"] = False
            cx.opts.string_encoding = "utf8"
            cx.opts.memory = mem
            top = max(top, caller_bump[0], callee_bump[0])
        top = max(top, cx.bump)
        cases.append(c)

    for x in xcode:
        top = max(top, check_base + 4 * (x["units"] & 0x7FFFFFFF)
                  + 2 * len(x["bytes"]) + 64)
    aimg = (top + 7) // 8 * 8
    for c in cases:
        c["alloc_at"] = aimg
        aimg += max(len(c["alloc"]), 1)
        aimg = (aimg + 7) // 8 * 8
        c["alloc2_at"] = aimg
        aimg += max(len(c["alloc2"]), 1)
        aimg = (aimg + 7) // 8 * 8
        c["alloc3_at"] = aimg
        aimg += max(len(c["alloc3"]), 1)
        aimg = (aimg + 7) // 8 * 8
        c["alloc3b_at"] = aimg
        aimg += max(len(c["alloc3b"]), 1)
        aimg = (aimg + 7) // 8 * 8
    for b in bad:
        b["alloc_at"] = 0
    for x in xcode:
        x["want_at"] = aimg
        aimg += max(len(x["bytes"]), 1)
        aimg = (aimg + 7) // 8 * 8
    if not cases:
        return 0, skipped, 0, 0, 0

    pages = (aimg + 0xFFFF) // 0x10000 + 1
    memtop = pages * 0x10000

    atrap, kinds = [], set()

    def probe(kind, c, p, ret, image=b"", at=0):
        if kind in kinds:
            return
        kinds.add(kind)
        atrap.append({"kind": kind, "name": c["name"], "pspill": c["pspill"],
                      "types": c["types"], "flat": c["flat"], "p": p,
                      "ret": ret, "image": image, "at": at})

    for c in cases:
        if not c["rspill"]:
            continue
        if c["align"] > 1:
            if c["pspill"]:
                probe("argalign", c, c["expected_at"] + 1, c["ret"])
            probe("retalign", c, c["expected_at"], c["ret"] + 1)
            probe("skew", c, c["expected_at"], c["ret"])

        p = past_end(memtop, c["align"], c["size"], c["lastb"])
        if p is not None and p > aimg:
            if c["pspill"]:
                probe("argbounds", c, p, c["ret"],
                      c["bytes"][:memtop - p], p)
            probe("retbounds", c, c["expected_at"], p)

        if (c["stringy"] and not c["pspill"] and c["types"] == ["i32", "i32"]
                and c["scratch"] < check_base):
            probe("adaptutf8", c, c["scratch"], c["ret"])

    aok = []
    for c in cases:
        if (aok or not c["rspill"] or c["pspill"] or c["listy"]
                or c["size"] > 0x10000):
            continue
        base = (check_base + c["align"] - 1) // c["align"] * c["align"]
        aok.append({"name": c["name"], "types": c["types"], "flat": c["flat"],
                    "ret": c["ret"], "skew": memtop - base})

    norms, ntrap, lprobe = [], [], []
    for c in cases:
        which = list_prim(d, c["t"])
        if which is not None and c["moved"] is not None:
            if which == "bool":
                lprobe.append({"name": c["name"], "at": c["scratch"]})
            else:
                ntrap.append({"name": c["name"], "kind": "list",
                              "scratch": c["scratch"], "bad": 0xD800})
    for c in cases:
        for slot, off, which in c["prims"] or ():
            if which == "char":

                for kind in ("move", "load", "store"):
                    if kind == "move" and c["moved"] is None:
                        continue
                    for b in (0x110000, 0xD800):
                        ntrap.append({"name": c["name"], "kind": kind,
                                      "slot": slot, "off": off, "bad": b,
                                      "types": c["types"], "flat": c["flat"],
                                      "scratch": c["scratch"]})
                continue
            if which.startswith("flags:"):
                nlab = int(which.split(":")[1])
                src, want = (1 << 32) - 1, (1 << nlab) - 1
                kinds = ("move",)
            else:
                src, want, kinds = PRIM_PROBES[which]
            for kind in kinds:
                if kind == "move" and c["moved"] is None:
                    continue

                norms.append({
                    "name": c["name"], "kind": kind, "slot": slot, "off": off,
                    "types": c["types"], "flat": c["flat"],
                    "src": src, "wantslot": want,
                    "want": c["moved"] if kind == "move" else c["flat"],
                    "scratch": c["scratch"], "orig": c["bytes"][off],
                    "size": c["size"], "bytes": c["bytes"]})

    global moves, padprobes, adapted, spilled, atraps, normprobes
    adapted += sum(1 for c in cases if c["adapt"])
    spilled += sum(1 for c in cases if c["rspill"])
    atraps += len(atrap) + len(aok) + len(ntrap)
    normprobes += len(norms) + len(ntrap) + len(lprobe)
    moves += sum(1 for c in cases if c["moved"] is not None)
    padprobes += len(pads)
    wat = build_module(adapter, cases, bad, image, check_base, pages,
                       utf8, utf16, xcode, handles, src_tbl, dst_tbl, pads,
                       atrap, aok, norms, ntrap, lprobe)
    work = os.path.join(rc.WORK, "adapter")
    os.makedirs(work, exist_ok=True)
    src = os.path.join(work, name.replace("/", "_") + ".wat")
    with open(src, "w") as f:
        f.write(wat)
    wasm = src[:-4] + ".wasm"
    r, _, se = rc.run([rc.WASM_TOOLS, "parse", src, "-o", wasm])
    if r != 0:
        fail("%s: the generated module does not assemble: %s"
             % (name, se.strip().splitlines()[0][:200]))
        return 0, skipped, 0, 0, 0
    r, _, se = rc.run([rc.WASM_TOOLS, "validate", "--features", "all", wasm])
    if r != 0:
        fail("%s: the generated module does not validate: %s"
             % (name, se.strip().splitlines()[0][:200]))
        return 0, skipped, 0, 0, 0
    r, _, se = rc.run([WASMTIME, "run", "--invoke", "check", "-C", "cache=n", wasm])
    if r != 0:

        fail("%s: the adapter disagrees with the reference: %s"
             % (name, se.strip().splitlines()[0][:200]))
        return 0, skipped, 0, 0, 0
    for k, a in enumerate(atrap):
        r, _, _ = rc.run([WASMTIME, "run", "--invoke", "atrap_%d" % k,
                          "-C", "cache=n", wasm])
        if r == 0:
            what = {"argalign": "the argument pointer is misaligned",
                    "retalign": "the result pointer is misaligned",
                    "skew": "the callee returned a misaligned pointer",
                    "argbounds": "the arguments end past the end of memory",
                    "retbounds": "the results would end past the end of "
                                 "memory",
                    "adaptutf8": "the argument is not utf8"}[a["kind"]]
            fail("%s type %s: %s and adapt_%s returned anyway"
                 % (name, a["name"], what, a["name"]))
    for k, a in enumerate(aok):
        r, _, se = rc.run([WASMTIME, "run", "--invoke", "aok_%d" % k,
                           "-C", "cache=n", wasm])
        if r != 0:
            fail("%s type %s: the callee returned a pointer inside its own "
                 "memory and adapt_%s refused it: %s"
                 % (name, a["name"], a["name"], se.strip()[:160]))
    for k, p in enumerate(ntrap):
        r, _, _ = rc.run([WASMTIME, "run", "--invoke", "ntrap_%d" % k,
                          "-C", "cache=n", wasm])
        if r == 0:
            fail("%s type %s: U+%04X is not a scalar value and %s_%s "
                 "returned anyway"
                 % (name, p["name"], p["bad"], p["kind"], p["name"]))
    for k, p in enumerate(lprobe):
        r, _, se = rc.run([WASMTIME, "run", "--invoke", "lprobe_%d" % k,
                           "-C", "cache=n", wasm])
        if r != 0:
            fail("%s type %s: a list element that is not 0 or 1 did not come "
                 "back normalised: %s" % (name, p["name"], se.strip()[:160]))
    for k, (what, _body) in enumerate(HANDLE_TRAPS):
        r, _, _ = rc.run([WASMTIME, "run", "--invoke", "htrap_%d" % k,
                          "-C", "cache=n", wasm])
        if r == 0:
            fail("%s: %s was accepted" % (name, what))
    for k, b in enumerate(bad):
        r, _, _ = rc.run([WASMTIME, "run", "--invoke", "bad_%d" % k,
                          "-C", "cache=n", wasm])
        if r == 0:
            what = {"discriminant": "the discriminant is out of range",
                    "badutf8": "the bytes are not utf8"}.get(
                        b["kind"], "the element pointer is misaligned")
            fail("%s type %s: %s and load_%s returned anyway"
                 % (name, b["name"], what, b["name"]))
    return (len(cases), skipped, sum(1 for c in cases if c["listy"]),
            len(utf8) + len(utf16), len(xcode) + len(handles))

def main():
    if not os.path.exists(rc.WARMC):
        print("adapter: %s not built" % rc.WARMC, file=sys.stderr)
        return 1
    if not os.path.exists(rc.WASM_TOOLS):
        print("adapter: wasm-tools not built (run `make wasm-tools`)",
              file=sys.stderr)
        return 1
    if subprocess.run(["which", WASMTIME], capture_output=True).returncode != 0:
        print("adapter: skipping, no wasmtime")
        return 0
    if not rc.ensure_spec():
        return 0
    sys.path.insert(0, rc.SPEC_DIR)
    try:
        import definitions as d
    except Exception as e:
        print("adapter: skipping, the reference does not import (%s)" % e)
        return 0

    rng = random.Random(20260807)
    total = skipped = comps = listy = utf8n = xn = 0
    for name, path in rc.components(os.path.join(rc.WORK, "adapter", "components")):
        n, s, l, u, x = check_component(name, path, d, rng)
        total += n
        skipped += s
        listy += l
        utf8n += u
        xn += x
        if n:
            comps += 1
    global failures
    failures += rc.failures

    print("adapter: %d value(s) across %d component(s) round-trip exactly as "
          "the reference stores them, %d of them allocating; %d byte string(s) "
          "get the same verdict from the encoding checks as from Python; "
          "%d transcoding(s) and handle operation(s) match the reference; "
          "%d value(s) move flat to flat and %d variant(s) come back with the "
          "join zeroed; %d cross a whole lift and lower and come back "
          "unchanged, post-return counted, %d of those through memory and "
          "byte-compared, with %d bad pointer(s) refused; "
          "%d bool and char probe(s); "
          "%d type(s) not yet emitted, %d failure(s)"
          % (total, comps, listy, utf8n, xn, moves, padprobes, adapted,
             spilled, atraps, normprobes, skipped, failures))
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
