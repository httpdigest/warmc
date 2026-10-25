"""Modules that must be *rejected*, encoded here as bytes.

Every other suite in this tree feeds warmc modules that are valid and checks
what they compute.  That leaves the whole of the front end's real job
unmeasured: a decoder is judged as much by what it refuses as by what it
accepts, and the two refusals are not interchangeable -- the spec's
`assert_malformed` and `assert_invalid` are separately observable, and an
engine that answers "malformed" where the standard says "invalid" is wrong
even though it rejected the module.

The upstream corpus asserts both, and `make check-spec` runs it; this file is
for the shapes that corpus does not happen to contain, and for the ones no
tool on the host can produce.  Every case here was a defect: it is a
suppression list read backwards.

Usage: gen_reject_cases.py OUTDIR
Writes OUTDIR/<name>.wasm and prints one "name class fragment" line per case,
where class is `malformed` or `invalid` and fragment is a substring the
diagnostic has to contain.

A case whose module is a `str` rather than `bytes` is *text*: it is written as
OUTDIR/<name>.wat and exercises the .wat front end instead of the binary
decoder.  A .wat is untrusted input as much as a .wasm, and some refusals live
only on that side -- the text parser's recursion has no binary analogue.
"""
import os
import sys

def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)

def sleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        done = (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40))
        out.append(b | (0 if done else 0x80))
        if done:
            return bytes(out)

def vec(items):
    return leb(len(items)) + b"".join(items)

def section(sid, body):
    return bytes([sid]) + leb(len(body)) + body

def module(sections):
    return b"\x00asm\x01\x00\x00\x00" + b"".join(sections)

def name(s):
    b = s.encode()
    return leb(len(b)) + b

I32, I64, F32, F64, V128, FUNCREF, EXTERNREF = (
    0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x70, 0x6F)
END = b"\x0b"

T_VOID = b"\x60\x00\x00"

def fn(body, locals_=b"\x00"):
    """One code entry: a locals vector and a body, with its own length."""
    b = locals_ + body + END
    return leb(len(b)) + b

CASES = []

def case(name_, cls, fragment, mod):
    CASES.append((name_, cls, fragment, mod))

case("huge_type_count", "malformed", "vector",
     module([section(1, leb(0xFFFFFFF0))]))

VT_HEAP_MAX = (1 << 22) - 1
case("type_space_past_valtype_width", "malformed", "too many types",
     module([section(1, leb(1) + b"\x4e" + leb(VT_HEAP_MAX + 2) +
                     b"\x00" * (VT_HEAP_MAX + 2))]))
case("heaptype_past_valtype_width", "malformed", "beyond the",
     module([section(1, vec([b"\x60\x01" + bytes([0x63]) +
                             sleb(VT_HEAP_MAX + 1) + b"\x00"]))]))
# One type with a million parameters is bounded only by the size of the type
# section, and every function naming that type pays for it again: parse_code
# allocates nparams + nlocals entries whatever the local caps say, so N
# three-byte code entries cost N * 4 MB.  The limit is the one V8 and
# wasmtime use.
case("param_count_over_limit", "malformed", "above the limit",
     module([section(1, vec([b"\x60" + leb(1001) + bytes([I32]) * 1001 +
                             leb(0)]))]))
case("param_count_at_limit_ok", "accept", "",
     module([section(1, vec([b"\x60" + leb(1000) + bytes([I32]) * 1000 +
                             leb(0)]))]))
case("result_count_over_limit", "malformed", "above the limit",
     module([section(1, vec([b"\x60" + leb(0) + leb(1001) +
                             bytes([I32]) * 1001]))]))

# A shared memory needs atomics to mean anything, and there are none here.
# The bit was decoded and dropped, so the module compiled as though it had
# asked for an ordinary memory.
# The stack a constant expression is evaluated on used to be a fixed 32
# entries and is now sized from the module, with a limit of its own; nothing
# in the tree had a constant expression deeper than 32, so neither the new
# capability nor the new limit was exercised.  array.new_fixed is the only
# constant instruction that takes an unbounded number of operands.
A_MUT_I32_T = b"\x5e" + bytes([I32]) + b"\x01"
def deep_const(n):
    return module([section(1, vec([A_MUT_I32_T])),
                   section(6, vec([b"\x64" + sleb(0) + b"\x00" +
                                   b"\x41\x00" * n +
                                   b"\xfb\x08" + leb(0) + leb(n) + END]))])
case("const_stack_deeper_than_32_ok", "accept", "", deep_const(64))
case("const_stack_over_limit", "invalid", "more than the",
     deep_const(1025))

case("shared_memory", "invalid", "shared memory",
     module([section(5, vec([b"\x03" + leb(1) + leb(1)]))]))
case("shared_table", "invalid", "shared memory",
     module([section(4, vec([bytes([FUNCREF]) + b"\x03" + leb(1) + leb(1)]))]))

case("huge_param_count", "malformed", "vector",
     module([section(1, vec([b"\x60" + leb(0xFFFFFF00)]))]))
case("huge_export_count", "malformed", "vector",
     module([section(7, leb(0xFFFFFFF0))]))
case("huge_elem_items", "malformed", "vector",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([FUNCREF, 0]) + leb(1)])),
             section(9, vec([leb(0) + b"\x41\x00\x0b" + leb(0xFFFFFF00)])),
             section(10, vec([fn(b"")]))]))

case("br_table_count_wraps", "malformed", "does not fit",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00\x0e" + b"\xff\xff\xff\xff\x0f" +
                                 b"\xc1\x82\x85\x8a\x04" * 2000)]))]))
case("br_table_two_labels_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02\x40\x41\x00\x0e" + leb(2) +
                                 leb(0) + leb(0) + leb(0) + END)]))]))

_groups = b"".join(leb(100000000) + bytes([I32]) for _ in range(43))
case("locals_sum_overflow", "malformed", "too many locals",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", leb(43) + _groups)]))]))
case("locals_one_huge_group", "malformed", "too many locals",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", leb(1) + leb(0xFFFFFFFF) +
                                 bytes([I32]))]))]))

case("elem_flags_8", "malformed", "element segment flags",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([FUNCREF, 0]) + leb(1)])),
             section(9, vec([leb(8) + b"\x41\x00\x0b" + vec([leb(0)])])),
             section(10, vec([fn(b"")]))]))

def _prefixed(prefix, sub, tail=b""):
    return module([section(1, vec([T_VOID])),
                   section(3, vec([leb(0)])),
                   section(5, vec([b"\x00" + leb(1)])),
                   section(10, vec([fn(b"\x41\x00" + bytes([prefix]) + leb(sub)
                                       + tail + b"\x1a")]))])

case("prefixed_sub_aliases_next", "malformed", "illegal opcode",
     _prefixed(0xFC, 65536, b"\x00\x00"))
case("prefixed_sub_65535", "malformed", "illegal opcode",
     _prefixed(0xFC, 65535))
case("prefixed_fd_sub_overflow", "malformed", "illegal opcode",
     _prefixed(0xFD, 65536, b"\x00\x00"))

case("blocktype_out_of_range", "malformed", "malformed block type",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02" + sleb(-129) + b"\x41\x07" + END)]))]))

case("blocktype_out_of_range_ref", "malformed", "malformed block type",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02" + sleb(-0x91) + b"\xd0\x6f" + END +
                                 b"\x1a")]))]))

case("blocktype_inline_ok", "accept", "",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02" + sleb(-1) + b"\x41\x07" + END)]))]))

_bt = b"\x02" + bytes([I64]) + b"\x02" + bytes([I32]) + b"\x41\x01" + END + END
case("nested_blocktype_mismatch", "invalid", "type mismatch",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(_bt)]))]))

_bt3 = (b"\x02" + bytes([F32]) +
        b"\x02" + bytes([I64]) +
        b"\x02" + bytes([I32]) + b"\x41\x01" + END +
        b"\x1a" + b"\x42\x02" + END +
        b"\x1a" + b"\x41\x03" + END)
case("nested_blocktype_three_deep", "invalid", "type mismatch",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(_bt3)]))]))

