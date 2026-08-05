"""Components encoded byte by byte, each with the verdict it must draw.

The valid cases in test/components are assembled by wasm-tools, which will
only ever produce components it considers correct -- so they say nothing about
what warmc does with a *bad* one, and that is the half of a decoder's job that
matters for anything untrusted.  Every case here is written out by hand with
the exit code it owes and a substring its diagnostic must contain.

Exit codes are the interface, the same ones the core reject suite uses:
  0  accepted
  2  malformed -- the bytes do not decode
  3  invalid   -- they decode and do not check out

Naming the diagnostic matters more than it looks: several of these were
already rejected before the rule they are for existed, for an unrelated
reason, and a case that passes for the wrong reason is not a case.
"""
import os
import struct
import sys

MAGIC = b"\x00asm"
COMPONENT_VERSION = b"\x0d\x00\x01\x00"

def u32(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)

def name(s):
    b = s.encode("utf-8") if isinstance(s, str) else s
    return u32(len(b)) + b

def sec(id_, body):
    return bytes([id_]) + u32(len(body)) + body

def vec(items):
    return u32(len(items)) + b"".join(items)

def comp(*sections):
    return MAGIC + COMPONENT_VERSION + b"".join(sections)

def na(s):
    return b"\x00" + name(s)

EMPTY_MODULE = b"\x00asm\x01\x00\x00\x00"

# A core module that decodes and does not check out: one function whose body
# names a local it does not have.  Nothing outside the module refers to it, so
# the component around it is well formed and the only thing that can reject
# this is the core validator running on an embedded module.
BAD_CORE_MODULE = (
    EMPTY_MODULE
    + sec(1, vec([b"\x60\x00\x00"]))
    + sec(3, vec([u32(0)]))
    + sec(10, vec([u32(6) + u32(0) + b"\x41\x00\x21\x00\x0b"])))

FUNCTYPE_EMPTY = b"\x40" + u32(0) + b"\x01\x00"

CASES = []

def case(name_, verdict, want, data):
    CASES.append((name_, verdict, want, data))

case("truncated_preamble", 2, "too short",
     MAGIC + b"\x0d\x00")

case("core_module_still_core", 2, "malformed section id",
     b"\x00asm\x01\x00\x00\x00" + sec(14, b""))

case("bad_layer", 2, "unknown component version",
     MAGIC + b"\x0d\x00\x02\x00")

case("unknown_section_id", 2, "malformed component section id",
     comp(sec(13, b"")))
case("section_size_past_end", 2, "unexpected end of section",
     comp(b"\x07" + u32(99)))
case("section_trailing_bytes", 2, "trailing bytes",
     comp(sec(7, vec([FUNCTYPE_EMPTY]) + b"\x00\x00")))
case("vector_longer_than_section", 2, "does not fit in the section",
     comp(sec(7, u32(4096))))
case("two_start_sections", 2, "more than one start section",
     comp(sec(7, vec([FUNCTYPE_EMPTY])),
          sec(10, vec([na("f") + b"\x01" + u32(0)])),
          sec(9, u32(0) + u32(0) + u32(0)),
          sec(9, u32(0) + u32(0) + u32(0))))
case("value_section", 3, "value definitions are not supported",
     comp(sec(12, vec([b"\x7f"]))))

case("unknown_deftype", 2, "malformed type constructor",
     comp(sec(7, vec([b"\x2f"]))))
case("record_with_no_fields", 3, "at least one field",
     comp(sec(7, vec([b"\x72" + u32(0)]))))
case("variant_with_no_cases", 3, "at least one case",
     comp(sec(7, vec([b"\x71" + u32(0)]))))
case("tuple_with_no_elements", 3, "at least one element",
     comp(sec(7, vec([b"\x6f" + u32(0)]))))
case("enum_with_no_labels", 3, "at least one label",
     comp(sec(7, vec([b"\x6d" + u32(0)]))))
case("flags_with_no_labels", 3, "at least one label",
     comp(sec(7, vec([b"\x6e" + u32(0)]))))
case("flags_with_33_labels", 3, "at most 32 labels",
     comp(sec(7, vec([b"\x6e" + vec([name("f%d" % i) for i in range(33)])]))))
case("fixed_list_length_zero", 3, "non-zero length",
     comp(sec(7, vec([b"\x67" + b"\x7f" + u32(0)]))))
# The length is five bytes of input and every canonical-ABI walk loops over
# it: the flattener asks for one core value per element, and the copy emitter
# writes one load and one store per element into the adapter it generates.
# It was bounded away from zero and nowhere else.
case("fixed_list_huge", 3, "above the limit",
     comp(sec(7, vec([b"\x67" + b"\x7f" + u32(0xffffff00)]))))
case("fixed_list_at_limit_ok", 0, "",
     comp(sec(7, vec([b"\x67" + b"\x7f" + u32(65536)]))))

