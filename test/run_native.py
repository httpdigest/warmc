"""--native-module: wasm imports compiled to calls of C functions.

  * test/native/natives.wat calls the C functions of test/native/natives.c at
    every level: integer and floating-point arguments in registers and, past
    eight of a kind, on the stack (packed on Apple's convention, 8 bytes each
    on AAPCS64's), every result type, values kept across calls that write
    every caller-saved register, a tail call, C calling back into the module,
    and C reading and writing linear memory at the native address
    warmc_memory_base gives;
  * a C function's arguments are hinted into C's registers, so that none is
    computed into wasm's and moved;
  * what warmc refuses: a native import used as anything but a call, or of a
    type C cannot be called with, warmc_memory_base of the wrong type or
    without a memory, and native imports where they cannot be linked.

It runs on the host's own target, macOS or Linux on AArch64, with clang.

Usage: python3 test/run_native.py
"""

import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
NATIVE = os.path.join(ROOT, "test", "native")

fails = []


def fail(msg):
    fails.append(msg)
    print("FAIL[native] " + msg)


def host():
    """(target, runtime, clang flags) of the host, or None."""
    if platform.machine() not in ("arm64", "aarch64"):
        return None
    if sys.platform == "darwin":
        return "macos", os.path.join(ROOT, BUILD, "warmrt-macos.a"), ["-mmacosx-version-min=11.0"]
    if sys.platform.startswith("linux"):
        # linked -static with no libc, as warmc's Linux programs are
        return "linux", os.path.join(ROOT, BUILD, "warmrt.a"), ["-ffreestanding", "-fno-stack-protector"]
    return None


def warmc(args):
    return subprocess.run([WARMC] + args, capture_output=True, text=True)


# Each a module and what warmc says when it refuses it.
REFUSE = {
    "a v128 parameter": ('(module (import "c" "f" (func (param v128))))', "called with i32, i64, f32"),
    "a reference parameter": ('(module (import "c" "f" (func (param externref))))', "called with i32, i64, f32"),
    "two results": ('(module (import "c" "f" (func (result i32 i32))))', "returns at most one"),
    "an export of one": ('(module (import "c" "f" (func)) (export "g" (func 0)))', "can only be called"),
    "ref.func of one": ('(module (import "c" "f" (func)) (elem declare func 0))', "can only be called"),
    "a global holding one": ('(module (import "c" "f" (func)) (global funcref (ref.func 0)))',
                             "can only be called"),
    "a table holding one": ('(module (import "c" "f" (func)) (table 1 funcref) (elem (i32.const 0) func 0))',
                            "can only be called"),
    "a start function": ('(module (import "c" "f" (func)) (start 0))', "can only be called"),
    "warmc_memory_base returning i32": ('(module (import "c" "warmc_memory_base" (func (result i32))) (memory 1))',
                                        "memory 0's base"),
    "warmc_memory_base taking an argument":
        ('(module (import "c" "warmc_memory_base" (func (param i32) (result i64))) (memory 1))', "memory 0's base"),
    "warmc_memory_base without a memory": ('(module (import "c" "warmc_memory_base" (func (result i64))))',
                                           "memory 0's base"),
    "an empty name": ('(module (import "c" "" (func)))', "not empty"),
}


def run_refusals(work, target, rt):
    for what, (wat, says) in REFUSE.items():
        src = os.path.join(work, "refuse.wat")
        with open(src, "w") as f:
            f.write(wat)
        r = warmc(["--target", target, "--rt", rt, "--native-module", "c", "-c", "-o",
                   os.path.join(work, "refuse.o"), src])
        if r.returncode == 0 or says not in r.stderr:
            fail("%s: warmc exit %d, said %r, wanted %r" % (what, r.returncode, r.stderr.strip()[:200], says))
    # where they cannot be linked
    src = os.path.join(work, "one.wat")
    with open(src, "w") as f:
        f.write('(module (import "c" "f" (func)) (func (export "g") (call 0)))')
    r = warmc(["--target", "windows", "--native-module", "c", "-c", "-o", os.path.join(work, "one.obj"), src])
    if r.returncode == 0 or "AArch64's alone" not in r.stderr:
        fail("windows: warmc exit %d, said %r" % (r.returncode, r.stderr.strip()[:200]))
    r = warmc(["--target", target, "--rt", rt, "--native-module", "c", "--shared", "-o",
               os.path.join(work, "libone.so"), src])
    if r.returncode == 0 or "bound by the program that loads it" not in r.stderr:
        fail("--shared: warmc exit %d, said %r" % (r.returncode, r.stderr.strip()[:200]))
    plain = os.path.join(work, "plain.wat")
    with open(plain, "w") as f:
        f.write('(module (func (export "g")))')
    r = warmc(["--target", target, "--rt", rt, "--link", "x.o", "-o", os.path.join(work, "plain"), plain])
    if r.returncode == 0 or "--link" not in r.stderr:
        fail("--link without the system linker: warmc exit %d, said %r" % (r.returncode, r.stderr.strip()[:200]))
    # an object alone: the C function an undefined symbol of it, the
    # intrinsic none
    obj = os.path.join(work, "one.o")
    with open(src, "w") as f:
        f.write('(module (import "c" "native_f" (func)) (import "c" "warmc_memory_base" (func (result i64)))'
                ' (memory 1) (func (export "g") (result i64) (call 0) (call 1)))')
    r = warmc(["--target", target, "--rt", rt, "--native-module", "c", "-c", "-o", obj, src])
    if r.returncode != 0:
        fail("-c: warmc: %s" % r.stderr.strip()[:300])
        return
    syms = subprocess.run(["nm", obj], capture_output=True, text=True).stdout
    if not re.search(r"\bU _?native_f\b", syms):
        fail("-c: no undefined native_f in the object:\n%s" % syms[:400])
    if "warmc_memory_base" in syms:
        fail("-c: warmc_memory_base is a symbol of the object, not memory 0's base")


