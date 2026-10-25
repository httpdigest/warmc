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


def wasm_nfuncs(b):
    """the functions of a binary module, the imported ones included"""
    n, i = 0, 8
    while i < len(b):
        sid = b[i]
        size, i = uleb(b, i + 1)
        end = i + size
        if sid == 3:
            n += uleb(b, i)[0]
        elif sid == 2:
            count, j = uleb(b, i)
            for _ in range(count):
                for _ in range(2):
                    ln, j = uleb(b, j)
                    j += ln
                kind = b[j]
                j += 1
                if kind == 0:
                    n += 1
                    j = uleb(b, j)[1]
                elif kind == 1:
                    j = skip_limits(b, skip_reftype(b, j))
                elif kind == 2:
                    j = skip_limits(b, j)
                elif kind == 3:
                    j = skip_reftype(b, j) + 1
                elif kind == 4:
                    j = uleb(b, j + 1)[1]
                else:
                    return None
        i = end
    return n


def skip_reftype(b, j):
    """past a value type"""
    if b[j] in (0x63, 0x64):
        return uleb(b, j + 1)[1]
    return j + 1


def skip_limits(b, j):
    flags = b[j]
    j = uleb(b, j + 1)[1]
    return uleb(b, j)[1] if flags & 1 else j


def func_names(customs, nfuncs=None):
    """{function index: name} from the name section, read as src/parse.c's
    read_func_names reads it: nothing at all from a section that is not as
    the appendix has it, and no name that is empty or holds a NUL"""
    sec = next((d for n, d in customs if n == b"name"), None)
    if sec is None:
        return {}
    out, i, last = {}, 0, -1
    try:
        while i < len(sec):
            sid = sec[i]
            size, i = uleb(sec, i + 1)
            if sid <= last or i + size > len(sec):
                return {}
            last, sub, i = sid, sec[i:i + size], i + size
            if sid != 1:
                continue
            n, j = uleb(sub, 0)
            prev = -1
            for _ in range(n):
                idx, j = uleb(sub, j)
                ln, j = uleb(sub, j)
                if idx <= prev or j + ln > len(sub) or (nfuncs is not None and idx >= nfuncs):
                    return {}
                prev = idx
                name = sub[j:j + ln].decode("utf-8")
                j += ln
                if name and "\0" not in name:
                    out[idx] = name
            if j != len(sub):
                return {}
    except (IndexError, UnicodeDecodeError):
        return {}
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
