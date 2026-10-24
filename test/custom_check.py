"""The custom sections warmc puts in what it links, read back.

module_data.c's emit_custom_table writes a module's custom sections into a
section of their own -- .wasm.custom, __TEXT,__wasm_custom, .wcustom -- as
a table, one per module, each 8-aligned:

    "wcustom\\0", u32 size (the table's, this header included), u32 count,
    count x { u32 name_off, name_len, data_off, data_len } from the table,
    then the names and the payloads

and the module descriptor points at it (ModuleDesc.custom, at 240).
test/check_macho.py, check_elf.py and check_pe.py hand check() what they
found in the image; it compares that with the module's own custom
sections, the name section and DWARF's .debug_* left out as warmc leaves
them out by default.  CUSTOM_SECTIONS=all or none in the environment makes
check_elf.py and check_pe.py link with that --custom-sections and every
checker expect it.
"""
import os
import struct
import subprocess
import tempfile

DESC_CUSTOM = 240


def uleb(b, i):
    v = s = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << s
        s += 7
        if not c & 0x80:
            return v, i


def wasm_customs(b):
    """(name, payload) of each custom section of a binary module, in order"""
    out, i = [], 8
    while i < len(b):
        sid = b[i]
        size, i = uleb(b, i + 1)
        if sid == 0:
            n, j = uleb(b, i)
            out.append((b[j:j + n], b[j + n:i + size]))
        i += size
    return out


def module_customs(warmc, module):
    """the custom sections of a .wasm, or of a .wat as warmc assembles it"""
    if not module.endswith(".wat"):
        return wasm_customs(open(module, "rb").read())
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "m.wasm")
        subprocess.run([warmc, "--emit-wasm", "-o", out, module], check=True,
                       capture_output=True)
        return wasm_customs(open(out, "rb").read())


def kept(customs, mode="default"):
    if mode == "all":
        return list(customs)
    if mode == "none":
        return []
    return [(n, d) for n, d in customs if n != b"name" and not n.startswith(b".debug_")]


def tables(sec):
    """the tables a custom section holds, each a list of (name, payload)"""
    out, at = [], 0
    while at < len(sec):
        if sec[at:at + 8] != b"wcustom\0":
            raise AssertionError("no table at offset %d of the custom section" % at)
        size, n = struct.unpack_from("<II", sec, at + 8)
        if at + size > len(sec) or 16 + 16 * n > size:
            raise AssertionError("the table at %d does not fit its section" % at)
        t = []
        for k in range(n):
            no, nl, do, dl = struct.unpack_from("<IIII", sec, at + 16 + 16 * k)
            if no + nl > size or do + dl > size:
                raise AssertionError("entry %d of the table at %d points past it" % (k, at))
            t.append((sec[at + no:at + no + nl], sec[at + do:at + do + dl]))
        out.append(t)
        at = (at + size + 7) & ~7
    return out


def check(problems, sec, sec_addr, desc_ptr, customs, mode="default"):
    """sec: the custom section's bytes (None when the image has none) at
    sec_addr; desc_ptr: what the module descriptor holds for it"""
    want = kept(customs, mode)
    if not want:
        if sec is not None:
            problems.append("a custom section, though the module has none to keep")
        if desc_ptr:
            problems.append("the descriptor points at custom sections at %#x, "
                            "though the module has none to keep" % desc_ptr)
        return
    if sec is None:
        problems.append("no custom section, though the module has %d to keep" % len(want))
        return
    if desc_ptr != sec_addr:
        problems.append("the descriptor points at %#x for the custom sections, "
                        "the section is at %#x" % (desc_ptr, sec_addr))
    try:
        got = tables(sec)
    except AssertionError as e:
        problems.append(str(e))
        return
    if got != [want]:
        problems.append("the custom sections are %r, the module's %r"
                        % ([[n for n, _ in t] for t in got], [n for n, _ in want]))