_btif = (b"\x41\x01" +
         b"\x04" + bytes([I64]) +
         b"\x02" + bytes([I32]) + b"\x41\x01" + END +
         b"\x05" +
         b"\x42\x02" + END +
         b"\x1a" + b"\x41\x00" + END)
case("nested_blocktype_if", "invalid", "type mismatch",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(_btif)]))]))

case("nested_blocktype_ok", "accept", "",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02" + bytes([I32]) +
                                b"\x02" + bytes([I64]) + b"\x42\x01" + END +
                                b"\x1a" + b"\x41\x07" + END)]))]))
case("locals_one_group_over_cap", "malformed", "too many locals",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", leb(1) + leb(50001) +
                                 bytes([I32]))]))]))
case("locals_at_cap_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", leb(1) + leb(50000) +
                                 bytes([I32]))]))]))

_chain = [b"\x50\x00\x5f\x00"]
for _i in range(1, 65):
    _chain.append(b"\x50" + leb(1) + leb(_i - 1) + b"\x5f\x00")
case("subtype_depth_over_limit", "invalid", "subtyping depth",
     module([section(1, vec(_chain))]))
case("subtype_depth_at_limit_ok", "accept", "",
     module([section(1, vec(_chain[:-1]))]))

case("locals_many_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", leb(2) + leb(1000) + bytes([I32]) +
                                 leb(1000) + bytes([F64]))]))]))

REC, SUB, SUB_FINAL = 0x4E, 0x50, 0x4F
REF, REF_NULL = 0x64, 0x63
T_I32 = b"\x60\x00\x01" + bytes([I32])

def _sub(super_idx, ct, final=False):
    sups = [] if super_idx is None else [leb(super_idx)]
    return bytes([SUB_FINAL if final else SUB]) + vec(sups) + ct

case("sub_of_final", "invalid", "final",
     module([section(1, vec([T_VOID, _sub(0, T_VOID)]))]))

case("sub_of_sub_final", "invalid", "final",
     module([section(1, vec([_sub(None, T_VOID, final=True),
                             _sub(0, T_VOID)]))]))

case("sub_forward", "invalid", "supertype",
     module([section(1, vec([_sub(1, T_VOID), _sub(None, T_VOID)]))]))
case("sub_self", "invalid", "supertype",
     module([section(1, vec([_sub(0, T_VOID)]))]))
case("sub_two_supers", "invalid", "supertype",
     module([section(1, vec([_sub(None, T_VOID), _sub(None, T_VOID),
                             bytes([SUB]) + vec([leb(0), leb(1)]) + T_VOID]))]))

case("sub_results_differ", "invalid", "match",
     module([section(1, vec([_sub(None, T_I32), _sub(0, T_VOID)]))]))

case("ref_unknown_type", "invalid", "unknown type",
     module([section(1, vec([b"\x60\x01" + bytes([REF]) + sleb(3) + b"\x00"]))]))

case("rec_self_reference_ok", "accept", "",
     module([section(1, bytes([1]) + bytes([REC]) +
                     vec([b"\x60\x01" + bytes([REF_NULL]) + sleb(1) + b"\x00",
                          b"\x60\x01" + bytes([REF_NULL]) + sleb(0) + b"\x00"]))]))

case("ref_forward_group", "invalid", "unknown type",
     module([section(1, vec([b"\x60\x01" + bytes([REF_NULL]) + sleb(1) + b"\x00",
                             T_VOID]))]))

case("heaptype_non_minimal", "malformed", "malformed heap type",
     module([section(1, vec([b"\x60\x01" + bytes([REF_NULL]) +
                             b"\xf0\x7f" + b"\x00"]))]))

case("local_uninit", "invalid", "uninitialized local",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x20\x00\x1a",
                                 leb(1) + leb(1) + bytes([REF]) + b"\x6f")]))]))
case("local_init_in_block", "invalid", "uninitialized local",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),

             section(10, vec([fn(b"\x02\x40" + b"\xd0\x6f\xd4\x21\x00" + END +
                                 b"\x20\x00\x1a",
                                 leb(1) + leb(1) + bytes([REF]) + b"\x6f")]))]))

case("local_init_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0\x6f\xd4\x21\x00" + b"\x20\x00\x1a",
                                 leb(1) + leb(1) + bytes([REF]) + b"\x6f")]))]))

_set = b"\xd0\x6f\xd4\x21\x00"
_read = b"\x20\x00\x1a"
_reflocal = leb(1) + leb(1) + bytes([REF]) + b"\x6f"
for _n, _stop in (("return", b"\x0f"), ("unreachable", b"\x00"),
                  ("br", b"\x0c\x00")):
    case("local_init_then_" + _n, "accept", "",
         module([section(1, vec([T_VOID])),
                 section(3, vec([leb(0)])),
                 section(10, vec([fn(_set + _stop + _read, _reflocal)]))]))

case("table_non_null_no_init", "invalid", "initialiser",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([REF]) + b"\x70" + b"\x00" + leb(1)])),
             section(10, vec([fn(b"")]))]))
case("table_non_null_init_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([b"\x40\x00" + bytes([REF]) + b"\x70" + b"\x00" +
                             leb(1) + b"\xd2" + leb(0) + END])),
             section(10, vec([fn(b"")]))]))

# A function whose only mention outside a body is a table's own initialiser
# is still declared: the spec's C.refs is every function index occurring
# anywhere but a function body, and the 0x40 table form's initialiser is one
# of those places.  Without it the ref.func in the body below is refused.
case("table_init_declares_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([b"\x40\x00" + bytes([FUNCREF]) + b"\x00" +
                             leb(1) + b"\xd2" + leb(0) + END])),
             section(10, vec([fn(b"\xd2" + leb(0) + b"\x1a")]))]))

case("table_init_bad_reserved", "malformed", "0x00",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([b"\x40\x01" + bytes([REF]) + b"\x70" + b"\x00" +
                             leb(1) + b"\xd2" + leb(0) + END])),
             section(10, vec([fn(b"")]))]))

case("call_ref_untyped", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0\x70" + b"\x14" + leb(0))]))]))
case("call_ref_unknown_type", "invalid", "unknown type",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0\x70" + b"\x14" + leb(4))]))]))

T_I32_VOID = b"\x60\x01" + bytes([I32]) + b"\x00"
T_VOID_I32 = b"\x60\x00\x01" + bytes([I32])

def tagsec(entries):
    return section(13, vec(entries))

case("tag_bad_attribute", "malformed", "attribute",
     module([section(1, vec([T_VOID])),
             tagsec([b"\x01" + leb(0)])]))
case("tag_unknown_type", "invalid", "unknown type",
     module([section(1, vec([T_VOID])),
             tagsec([b"\x00" + leb(3)])]))
case("tag_with_result", "invalid", "non-empty tag result type",
     module([section(1, vec([T_VOID_I32])),
             tagsec([b"\x00" + leb(0)])]))

case("tag_section_out_of_order", "malformed", "out of order",
     module([section(1, vec([T_VOID])),
             section(6, vec([bytes([I32]) + b"\x00\x41\x00" + END])),
             tagsec([b"\x00" + leb(0)])]))
case("tag_section_ok", "accept", "",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(0), b"\x00" + leb(1)]),
             section(10, vec([fn(b"")]))]))

case("throw_unknown_tag", "invalid", "unknown tag",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x08" + leb(0))]))]))
case("throw_missing_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(b"\x08" + leb(0))]))]))

case("throw_ref_empty", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x0a")]))]))
case("throw_ref_wrong_type", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00\x0a")]))]))

def try_table(bt, catches, body):
    return b"\x1f" + bt + vec(catches) + body + END

case("try_table_bad_catch_kind", "malformed", "catch clause",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(b"\x40", [b"\x04" + leb(0)], b""))]))]))
case("try_table_unknown_tag", "invalid", "unknown tag",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)], b""))]))]))
case("try_table_unknown_label", "invalid", "unknown label",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(0)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(9)], b""))]))]))

case("try_table_catch_arity", "invalid", "the clause transfers",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)], b""))]))]))
case("try_table_catch_all_ref_arity", "invalid", "the clause transfers",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(b"\x40", [b"\x03" + leb(0)], b""))]))]))