case("valtype_names_unknown_type", 3, "unknown type",
     comp(sec(7, vec([b"\x70" + u32(7)]))))
case("own_names_unknown_type", 3, "unknown type",
     comp(sec(7, vec([b"\x69" + u32(0)]))))
case("variant_case_missing_terminator", 2, "malformed variant case",
     comp(sec(7, vec([b"\x71" + vec([name("a") + b"\x00" + b"\x01"])]))))
case("bad_optional_valtype_tag", 2, "malformed optional value type",
     comp(sec(7, vec([b"\x6a" + b"\x07"]))))
case("bad_result_list_tag", 2, "malformed result list",
     comp(sec(7, vec([b"\x40" + u32(0) + b"\x09"]))))
case("bad_empty_result_list", 2, "malformed empty result list",
     comp(sec(7, vec([b"\x40" + u32(0) + b"\x01\x07"]))))
case("bad_type_bound", 2, "malformed type bound",
     comp(sec(10, vec([na("x") + b"\x03\x09"]))))
case("bad_extern_type", 2, "malformed extern type",
     comp(sec(10, vec([na("x") + b"\x09"]))))
case("import_module_wrong_sort", 2, "only core modules may be imported",
     comp(sec(10, vec([na("x") + b"\x00\x12" + u32(0)]))))
case("instance_type_with_import", 2, "may not contain an import",
     comp(sec(7, vec([b"\x42" + vec([b"\x03" + na("x") + b"\x01" + u32(0)])]))))
case("bad_instance_declarator", 2, "malformed instance declarator",
     comp(sec(7, vec([b"\x42" + vec([b"\x09"])]))))

case("bad_alias_form", 2, "malformed alias form",
     comp(sec(6, vec([b"\x01\x09"]))))
case("alias_unknown_instance", 3, "unknown instance",
     comp(sec(6, vec([b"\x01\x00" + u32(0) + name("f")]))))
case("alias_unknown_core_instance", 3, "unknown core instance",
     comp(sec(6, vec([b"\x00\x00\x01" + u32(0) + name("f")]))))
case("bad_sort", 2, "malformed sort",
     comp(sec(6, vec([b"\x09\x02" + u32(0) + u32(0)]))))
case("bad_core_sort", 2, "malformed core sort",
     comp(sec(6, vec([b"\x00\x09\x02" + u32(0) + u32(0)]))))

case("core_instantiate_unknown_module", 3, "unknown core module",
     comp(sec(2, vec([b"\x00" + u32(0) + u32(0)]))))
case("core_instantiate_arg_not_instance", 2, "only an instance",
     comp(sec(1, EMPTY_MODULE),
          sec(2, vec([b"\x00" + u32(0) + vec([name("e") + b"\x00" + u32(0)])]))))
case("bad_core_instance_form", 2, "malformed core instance",
     comp(sec(2, vec([b"\x09"]))))
case("bad_instance_form", 2, "malformed instance",
     comp(sec(5, vec([b"\x09"]))))
case("instantiate_unknown_component", 3, "unknown component",
     comp(sec(5, vec([b"\x00" + u32(0) + u32(0)]))))
case("inline_export_unknown_core_func", 3, "unknown core function",
     comp(sec(2, vec([b"\x01" + vec([name("f") + b"\x00" + u32(0)])]))))

case("lift_wrong_sort", 2, "canon lift must name a core function",
     comp(sec(8, vec([b"\x00\x01" + u32(0)]))))
case("lift_unknown_core_func", 3, "unknown core function",
     comp(sec(8, vec([b"\x00\x00" + u32(0) + u32(0) + u32(0)]))))
case("lower_unknown_func", 3, "unknown function",
     comp(sec(8, vec([b"\x01\x00" + u32(0) + u32(0)]))))
case("resource_new_unknown_type", 3, "unknown type",
     comp(sec(8, vec([b"\x02" + u32(0)]))))
case("canon_async_builtin_refused", 3, "not supported",
     comp(sec(8, vec([b"\x1f"]))))

MODULE_ONE_FUNC = bytes.fromhex(
    "0061736d01000000"
    "01040160000004"[:12] +
    "")
MODULE_ONE_FUNC = (b"\x00asm\x01\x00\x00\x00"
                   + sec(1, vec([b"\x60" + u32(0) + u32(0)]))
                   + sec(3, vec([u32(0)]))
                   + sec(7, vec([name("f") + b"\x00" + u32(0)]))
                   + sec(10, vec([u32(2) + u32(0) + b"\x0b"])))
CORE_FUNC_PRELUDE = (sec(1, MODULE_ONE_FUNC),
                     sec(2, vec([b"\x00" + u32(0) + u32(0)])),
                     sec(6, vec([b"\x00\x00" + b"\x01" + u32(0) + name("f")])))
