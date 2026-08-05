"""Which wasm proposal each corpus file belongs to.

Wasm 3.0 is 1.0 + 2.0 + the 3.0 additions merged into one standard, and the
upstream corpus reflects that: the `.wast` files sit in one flat directory
with no grouping.  (`proposals/` in the checkout holds only post-3.0 work --
threads, custom page sizes, wide arithmetic, custom descriptors -- which warmc
does not implement.)  So a per-proposal conformance table needs a mapping, and
this is it.

Two things about it are worth stating plainly:

- **It assigns each file to exactly one proposal, by what the file is named
  for.**  That is a judgement, and in a few places an arguable one: a corpus
  file may exercise several features at once.  `return_call_ref.wast` is a
  tail call *and* a typed reference; `type-subtyping.wast` is the type system
  the GC proposal brought in but is used by typed references too.  The table
  is a summary, not a partition of the language.

- **Every file must be classified.**  `classify_all` raises on a file that
  matches nothing, so a corpus bump cannot quietly drop new files into a
  default bucket and leave the table looking complete.
"""

RELAXED_SIMD = {
    "i8x16_relaxed_swizzle", "i16x8_relaxed_q15mulr_s", "i32x4_relaxed_trunc",
    "relaxed_dot_product", "relaxed_laneselect", "relaxed_madd_nmadd",
    "relaxed_min_max",
}

GC = {
    "array", "array_copy", "array_fill", "array_init_data", "array_init_elem",
    "array_new_data", "array_new_elem", "struct", "i31",
    "ref_test", "ref_cast", "br_on_cast", "br_on_cast_fail",
    "extern", "ref_eq", "binary-gc",

    "type-canon", "type-equivalence", "type-rec", "type-subtyping",
}

TYPED_REFS = {
    "call_ref", "return_call_ref", "ref_as_non_null",
    "br_on_null", "br_on_non_null", "local_init", "ref", "table-sub",
}

EXCEPTIONS = {"tag", "throw", "throw_ref", "try_table"}

TAIL_CALLS = {"return_call", "return_call_indirect"}

MULTI_MEMORY = {"memory-multi", "simd_memory-multi", "memory_size3"}

ADDRESS64 = {
    "address64", "align64", "binary_leb128_64", "bulk64", "call_indirect64",
    "endianness64", "float_memory64", "load64", "memory64", "memory64-imports",
    "memory_copy64", "memory_fill64", "memory_grow64", "memory_init64",
    "memory_redundancy64", "memory_trap64",
    "table64", "table_copy64", "table_fill64", "table_get64", "table_grow64",
    "table_init64", "table_set64", "table_size64",
}

ORDER = [
    ("Relaxed SIMD", "3.0", RELAXED_SIMD),
    ("Garbage collection", "3.0", GC),
    ("Typed references", "3.0", TYPED_REFS),
    ("Exception handling", "3.0", EXCEPTIONS),
    ("Tail calls", "3.0", TAIL_CALLS),
    ("Multiple memories", "3.0", MULTI_MEMORY),
    ("64-bit address space", "3.0", ADDRESS64),
    ("Vector instructions", "2.0", None),
    ("Core", "1.0 + 2.0", None),
]

VECTORS = "Vector instructions"
CORE = "Core"

def classify(name):
    """A .wast file name (with or without the extension) to a proposal."""
    stem = name[:-5] if name.endswith(".wast") else name
    for label, _ver, members in ORDER:
        if members is not None and stem in members:
            return label
    if stem.startswith("simd_"):
        return VECTORS
    return CORE

def classify_all(names):
    """{proposal: [file, ...]} over every name, in ORDER's order.

    Raises if a name in one of the explicit sets does not exist in the corpus:
    a stale entry here would silently shrink a proposal's row rather than
    announce itself.
    """
    stems = {n[:-5] if n.endswith(".wast") else n for n in names}
    for label, _ver, members in ORDER:
        if members is None:
            continue
        missing = sorted(members - stems)
        if missing:
            raise AssertionError(
                "proposals.py lists files the corpus does not have, under %r: %s"
                % (label, ", ".join(missing)))
    out = {label: [] for label, _v, _m in ORDER}
    for n in sorted(names):
        out[classify(n)].append(n)
    return out

def version_of(label):
    for name, ver, _m in ORDER:
        if name == label:
            return ver
    return "?"