case("try_table_label_is_outer", "accept", "",
     module([section(1, vec([T_VOID_I32, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)],
                                           b"") + b"\x00")]))]))

EXNREF = 0x69
NULL_EXN = b"\xd0" + sleb(-0x17)
NULL_FUNC = b"\xd0" + sleb(-0x10)
T_I64_VOID = b"\x60\x01" + bytes([I64]) + b"\x00"
T_I32_I32 = b"\x60\x01" + bytes([I32]) + b"\x01" + bytes([I32])
T_VOID_I32_EXN = b"\x60\x00\x02" + bytes([I32, EXNREF])
T_VOID_I32_FUNCREF = b"\x60\x00\x02" + bytes([I32, FUNCREF])

case("throw_wrong_operand_type", "invalid", "type mismatch",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(b"\x42\x00" + b"\x08" + leb(0))]))]))
case("throw_extra_operands_ok", "accept", "",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(b"\x41\x01\x41\x02" + b"\x08" + leb(0))]))]))

case("catch_ref_arity", "invalid", "the clause transfers",
     module([section(1, vec([T_VOID_I32, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)],
                                           b"") + b"\x41\x00")]))]))
case("catch_ref_ok", "accept", "",
     module([section(1, vec([T_VOID_I32_EXN, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)],
                                           b"") + b"\x41\x00" + NULL_EXN)]))]))
case("catch_all_label_arity", "invalid", "the clause transfers",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(b"\x40", [b"\x02" + leb(0)],
                                           b"") + b"\x41\x00")]))]))
case("catch_ref_last_not_exnref", "invalid", "does not accept an exnref",
     module([section(1, vec([T_VOID_I32_FUNCREF, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)], b"") +
                                 b"\x41\x00" + NULL_FUNC)]))]))
case("catch_payload_type", "invalid", "does not accept the tag's payload",
     module([section(1, vec([T_VOID_I32, T_I64_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)],
                                           b"") + b"\x41\x00")]))]))

case("try_table_unknown_blocktype", "invalid", "unknown type",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(sleb(9), [], b""))]))]))
case("try_table_blocktype_needs_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID, T_I32_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(sleb(1), [], b"") + b"\x1a")]))]))

case("try_table_missing_end", "malformed", "end opcode",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x1f\x40" + vec([]))]))]))

case("throw_ref_null_exn_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(NULL_EXN + b"\x0a")]))]))
case("throw_ref_null_func", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(NULL_FUNC + b"\x0a")]))]))
case("throw_ref_unreachable_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x00\x0a")]))]))

def imp(mod, field, kind, desc):
    return name(mod) + name(field) + bytes([kind]) + desc

case("tag_import_ok", "accept", "",
     module([section(1, vec([T_I32_VOID])),
             section(2, vec([imp("m", "t", 4, b"\x00" + leb(0))]))]))
case("tag_import_bad_attribute", "malformed", "attribute",
     module([section(1, vec([T_I32_VOID])),
             section(2, vec([imp("m", "t", 4, b"\x01" + leb(0))]))]))
case("tag_import_with_result", "invalid", "non-empty tag result type",
     module([section(1, vec([T_VOID_I32])),
             section(2, vec([imp("m", "t", 4, b"\x00" + leb(0))]))]))
case("catch_imported_tag_ok", "accept", "",
     module([section(1, vec([T_VOID_I32, T_I32_VOID])),
             section(2, vec([imp("m", "t", 4, b"\x00" + leb(1))])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)],
                                           b"") + b"\x41\x00")]))]))

case("exnref_table_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([EXNREF, 0]) + leb(1)])),
             section(10, vec([fn(b"")]))]))
case("exnref_global_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(6, vec([bytes([EXNREF, 1]) + NULL_EXN + END])),
             section(10, vec([fn(b"")]))]))
case("exnref_global_wrong_init", "invalid", "global initialiser",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(6, vec([bytes([EXNREF, 1]) + b"\xd2" + leb(0) + END])),
             section(9, vec([bytes([3, 0]) + vec([leb(0)])])),
             section(10, vec([fn(b"")]))]))
case("exnref_elem_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([EXNREF, 0]) + leb(2)])),
             section(9, vec([bytes([5, EXNREF]) + vec([NULL_EXN + END])])),
             section(10, vec([fn(b"")]))]))

case("global_init_throw", "invalid", "constant expression required",
     module([section(1, vec([T_VOID, T_I32_VOID])),
             tagsec([b"\x00" + leb(1)]),
             section(6, vec([bytes([I32, 0]) + b"\x41\x00\x08" + leb(0) + END]))]))
case("global_init_try_table", "invalid", "constant expression required",
     module([section(1, vec([T_VOID])),
             section(6, vec([bytes([I32, 0]) + b"\x1f" + bytes([I32]) + vec([]) +
                             b"\x41\x00" + END + END]))]))

for nm_, op in (("legacy_try", b"\x06\x40"),
                ("legacy_catch", b"\x06\x40\x07" + leb(0)),
                ("legacy_rethrow", b"\x09" + leb(0)),
                ("legacy_delegate", b"\x06\x40\x18" + leb(0)),
                ("legacy_catch_all", b"\x06\x40\x19")):
    case(nm_, "malformed", "illegal opcode",
         module([section(1, vec([T_VOID])),
                 section(3, vec([leb(0)])),
                 tagsec([b"\x00" + leb(0)]),
                 section(10, vec([fn(op)]))]))

case("throw_ref_externref", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([EXTERNREF]) + b"\x0a")]))]))

case("catch_ref_non_null_label", "accept", "",
     module([section(1, vec([b"\x60\x00\x02" + bytes([I32]) + b"\x64\x69",
                             T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)], b"") +
                                 b"\x00")]))]))

case("catch_ref_nullable_label_ok", "accept", "",
     module([section(1, vec([b"\x60\x00\x02" + bytes([I32, EXNREF]),
                             T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)], b"") +
                                 b"\x00")]))]))

case("catch_ref_wrong_ref_label", "invalid", "does not accept an exnref",
     module([section(1, vec([T_VOID_I32_FUNCREF, T_I32_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x01" + leb(0) + leb(0)], b"") +
                                 b"\x00")]))]))

case("catch_label_supertype_ok", "accept", "",
     module([section(1, vec([b"\x60\x00\x01" + bytes([FUNCREF]),
                             b"\x60\x01\x64\x70\x00"])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(1)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x00" + leb(0) + leb(0)], b"") +
                                 b"\xd0" + bytes([FUNCREF]))]))]))

case("try_table_second_clause_bad", "invalid", "unknown tag",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(0)]),
             section(10, vec([fn(try_table(b"\x40",
                                           [b"\x02" + leb(0),
                                            b"\x00" + leb(9) + leb(0)], b""))]))]))
case("try_table_short_catch_vec", "malformed", "catch clause",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             tagsec([b"\x00" + leb(0)]),
             section(10, vec([fn(b"\x1f\x40" + leb(2) + b"\x02" + leb(0))]))]))
case("export_unknown_tag", "invalid", "unknown tag",
     module([section(1, vec([T_VOID])),
             section(7, vec([name("t") + b"\x04" + leb(0)]))]))

I8, I16 = 0x78, 0x77
SUB, SUB_FINAL, REC = 0x50, 0x4F, 0x4E

def structty(fields):
    """fields: [(storage byte or bytes, mutable)]"""
    body = b""
    for t, mut in fields:
        body += (bytes([t]) if isinstance(t, int) else t) + (b"\x01" if mut else b"\x00")
    return b"\x5f" + leb(len(fields)) + body

def arrayty(t, mut):
    return b"\x5e" + (bytes([t]) if isinstance(t, int) else t) + \
        (b"\x01" if mut else b"\x00")

def sub(final, supers, comp):
    return bytes([SUB_FINAL if final else SUB]) + vec([leb(s) for s in supers]) + comp

case("packed_as_result", "malformed", "unknown value type",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I8])]))]))
case("packed_as_param", "malformed", "unknown value type",
     module([section(1, vec([b"\x60\x01" + bytes([I16]) + b"\x00"]))]))
case("packed_as_global", "malformed", "unknown value type",
     module([section(1, vec([T_VOID])),
             section(6, vec([bytes([I8]) + b"\x00\x41\x00" + END]))]))