def run_calls(work, target, rt, cflags):
    obj = os.path.join(work, "natives.o")
    r = subprocess.run(["clang", "-O2", "-c"] + cflags + ["-I", os.path.join(ROOT, "rt"), "-o", obj,
                        os.path.join(NATIVE, "natives.c")], capture_output=True, text=True)
    if r.returncode != 0:
        fail("clang natives.c: %s" % r.stderr[:400])
        return 0
    runs = 0
    for level in ("-O0", "-O1", "-O2", "-O3"):
        exe = os.path.join(work, "natives" + level)
        r = warmc(["--target", target, "--rt", rt, "--wasi", level, "--native-module", "c", "--link", obj,
                   "-o", exe, os.path.join(NATIVE, "natives.wat")])
        if r.returncode != 0:
            fail("natives.wat %s: warmc: %s" % (level, r.stderr.strip()[:400]))
            continue
        got = subprocess.run([exe], capture_output=True, text=True)
        runs += 1
        if got.returncode != 0 or got.stdout != "native: ok\n":
            fail("natives.wat %s: exit %d (the check that failed), stdout %r"
                 % (level, got.returncode, got.stdout[:200]))
    return runs


SHAPE = '''(module
  (import "c" "nat_i32" (func $nat_i32 (param i32 i32) (result i32)))
  (memory 1)
  ;; both arguments loaded, the call's to make: each loaded into C's register
  (func $shape (export "shape") (result i32)
    (call $nat_i32 (i32.load (i32.const 16)) (i32.load (i32.const 32)))))'''


def run_shape(work, target, rt):
    src = os.path.join(work, "shape.wat")
    with open(src, "w") as f:
        f.write(SHAPE)
    obj = os.path.join(work, "shape.o")
    r = warmc(["--target", target, "--rt", rt, "-O2", "--native-module", "c", "-c", "-o", obj, src])
    if r.returncode != 0:
        fail("shape: warmc: %s" % r.stderr.strip()[:300])
        return
    objdump = shutil.which("llvm-objdump") or shutil.which("objdump")
    if not objdump:
        print("native: shape left out (no objdump)")
        return
    d = subprocess.run([objdump, "-d", "--no-show-raw-insn", obj], capture_output=True, text=True).stdout
    body = d[d.find("shape"):]
    moves = [l for l in body.split("\n") if re.search(r"\bmov\s+[wx]0, [wx][0-9]", l)]
    if moves:
        fail("shape: an argument computed elsewhere and moved into C's register:\n%s" % "\n".join(moves))


def main():
    h = host()
    if not h:
        print("native: left out (warmc calls C from AArch64 code alone, on macOS and Linux)")
        return 0
    target, rt, cflags = h
    if not os.path.exists(WARMC) or not os.path.exists(rt):
        print("native: %s or %s not built" % (WARMC, rt), file=sys.stderr)
        return 1
    if not shutil.which("clang"):
        print("native: left out (needs clang)")
        return 0
    work = tempfile.mkdtemp(prefix="native.", dir=os.path.join(ROOT, BUILD))
    try:
        runs = run_calls(work, target, rt, cflags)
        run_shape(work, target, rt)
        run_refusals(work, target, rt)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("native: %d failure(s)" % len(fails))
        return 1
    print("native: C called %d times through natives.wat's checks at -O0 to -O3, its arguments in C's "
          "registers, %d modules refused as they should be" % (runs, len(REFUSE) + 3))
    return 0


if __name__ == "__main__":
    sys.exit(main())