case("bad_canon_option", 2, "malformed canonical option",
     comp(*CORE_FUNC_PRELUDE,
          sec(7, vec([FUNCTYPE_EMPTY])),
          sec(8, vec([b"\x00\x00" + u32(0) + vec([b"\x09"]) + u32(0)]))))

case("canon_lift_with_prelude", 0, "",
     comp(*CORE_FUNC_PRELUDE,
          sec(7, vec([FUNCTYPE_EMPTY])),
          sec(8, vec([b"\x00\x00" + u32(0) + u32(0) + u32(0)]))))

case("bad_utf8_in_name", 2, "malformed UTF-8",
     comp(sec(10, vec([b"\x00" + u32(2) + b"\xff\xfe" + b"\x01" + u32(0)]))))
case("bad_name_attribute_form", 2, "unknown name attribute form",
     comp(sec(10, vec([b"\x09" + name("x")]))))
case("bad_name_attribute", 2, "unknown name attribute",
     comp(sec(10, vec([b"\x02" + name("x") + vec([b"\x09"])]))))

case("export_unknown_func", 3, "unknown function",
     comp(sec(11, vec([na("e") + b"\x01" + u32(0) + b"\x00"]))))
case("bad_export_type_flag", 2, "malformed export type",
     comp(sec(1, EMPTY_MODULE),
          sec(11, vec([na("e") + b"\x00\x11" + u32(0) + b"\x09"]))))

case("core_module_invalid", 3, "unknown local",
     comp(sec(1, BAD_CORE_MODULE)))

# Three bytes buy one level of nesting and the decoder recursed on the C
# stack with a whole Component frame per level, so about sixty kilobytes of
# input killed the process with a signal -- which is none of the three
# verdicts warmc owes.  One byte flipped cannot build a nesting, so the
# mutation sweep cannot reach this on its own.
def nested_component_types(k):
    body = (b"\x41" + u32(1) + b"\x01") * k + b"\x41" + u32(0)
    return comp(sec(7, u32(1) + body))

case("component_type_nesting_deep", 2, "nest deeper than",
     nested_component_types(30000))
case("component_type_nesting_ok", 0, "", nested_component_types(64))

# Value types form a DAG with unrestricted sharing and the canonical ABI
# walked it structurally, so `t(i) = tuple<t(i-1), t(i-1)>` doubled the work
# for four bytes a level.  --validate-only answers instantly on this and the
# type walk behind --dump-canon did not return at all, which is what the
# termination probe below every case is for.
def doubling_tuples(k):
    ents = [b"\x7d"] + [b"\x6f" + u32(2) + u32(i - 1) + u32(i - 1)
                        for i in range(1, k + 1)]
    return comp(sec(7, vec(ents)))

case("canon_type_sharing_doubles", 0, "", doubling_tuples(40))

# An outer alias naming a sort the link plan keeps no space of its own for.
# The text format cannot say this -- wasm-tools allows outer aliases of types,
# modules and components only -- so it takes hand-written bytes.  The decoder
# advances its index space for every sort an alias can name, and the plan used
# to advance for two of them and record nothing for the rest, so what followed
# was resolved against indices that had slipped.  It must answer, and the plan
# it builds must say it cannot follow this.
case("outer_alias_core_func", 0, "",
     comp(sec(6, vec([b"\x00\x00\x02" + u32(0) + u32(0)]))))

case("empty_component", 0, "", comp())
case("one_module", 0, "", comp(sec(1, EMPTY_MODULE)))

# An `alias outer 0` inside an instance type's declarator list, naming its own
# slot.  Climbing zero scopes leaves the scope alone, so resolving the type
# asked for itself and clang turned the tail call into a jump: the process
# spun rather than overflowing, which is why the sanitizer had nothing to say.
# Both sibling branches of the same resolver already refuse an index that does
# not decrease.  --validate-only answers, so the verdict here is 0 and what
# this case is really for is the termination probe in run_components.py.
case("alias_outer_self", 0, "",
     bytes.fromhex("0061736d0d00010007080142010203020000"))
case("one_functype", 0, "", comp(sec(7, vec([FUNCTYPE_EMPTY]))))
case("custom_section_ignored", 0, "",
     comp(sec(0, name("greeting") + b"hello"), sec(7, vec([FUNCTYPE_EMPTY]))))

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "build/components"
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    for name_, verdict, want, data in CASES:
        path = os.path.join(outdir, name_ + ".wasm")
        with open(path, "wb") as f:
            f.write(data)
        manifest.append("%s\t%d\t%s" % (name_, verdict, want))
    with open(os.path.join(outdir, "manifest.tsv"), "w") as f:
        f.write("\n".join(manifest) + "\n")
    print("component rejects: wrote %d cases" % len(CASES))

if __name__ == "__main__":
    main()