case("packed_as_local", "malformed", "unknown value type",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"", locals_=leb(1) + leb(1) + bytes([I8]))]))]))

case("packed_field_ok", "accept", "",
     module([section(1, vec([structty([(I8, False), (I16, True)]),
                             arrayty(I8, True)]))]))

case("struct_extends_func", "invalid", "does not match supertype",
     module([section(1, vec([sub(False, [], T_VOID),
                             sub(True, [0], structty([(I32, False)]))]))]))
case("func_extends_struct", "invalid", "does not match supertype",
     module([section(1, vec([sub(False, [], structty([(I32, False)])),
                             sub(True, [0], T_VOID)]))]))
case("array_extends_struct", "invalid", "does not match supertype",
     module([section(1, vec([sub(False, [], structty([(I32, False)])),
                             sub(True, [0], arrayty(I32, False))]))]))

REF_NULL_FUNC = b"\x63" + bytes([FUNCREF])
REF_FUNC = b"\x64" + bytes([FUNCREF])
case("field_immutable_narrowed_ok", "accept", "",
     module([section(1, vec([sub(False, [], structty([(REF_NULL_FUNC, False)])),
                             sub(True, [0], structty([(REF_FUNC, False)]))]))]))
case("field_mutable_narrowed", "invalid", "field 0 does not match",
     module([section(1, vec([sub(False, [], structty([(REF_NULL_FUNC, True)])),
                             sub(True, [0], structty([(REF_FUNC, True)]))]))]))

case("field_mutability_dropped", "invalid", "field 0 does not match",
     module([section(1, vec([sub(False, [], structty([(I32, True)])),
                             sub(True, [0], structty([(I32, False)]))]))]))
case("field_mutability_added", "invalid", "field 0 does not match",
     module([section(1, vec([sub(False, [], structty([(I32, False)])),
                             sub(True, [0], structty([(I32, True)]))]))]))

case("struct_appends_field_ok", "accept", "",
     module([section(1, vec([sub(False, [], structty([(I32, False)])),
                             sub(True, [0], structty([(I32, False),
                                                      (I64, True)]))]))]))
case("struct_drops_field", "invalid", "does not match supertype",
     module([section(1, vec([sub(False, [], structty([(I32, False), (I64, False)])),
                             sub(True, [0], structty([(I32, False)]))]))]))

case("array_immutable_narrowed_ok", "accept", "",
     module([section(1, vec([sub(False, [], arrayty(REF_NULL_FUNC, False)),
                             sub(True, [0], arrayty(REF_FUNC, False))]))]))
case("array_mutable_narrowed", "invalid", "field 0 does not match",
     module([section(1, vec([sub(False, [], arrayty(REF_NULL_FUNC, True)),
                             sub(True, [0], arrayty(REF_FUNC, True))]))]))

case("func_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([structty([(I32, False)])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"")]))]))
case("import_func_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([structty([(I32, False)])])),
             section(2, vec([name("m") + name("f") + b"\x00" + leb(0)]))]))
case("tag_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([structty([(I32, False)])])),
             tagsec([b"\x00" + leb(0)])]))
case("block_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([T_VOID, structty([(I32, False)])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x02" + sleb(1) + b"\x0b")]))]))
case("call_indirect_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([T_VOID, structty([(I32, False)])])),
             section(3, vec([leb(0)])),
             section(4, vec([bytes([FUNCREF]) + b"\x00" + leb(0)])),
             section(10, vec([fn(b"\x41\x00\x11" + leb(1) + leb(0))]))]))
case("call_ref_names_struct_type", "invalid", "not a function type",
     module([section(1, vec([T_VOID, structty([(I32, False)])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + sleb(1) + b"\x14" + leb(1))]))]))

NULL1 = b"\xd0" + sleb(1)
S_NEW = b"\xfb\x00" + leb(1)
S_NEW_DEF = b"\xfb\x01" + leb(1)

def s_get(field, form=2):
    return bytes([0xFB, form]) + leb(1) + leb(field)

def s_set(field):
    return b"\xfb\x05" + leb(1) + leb(field)

def one_func(sig, structdef, body):
    """A module of one function over one struct type."""
    return module([section(1, vec([sig, structdef])),
                   section(3, vec([leb(0)])),
                   section(10, vec([fn(body)]))])

PACKED = structty([(I8, True)])
PLAIN = structty([(I32, True)])
IMMUT = structty([(I32, False)])

case("struct_get_packed_unsigned_form", "invalid", "needs struct.get_s or _u",
     one_func(T_VOID_I32, PACKED, NULL1 + s_get(0, 2)))
case("struct_get_plain_signed_form", "invalid", "needs a packed field",
     one_func(T_VOID_I32, PLAIN, NULL1 + s_get(0, 3)))
case("struct_get_plain_unsigned_form", "invalid", "needs a packed field",
     one_func(T_VOID_I32, PLAIN, NULL1 + s_get(0, 4)))
case("struct_get_packed_s_ok", "accept", "",
     one_func(T_VOID_I32, PACKED, NULL1 + s_get(0, 3)))
case("struct_get_packed_u_ok", "accept", "",
     one_func(T_VOID_I32, PACKED, NULL1 + s_get(0, 4)))

case("struct_get_unknown_field", "invalid", "unknown field",
     one_func(T_VOID_I32, PLAIN, NULL1 + s_get(1)))
case("struct_set_unknown_field", "invalid", "unknown field",
     one_func(T_VOID, PLAIN, NULL1 + b"\x41\x00" + s_set(1)))

case("struct_set_immutable", "invalid", "field is immutable",
     one_func(T_VOID, IMMUT, NULL1 + b"\x41\x00" + s_set(0)))

case("struct_new_names_array", "invalid", "not a struct type",
     one_func(T_VOID, arrayty(I32, True), S_NEW))
case("struct_get_names_array", "invalid", "not a struct type",
     one_func(T_VOID_I32, arrayty(I32, True), NULL1 + s_get(0)))
case("struct_new_names_func", "invalid", "not a struct type",
     module([section(1, vec([T_VOID, T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(S_NEW)]))]))

case("struct_new_default_non_nullable", "invalid", "no default value",
     one_func(T_VOID, structty([(REF_FUNC, True)]), S_NEW_DEF + b"\x1a"))
case("struct_new_default_nullable_ok", "accept", "",
     one_func(T_VOID, structty([(REF_NULL_FUNC, True)]), S_NEW_DEF + b"\x1a"))

case("struct_new_default_packed_ok", "accept", "",
     one_func(T_VOID, PACKED, S_NEW_DEF + b"\x1a"))

case("struct_new_wrong_operand", "invalid", "type mismatch",
     one_func(T_VOID, PLAIN, b"\x42\x00" + S_NEW + b"\x1a"))
case("struct_new_too_few_operands", "invalid", "type mismatch",
     one_func(T_VOID, structty([(I32, True), (I64, True)]),
              b"\x41\x00" + S_NEW + b"\x1a"))
case("struct_new_packed_takes_i32", "accept", "",
     one_func(T_VOID, PACKED, b"\x41\x00" + S_NEW + b"\x1a"))

case("struct_new_operands_swapped", "invalid", "type mismatch",
     one_func(T_VOID, structty([(I32, True), (I64, True)]),
              b"\x42\x00\x41\x00" + S_NEW + b"\x1a"))

case("struct_get_wrong_ref", "invalid", "type mismatch",
     one_func(T_VOID_I32, PLAIN, b"\xd0" + bytes([FUNCREF]) + s_get(0)))
case("struct_get_i32_operand", "invalid", "type mismatch",
     one_func(T_VOID_I32, PLAIN, b"\x41\x00" + s_get(0)))

case("ref_i31_wrong_operand", "invalid", "type mismatch",
     module([section(1, vec([b"\x60\x00\x01\x6c"])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x42\x00\xfb\x1c")]))]))
case("i31_get_wrong_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([FUNCREF]) + b"\xfb\x1d")]))]))
case("i31_get_on_anyref", "invalid", "type mismatch",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0\x6e\xfb\x1e")]))]))

case("ref_i31_is_non_null", "accept", "",
     module([section(1, vec([b"\x60\x00\x01\x64\x6c"])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00\xfb\x1c")]))]))

def gstruct(structdef, init, gtype):
    return module([section(1, vec([T_VOID, structdef])),
                   section(6, vec([gtype + b"\x00" + init + END]))])

case("const_struct_new_ok", "accept", "",
     gstruct(PLAIN, b"\x41\x00" + S_NEW, b"\x64" + sleb(1)))
case("const_struct_new_wrong_operand", "invalid", "type mismatch",
     gstruct(PLAIN, b"\x42\x00" + S_NEW, b"\x64" + sleb(1)))
case("const_struct_new_too_few", "invalid", "not enough operands",
     gstruct(structty([(I32, True), (I64, True)]), b"\x41\x00" + S_NEW,
             b"\x64" + sleb(1)))
case("const_struct_new_names_array", "invalid", "not a struct type",
     gstruct(arrayty(I32, True), S_NEW, b"\x64" + sleb(1)))
case("const_struct_new_default_non_nullable", "invalid", "no default value",
     gstruct(structty([(REF_FUNC, True)]), S_NEW_DEF, b"\x64" + sleb(1)))
case("const_ref_i31_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(6, vec([b"\x64\x6c\x00\x41\x05\xfb\x1c" + END]))]))
case("const_ref_i31_wrong_operand", "invalid", "ref.i31 needs an i32",
     module([section(1, vec([T_VOID])),
             section(6, vec([b"\x64\x6c\x00\x42\x05\xfb\x1c" + END]))]))

case("const_struct_get_not_constant", "invalid", "constant expression required",
     module([section(1, vec([T_VOID, PLAIN])),
             section(6, vec([bytes([I32]) + b"\x00\xd0" + sleb(1) +
                             s_get(0) + END]))]))

A_MUT_I32 = arrayty(I32, True)
A_IMM_I32 = arrayty(I32, False)
A_MUT_I8 = arrayty(I8, True)
A_MUT_REF = arrayty(REF_NULL_FUNC, True)

def a_op(sub, *imms):
    b = bytes([0xFB, sub])
    for x in imms:
        b += leb(x)
    return b

case("array_get_packed_unsigned_form", "invalid", "needs array.get_s or _u",
     one_func(T_VOID_I32, A_MUT_I8, NULL1 + b"\x41\x00" + a_op(11, 1)))
case("array_get_plain_signed_form", "invalid", "needs a packed element",
     one_func(T_VOID_I32, A_MUT_I32, NULL1 + b"\x41\x00" + a_op(12, 1)))
case("array_get_packed_u_ok", "accept", "",
     one_func(T_VOID_I32, A_MUT_I8, NULL1 + b"\x41\x00" + a_op(13, 1)))

case("array_set_immutable", "invalid", "array is immutable",
     one_func(T_VOID, A_IMM_I32, NULL1 + b"\x41\x00\x41\x00" + a_op(14, 1)))
case("array_fill_immutable", "invalid", "array is immutable",
     one_func(T_VOID, A_IMM_I32,
              NULL1 + b"\x41\x00\x41\x00\x41\x00" + a_op(16, 1)))
case("array_copy_immutable_dst", "invalid", "destination array is immutable",
     one_func(T_VOID, A_IMM_I32,
              NULL1 + b"\x41\x00" + NULL1 + b"\x41\x00\x41\x00" + a_op(17, 1, 1)))

case("array_get_names_struct", "invalid", "not an array type",
     one_func(T_VOID_I32, structty([(I32, True)]), NULL1 + b"\x41\x00" + a_op(11, 1)))
case("array_new_names_struct", "invalid", "not an array type",
     one_func(T_VOID, structty([(I32, True)]), b"\x41\x00\x41\x00" + a_op(6, 1)))

case("array_len_ok", "accept", "",
     one_func(T_VOID_I32, A_MUT_I32, NULL1 + a_op(15)))
case("array_len_on_struct", "invalid", "type mismatch",
     one_func(T_VOID_I32, structty([(I32, True)]), NULL1 + a_op(15)))
case("array_len_on_funcref", "invalid", "type mismatch",
     one_func(T_VOID_I32, A_MUT_I32, b"\xd0" + bytes([FUNCREF]) + a_op(15)))

case("array_new_default_non_nullable", "invalid", "no default value",
     one_func(T_VOID, arrayty(REF_FUNC, True), b"\x41\x00" + a_op(7, 1) + b"\x1a"))
case("array_new_default_nullable_ok", "accept", "",
     one_func(T_VOID, A_MUT_REF, b"\x41\x00" + a_op(7, 1) + b"\x1a"))

case("array_new_operands_swapped", "invalid", "type mismatch",
     one_func(T_VOID, arrayty(I64, True), b"\x41\x00\x42\x00" + a_op(6, 1) + b"\x1a"))
case("array_new_ok", "accept", "",
     one_func(T_VOID, arrayty(I64, True), b"\x42\x00\x41\x00" + a_op(6, 1) + b"\x1a"))
case("array_new_fixed_wrong_operand", "invalid", "type mismatch",
     one_func(T_VOID, A_MUT_I32, b"\x42\x00" + a_op(8, 1, 1) + b"\x1a"))
case("array_new_fixed_too_few", "invalid", "type mismatch",
     one_func(T_VOID, A_MUT_I32, b"\x41\x00" + a_op(8, 1, 2) + b"\x1a"))

# The element count is an immediate, and the operands it asks for are on the
# stack rather than in the bytes after it -- so nothing about the size of the
# body bounds it.  After `unreachable` the frame is polymorphic and every pop
# succeeds without consuming anything, so the whole count is walked for
# nothing: eight bytes of instruction bought seconds of compiling, and the
# builder would then have asked for one Ref and one store per element.  The
# limit is V8's.
case("array_new_fixed_count_unbounded", "invalid", "above the limit",
     one_func(T_VOID, A_MUT_I32,
              b"\x00" + a_op(8, 1) + b"\xff\xff\xff\xff\x0f" + b"\x1a"))
case("array_new_fixed_count_at_limit_ok", "accept", "",
     one_func(T_VOID, A_MUT_I32,
              b"\x00" + a_op(8, 1) + leb(10000) + b"\x1a"))

CP_TYPES = vec([T_VOID, A_MUT_I32, arrayty(I64, True)])
case("array_copy_element_mismatch", "invalid", "element types do not match",
     module([section(1, CP_TYPES),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + sleb(1) + b"\x41\x00" +
                                 b"\xd0" + sleb(2) + b"\x41\x00\x41\x00" +
                                 a_op(17, 1, 2))]))]))

DATASEC = section(11, vec([b"\x01" + name("ab")]))
DATACOUNT = section(12, leb(1))
case("array_new_data_ref_element", "invalid", "needs a numeric element",
     module([section(1, vec([T_VOID, A_MUT_REF])),
             section(3, vec([leb(0)])),
             DATACOUNT,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(9, 1, 0) + b"\x1a")])),
             DATASEC]))
case("array_new_data_ok", "accept", "",
     module([section(1, vec([T_VOID, A_MUT_I8])),
             section(3, vec([leb(0)])),
             DATACOUNT,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(9, 1, 0) + b"\x1a")])),
             DATASEC]))
case("array_new_data_unknown_segment", "invalid", "unknown data segment",
     module([section(1, vec([T_VOID, A_MUT_I8])),
             section(3, vec([leb(0)])),
             DATACOUNT,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(9, 1, 7) + b"\x1a")])),
             DATASEC]))

case("array_new_data_no_datacount", "malformed", "data count",
     module([section(1, vec([T_VOID, A_MUT_I8])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(9, 1, 0) + b"\x1a")])),
             DATASEC]))
case("array_init_data_ref_element", "invalid", "needs a numeric element",
     module([section(1, vec([T_VOID, A_MUT_REF])),
             section(3, vec([leb(0)])),
             DATACOUNT,
             section(10, vec([fn(b"\xd0" + sleb(1) +
                                 b"\x41\x00\x41\x00\x41\x00" + a_op(18, 1, 0))])),
             DATASEC]))

ELEMSEC = section(9, vec([b"\x05" + bytes([FUNCREF]) + leb(0)]))
case("array_new_elem_ok", "accept", "",
     module([section(1, vec([T_VOID, A_MUT_REF])),
             section(3, vec([leb(0)])),
             ELEMSEC,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(10, 1, 0) + b"\x1a")]))]))
case("array_new_elem_type_mismatch", "invalid", "element segment type",
     module([section(1, vec([T_VOID, arrayty(b"\x63" + bytes([EXTERNREF]), True)])),
             section(3, vec([leb(0)])),
             ELEMSEC,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(10, 1, 0) + b"\x1a")]))]))
case("array_new_elem_unknown_segment", "invalid", "unknown elem segment",
     module([section(1, vec([T_VOID, A_MUT_REF])),
             section(3, vec([leb(0)])),
             ELEMSEC,
             section(10, vec([fn(b"\x41\x00\x41\x00" + a_op(10, 1, 3) + b"\x1a")]))]))

case("const_array_new_ok", "accept", "",
     gstruct(A_MUT_I32, b"\x41\x00\x41\x02" + a_op(6, 1), b"\x64" + sleb(1)))
case("const_array_new_wrong_element", "invalid", "element expects",
     gstruct(A_MUT_I32, b"\x42\x00\x41\x02" + a_op(6, 1), b"\x64" + sleb(1)))
case("const_array_new_length_not_i32", "invalid", "length must be an i32",
     gstruct(A_MUT_I32, b"\x41\x00\x42\x02" + a_op(6, 1), b"\x64" + sleb(1)))
case("const_array_new_names_struct", "invalid", "not an array type",
     gstruct(structty([(I32, True)]), b"\x41\x00\x41\x02" + a_op(6, 1),
             b"\x64" + sleb(1)))
case("const_array_new_default_non_nullable", "invalid", "no default value",
     gstruct(arrayty(REF_FUNC, True), b"\x41\x02" + a_op(7, 1), b"\x64" + sleb(1)))
case("const_array_new_fixed_ok", "accept", "",
     gstruct(A_MUT_I32, b"\x41\x07\x41\x08" + a_op(8, 1, 2), b"\x64" + sleb(1)))
case("const_array_new_fixed_wrong_element", "invalid", "element 0 expects",
     gstruct(A_MUT_I32, b"\x42\x07\x41\x08" + a_op(8, 1, 2), b"\x64" + sleb(1)))

case("const_array_new_data_not_constant", "invalid", "constant expression required",
     module([section(1, vec([T_VOID, A_MUT_I8])),
             section(6, vec([b"\x64" + sleb(1) + b"\x00\x41\x00\x41\x00" +
                             a_op(9, 1, 0) + END])),
             DATACOUNT,
             DATASEC]))

HT_ANY, HT_EQ, HT_I31, HT_STRUCT, HT_ARRAY = 0x6E, 0x6D, 0x6C, 0x6B, 0x6A
HT_FUNC, HT_EXTERN, HT_NONE, HT_NOFUNC = 0x70, 0x6F, 0x71, 0x73
T_VOID_ANY = b"\x60\x00\x01" + bytes([HT_ANY])
T_VOID_EXTERN = b"\x60\x00\x01" + bytes([HT_EXTERN])

def r_test(ht, nul=False):
    return bytes([0xFB, 0x15 if nul else 0x14, ht])

def r_cast(ht, nul=False):
    return bytes([0xFB, 0x17 if nul else 0x16, ht])

def br_cast(fail, flags, label, h1, h2):
    return bytes([0xFB, 0x19 if fail else 0x18, flags]) + leb(label) + \
        bytes([h1, h2])

case("ref_test_across_hierarchies", "invalid", "different hierarchy",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) + r_test(HT_FUNC))]))]))
case("ref_test_extern_against_any", "invalid", "different hierarchy",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_EXTERN]) + r_test(HT_ANY))]))]))
case("ref_cast_across_hierarchies", "invalid", "different hierarchy",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) + r_cast(HT_NOFUNC) +
                                 b"\x1a")]))]))

case("ref_test_within_any_ok", "accept", "",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) + r_test(HT_I31))]))]))
case("ref_test_up_ok", "accept", "",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_I31]) + r_test(HT_ANY))]))]))
case("ref_test_on_i32", "invalid", "needs a reference",
     module([section(1, vec([T_VOID_I32])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00" + r_test(HT_ANY))]))]))

case("br_on_cast_target_not_below", "invalid", "not a subtype of its source",
     module([section(1, vec([T_VOID_ANY])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 br_cast(False, 3, 0, HT_I31, HT_STRUCT))]))]))
case("br_on_cast_siblings", "invalid", "not a subtype of its source",
     module([section(1, vec([T_VOID_ANY])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 br_cast(False, 3, 0, HT_STRUCT, HT_ARRAY))]))]))

case("br_on_cast_bad_flags", "malformed", "br_on_cast flags",
     module([section(1, vec([T_VOID_ANY])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 br_cast(False, 7, 0, HT_ANY, HT_I31))]))]))
case("br_on_cast_label_mismatch", "invalid", "does not accept the result",
     module([section(1, vec([b"\x60\x00\x01" + bytes([I32])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 br_cast(False, 3, 0, HT_ANY, HT_I31))]))]))
case("br_on_cast_label_empty", "invalid", "label takes no values",
     module([section(1, vec([T_VOID])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 br_cast(False, 3, 0, HT_ANY, HT_I31) + b"\x1a")]))]))

case("any_convert_extern_wrong_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID_ANY])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) + b"\xfb\x1a")]))]))
case("extern_convert_any_wrong_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID_EXTERN])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_EXTERN]) + b"\xfb\x1b")]))]))
case("convert_round_trip_ok", "accept", "",
     module([section(1, vec([T_VOID_ANY])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\xd0" + bytes([HT_ANY]) +
                                 b"\xfb\x1b\xfb\x1a")]))]))

case("convert_keeps_non_null", "accept", "",
     module([section(1, vec([b"\x60\x00\x01\x64" + bytes([HT_ANY])])),
             section(3, vec([leb(0)])),
             section(10, vec([fn(b"\x41\x00\xfb\x1c\xfb\x1b\xfb\x1a")]))]))

case("const_convert_ok", "accept", "",
     module([section(1, vec([T_VOID])),
             section(6, vec([b"\x63" + bytes([HT_EXTERN]) + b"\x00" +
                             b"\xd0" + bytes([HT_ANY]) + b"\xfb\x1b" + END]))]))
case("const_convert_wrong_operand", "invalid", "type mismatch",
     module([section(1, vec([T_VOID])),
             section(6, vec([b"\x63" + bytes([HT_EXTERN]) + b"\x00" +
                             b"\xd0" + bytes([HT_EXTERN]) + b"\xfb\x1b" + END]))]))

case("const_ref_cast_not_constant", "invalid", "constant expression required",
     module([section(1, vec([T_VOID])),
             section(6, vec([b"\x63" + bytes([HT_I31]) + b"\x00" +
                             b"\xd0" + bytes([HT_ANY]) + r_cast(HT_I31, True) +
                             END]))]))

# A compact import group (empty field name, kind byte 0x7f) shares its module
# name across a vector of imports.  The group marker with a field name is not
# a group and not an import, and the reader must say so rather than desync.
case("compact_import_named_group", "malformed", "non-empty field name",
     module([section(1, vec([T_VOID])),
             section(2, vec([name("m") + name("x") + b"\x7f" +
                             vec([name("f") + b"\x00" + leb(0)])]))]))

# Exact reference types are the custom-descriptors proposal, which is not
# supported; the refusal must name it instead of reading the 0x62 prefix as
# a desynced index (binaryen emits these under --all-features).
case("exact_ref_unsupported", "malformed", "custom-descriptors",
     module([section(1, vec([b"\x60\x01" + b"\x64\x62" + leb(0) +
                             leb(0)]))]))

# Text only.  A folded expression nests one C stack frame per level in the .wat
# parser, and without a cap a deeply nested source overflows the stack and
# crashes warmc rather than being rejected -- ~80k levels sufficed on an 8 MB
# stack.  The binary decoder has no matching recursion (its blocks are flat), so
# this shape cannot be built as bytes; it has to go in as text.  One level past
# the limit is enough to prove the boundary is a diagnostic and not a fault.
_DEPTH = 5001
case("text_deep_nesting", "malformed", "instruction nesting too deep",
     "(module (func " + "(block " * _DEPTH + "(nop)" + ")" * _DEPTH + "))")

# Text only.  A custom annotation is a module field: before (module ...) it is
# outside the module and refused -- while any other annotation there is white
# space and leaves the custom one inside the module where it belongs -- and in
# a binary module the bytes are the whole module, so it would go nowhere.
case("text_custom_before_module", "malformed", "misplaced @custom annotation",
     '(@custom "a" "x")\n(module (func))')
case("text_custom_after_other_annotation", "accept", "",
     '(@foo bar)\n(module (@custom "a" "x") (func))')
case("text_custom_in_binary_module", "malformed", "a custom annotation in a binary module",
     '(module binary (@custom "a" "x") "\\00asm\\01\\00\\00\\00")')
# An annotation's id and an identifier may be strings, but not empty ones.
case("text_empty_annotation_id", "malformed", "empty annotation id", '(module (@) (func))')
case("text_empty_annotation_id_spaced", "malformed", "empty annotation id",
     '(module (@ custom "a" "x") (func))')
case("text_empty_quoted_annotation_id", "malformed", "empty annotation id",
     '(module (@"") (func))')
case("text_empty_quoted_identifier", "malformed", "empty identifier", '(module (func $""))')
# A file is one module: what follows it is refused, not ignored.
case("text_more_after_module", "malformed", "more after the module",
     '(module (func))\n(func)')
case("text_more_after_binary_module", "malformed", "more after the module",
     '(module binary "\\00asm\\01\\00\\00\\00") (func)')
# A '_' in a number stands between two digits of one part: not after the 0x,
# not beside an exponent's e or p or the point, not leading a NaN's payload;
# in a \u{...} escape, between two hex digits too.
for _n, _t in [("hex_after_x", "(i32.const 0x_20)"),
               ("before_exponent", "(f64.const 1_e5)"),
               ("after_exponent", "(f64.const 1e_5)"),
               ("after_point", "(f64.const 1._5)"),
               ("before_hex_exponent", "(f64.const 0x1_p4)"),
               ("nan_payload", "(f32.const nan:0x_1)")]:
    case("text_underscore_" + _n, "malformed", "misplaced '_' in a number",
         "(module (func (drop %s)))" % _t)
case("text_unicode_escape_leading_underscore", "malformed", "expected a hex digit",
     '(module (memory 1) (data (i32.const 0) "\\u{_41}"))')
case("text_underscores_between_digits", "accept", "",
     '(module (memory 1) (data (i32.const 0) "\\u{1_F600}")'
     ' (func (drop (i32.const 0x1_0)) (drop (i64.const -1_000))'
     ' (drop (f64.const 1_0.2_5e1_0)) (drop (f64.const 0x1_f.8p1_0)) (drop (f32.const nan:0x1_0))))')

# Text only: the shapes of the text format the corpus's text half does not
# reach.  A lone surrogate here, like "\udcff", is a raw byte that is not
# UTF-8: main() writes it as the byte.
#
# Characters.  Source is UTF-8 throughout, comments and strings too; a string
# holds no character below U+20 and no U+7F but as an escape; NUL is no
# idchar, in an annotation either.
case("text_nul_in_annotation", "malformed", "unexpected byte 0x00",
     "(module (@a \x00) (func))")
case("text_nul_between_tokens", "malformed", "unexpected byte 0x00",
     "(module (func) \x00)")
for _n, _c in [("tab", "\t"), ("cr", "\r"), ("del", "\x7f"), ("unit_sep", "\x1f")]:
    case("text_raw_%s_in_string" % _n, "malformed", "control character 0x%02x in a string" % ord(_c),
         '(module (memory 1) (data (i32.const 0) "a%sb"))' % _c)
case("text_raw_tab_in_quoted_id", "malformed", "control character 0x09 in a string",
     '(module (func $"a\tb"))')
case("text_control_escapes_in_string", "accept", "",
     '(module (memory 1) (data (i32.const 0) "\\t\\n\\r\\00\\7f\\u{1f}"))')
case("text_bad_utf8_in_line_comment", "malformed", "malformed UTF-8 encoding",
     "(module (func)) ;; \udcff\n")
case("text_bad_utf8_in_block_comment", "malformed", "malformed UTF-8 encoding",
     "(module (; \udcc3 ;) (func))")
case("text_bad_utf8_raw_in_string", "malformed", "malformed UTF-8 encoding",
     '(module (memory 1) (data (i32.const 0) "\udce2\udc82"))')
case("text_overlong_utf8_in_string", "malformed", "malformed UTF-8 encoding",
     '(module (memory 1) (data (i32.const 0) "\udcc0\udcaf"))')
case("text_utf8_in_comments_and_strings", "accept", "",
     '(module ;; Grüße\n (; ★ ;) (memory 1) (data (i32.const 0) "Heiße ★ 😀"))')
case("text_bad_utf8_escape_in_quoted_id", "malformed",
     "malformed UTF-8 encoding in an identifier", '(module (func $"\\ef\\bb"))')
case("text_utf8_escapes_in_quoted_id", "accept", "",
     '(module (func $"\\ef\\bb\\bf\\u{1F600}") (export "f" (func $"\\u{feff}😀")))')
case("text_bad_utf8_escape_in_annotation_id", "malformed",
     "malformed UTF-8 encoding in an annotation id", '(module (@"\\c3") (func))')
# A line comment ends at a newline, which is U+0A, U+0D or the two: past a
# lone CR the next line is code.  Had the comment run on, the module would
# lack its closing parentheses.
case("text_line_comment_ends_at_cr", "accept", "",
     "(module (func ;; one\r(drop (i32.const 1))) ;; two\r\n(func))")
# An error after lone CRs is placed on the line they make.
case("text_cr_counts_lines", "malformed", ":3:18: malformed float",
     "(module\r(func\r(drop (f32.const .5))))")
# Tokens.  An annotation opens with "(@" and no space between; idchars and
# strings with nothing between them are one token, which means something only
# as idchars alone, one string alone, or $ before one string -- anything else
# is reserved, and so are , ; [ ] { } -- and only an annotation may hold one.
case("text_spaced_annotation_is_no_annotation", "malformed", "unknown module field '@a'",
     "(module ( @a) (func))")
case("text_annotation_holds_reserved_tokens", "accept", "",
     '(module (@a , ; ] [ }} }x{ x"y" "a""b" $"l"0 ,{{};}] ;) (func))')
case("text_reserved_comma", "malformed", "unexpected token ','", "(module (func) ,)")
case("text_reserved_brace", "malformed", "unexpected token '{'", "(module (func {}))")
case("text_keyword_then_string", "malformed", "unexpected token 'export\"f\"'",
     '(module (func) (export"f" (func 0)))')
case("text_two_strings_unseparated", "malformed", "unexpected token '\"a\"\"b\"'",
     '(module (memory 1) (data (i32.const 0) "a""b"))')
case("text_quoted_id_then_idchars", "malformed", "unexpected token '$\"f\"g'",
     '(module (func $"f"g))')
case("text_id_then_string", "malformed", "unexpected token '$f\"g\"'",
     '(module (func $f"g"))')
case("text_strings_separated", "accept", "",
     '(module (memory 1) (data (i32.const 0) "a" "b"(;;)"c"))')
# Numbers.  A float is digits, then maybe a point and more digits, then maybe
# an exponent with digits of its own, or the same in hex after a lowercase
# 0x; a NaN's payload is hex; a value that rounds to infinity is out of range
# where one that rounds to zero is not.
for _n, _t in [("leading_point", "f32.const .5"), ("leading_point_exp", "f64.const .5e1"),
               ("empty_exponent", "f64.const 1e"), ("signed_empty_exponent", "f64.const 1e+"),
               ("hex_empty_exponent", "f64.const 0x1p"), ("hex_leading_point", "f64.const 0x.8p1"),
               ("infinity_spelled", "f32.const infinity"), ("inf_upper", "f64.const INF"),
               ("upper_hex_prefix", "f64.const 0X1p3"), ("exponent_in_hex_digits", "f64.const 0x1e3p")]:
    case("text_float_" + _n, "malformed", "malformed float", "(module (func (drop (%s))))" % _t)
case("text_int_upper_hex_prefix", "malformed", "malformed number '0X10'",
     "(module (func (drop (i32.const 0X10))))")
for _n, _t in [("decimal", "nan:1"), ("upper_x", "nan:0X1")]:
    case("text_nan_payload_" + _n, "malformed", "a NaN payload is written in hex",
         "(module (func (drop (f32.const %s))))" % _t)
for _n, _t in [("f32_hex", "f32.const 0x1p128"), ("f32_decimal", "f32.const 3.5e38"),
               ("f32_halfway", "f32.const 0x1.ffffffp127"), ("f64_decimal", "f64.const -1e309"),
               ("f64_hex", "f64.const 0x1p1024"), ("v128_lane", "v128.const f32x4 0 0 0 1e39")]:
    case("text_float_out_of_range_" + _n, "malformed", "is out of range",
         "(module (func (drop (%s))))" % _t)
case("text_floats_at_the_edges", "accept", "",
     "(module (func"
     " (drop (f32.const 1.)) (drop (f64.const 1.e5)) (drop (f64.const 0x1.)) (drop (f64.const 0x1P3))"
     " (drop (f64.const 1E3)) (drop (f32.const 0x1.fffffep127)) (drop (f32.const -0x1.fffffefffffffp127))"
     " (drop (f32.const 3.4028234e38)) (drop (f64.const 0x1.fffffffffffffp1023))"
     " (drop (f64.const 1.7976931348623157e308)) (drop (f32.const 1e-50)) (drop (f64.const 1e-400))"
     " (drop (f32.const -inf)) (drop (f64.const +nan)) (drop (f64.const nan:0xf_ffff_ffff_ffff))))")
# Type uses.  A function's, a type definition's, an import's and a tag's
# parameters may be named; a block type's and call_indirect's not.  A type
# use that has both an index and a signature must name a type, or there is
# nothing to check the signature against.
for _n, _b in [("block", "(block (param $x i32) (drop))"),
               ("loop", "(loop (param $x i32) (drop))"),
               ("if", "(if (param $x i32) (i32.const 1) (then (drop)) (else (drop)))"),
               ("try_table", "(try_table (param $x i32) (drop))"),
               ("flat_block", "block (param $x i32) drop end"),
               ("block_with_type", "(block (type $t) (param $x i32) (drop))"),
               ("call_indirect", "(call_indirect (param $x i32) (i32.const 0))"),
               ("return_call_indirect", "(return_call_indirect (param $x i32) (i32.const 0))")]:
    case("text_named_param_in_" + _n, "malformed", "a parameter here cannot be named",
         "(module (type $t (func (param i32))) (table 1 funcref)"
         " (func (i32.const 0) %s))" % _b)
case("text_named_params_where_allowed", "accept", "",
     '(module (type (func (param $a i32))) (import "m" "f" (func (param $b i32)))'
     ' (import "m" "t" (tag (param $c i32))) (tag (param $d i32)) (func (param $e i32)))')
case("text_typeuse_signature_unknown_type", "malformed", "unknown type 5",
     "(module (func (type 5) (param i32)))")
case("text_typeuse_signature_unknown_type_in_block", "malformed", "unknown type 7",
     "(module (func (block (type 7) (result i32) (i32.const 0)) (drop)))")
case("text_typeuse_unknown_type_alone", "invalid", "unknown type", "(module (func (type 5)))")
# Imports come before every definition of a function, table, memory, global
# or tag, whatever the kinds of the two -- an inline import included.  Types,
# segments, exports and the start function are no definitions here.
for _n, _d, _i in [("tag", "(tag)", '(import "m" "f" (func))'),
                   ("func_then_tag", "(func)", '(import "m" "t" (tag))'),
                   ("memory_inline_func", "(memory 1)", '(func (import "m" "f"))'),
                   ("table_inline_global", "(table 1 funcref)", '(global (import "m" "g") i32)'),
                   ("global_inline_tag", "(global i32 (i32.const 0))", '(tag (import "m" "t"))'),
                   ("tag_inline_memory", "(tag)", '(memory (import "m" "mem") 1)')]:
    case("text_import_after_" + _n, "malformed", "import after a", "(module %s %s)" % (_d, _i))
case("text_import_after_non_definitions", "accept", "",
     '(module (type (func)) (elem declare func) (data "") (export "f" (func 0))'
     ' (start 0) (import "m" "f" (func)) (func (import "m" "g")))')
case("text_two_start_functions", "malformed", "one start function at most",
     "(module (func) (start 0) (func) (start 1))")
# An inline signature stands for the first type that is a function type, in
# no rec group of its own -- a group of one would do as well, and is
# equivalent -- final and with no supertype; or for a new type at the end.
# So $f's type below is never $t, and only a subtype of $t could be.
for _n, _t in [("nonfinal", "(type $t (sub (func)))"),
               ("in_rec_group", "(rec (type $t (func)) (type (struct)))")]:
    case("text_implicit_type_is_not_" + _n, "invalid", "",
         "(module %s (func $f) (global (ref $t) (ref.func $f)))" % _t)
# $t is the first type with $f's signature, but it has a supertype, whose
# signature is another
case("text_implicit_type_is_not_with_supertype", "invalid", "",
     "(module (type $s (sub (func (result anyref)))) (type $t (sub final $s (func (result eqref))))"
     " (func $f (result eqref) (ref.null eq)) (global (ref $t) (ref.func $f)))")
for _n, _t in [("plain", "(type $t (func))"), ("sub_final", "(type $t (sub final (func)))"),
               ("rec_of_one", "(rec (type $t (func)))")]:
    case("text_implicit_type_is_" + _n, "accept", "",
         "(module %s (func $f) (global (ref $t) (ref.func $f)))" % _t)
# Inline segments.  A table's inline (elem ...) takes an address type before
# its element type, an offset of that address type, and the table's element
# type for its items, function indices included; an item is (item ...) or a
# folded instruction; a memory's inline (data ...) an offset of its address
# type.  Every one of these was refused, as malformed or as invalid.
case("text_inline_elem_table64", "accept", "",
     "(module (table i64 funcref (elem $f)) (func $f))")
case("text_inline_elem_table64_exprs", "accept", "",
     "(module (table i64 funcref (elem (ref.func $f))) (func $f))")
case("text_inline_elem_typed_indices", "accept", "",
     "(module (type $t (func)) (table (ref null $t) (elem $f $f)) (func $f (type $t)))")
# A (ref func) table has no default for its elements, so the abbreviation,
# which gives the table none, is invalid -- after it is read.
case("text_inline_elem_nonnull_indices", "invalid", "has no default value",
     "(module (table (ref func) (elem $f)) (func $f))")
case("text_inline_elem_folded_items", "accept", "",
     "(module (type $a (array i8)) (table (ref null $a) (elem"
     " (array.new $a (i32.const 7) (i32.const 3)) (item (array.new_fixed $a 1 (i32.const 1)))"
     " (item i32.const 2 i32.const 4 array.new $a))))")
case("text_elem_folded_items", "accept", "",
     "(module (type $a (array i8)) (elem $e (ref $a)"
     " (array.new $a (i32.const 7) (i32.const 3)) (array.new_default $a (i32.const 1)))"
     " (elem i31ref (ref.i31 (i32.const 5))))")
case("text_inline_data_memory64", "accept", "",
     '(module (memory i64 (data "x")) (memory (data "y")))')

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name_, cls, frag, mod in CASES:
        text = isinstance(mod, str)
        path = os.path.join(out, name_ + (".wat" if text else ".wasm"))
        tmp = "%s.%d.tmp" % (path, os.getpid())
        # UTF-8 whatever the locale, a lone surrogate as the byte it stands
        # for, and no newline translated
        with (open(tmp, "w", encoding="utf-8", errors="surrogateescape", newline="")
              if text else open(tmp, "wb")) as f:
            f.write(mod)
        os.replace(tmp, path)
        print("%s %s %s" % (name_, cls, frag))

main()
