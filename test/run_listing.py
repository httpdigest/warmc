"""WARMC_LISTING's offsets (src/isel.c): each instruction's place in its
function's code -- the check ahead of an exported function's symbol and the
prologue counted in -- beside the value it made.

  out    exported, so with the check an entry from outside makes ahead of
         its symbol: x * y + 7
  deep   called, with a frame: x * y where d is 0, itself with d - 1
         otherwise -- placed as a start and its full body, each with a
         listing of its own, the whole function's first

For each, the listing's `mul` is at an offset where the object's
disassembly has a mul or a madd, counted from where the function's code
starts, and `body` is where the code after the prologue does.  arm64's
alone, as the listing is.

Usage: python3 test/run_listing.py
"""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.environ.get("WARMC_BIN") or os.path.join(ROOT, BUILD, "warmc")
OBJDUMP = shutil.which("llvm-objdump") or shutil.which("objdump")
ARM64 = platform.machine() in ("arm64", "aarch64")

MODULE = """(module
  (func $deep (param $x i32) (param $y i32) (param $d i32) (result i32)
    (if (result i32) (i32.eqz (local.get $d))
      (then (i32.mul (local.get $x) (local.get $y)))
      (else (i32.add (call $deep (local.get $x) (local.get $y) (i32.sub (local.get $d) (i32.const 1)))
                     (i32.mul (local.get $y) (local.get $d))))))
  (func $out (export "out") (param $x i32) (param $y i32) (result i32)
    (i32.add (i32.mul (local.get $x) (local.get $y)) (i32.const 7)))
  (func (export "run") (param $x i32) (param $y i32) (result i32)
    (call $deep (local.get $x) (local.get $y) (i32.const 2)))
)
"""


def main():
    if not ARM64:
        print("listing: arm64's alone (skipped)")
        return 0
    if not OBJDUMP:
        print("FAIL[listing]: no llvm-objdump or objdump to read the object with")
        return 1
    os.chdir(ROOT)
    flags, skip = [], False
    for a in gen_gosp.target_flags(BUILD):
        if skip:
            skip = False
        elif a == "--rt":
            skip = True
        else:
            flags.append(a)
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        wat, obj = os.path.join(tmp, "l.wat"), os.path.join(tmp, "l.o")
        open(wat, "w").write(MODULE)
        r = subprocess.run([WARMC] + flags + ["-O2", "-fno-inline", "-c", "-o", obj, wat], capture_output=True, text=True,
                           env=dict(os.environ, WARMC_LISTING="out,deep"))
        if r.returncode:
            print("FAIL[listing]: the compile failed: %s" % r.stderr.strip()[:300])
            return 1
        d = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", obj], capture_output=True, text=True).stdout
        start, at = {}, {}
        for line in d.splitlines():
            m = re.match(r"^([0-9a-f]+) <_?(.*)>:$", line)
            if m:
                start[m.group(2)] = int(m.group(1), 16)
                continue
            m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)", line)
            if m:
                at[int(m.group(1), 16)] = m.group(2)
        cur, seen = None, {}
        for line in r.stderr.splitlines():
            m = re.match(r"^LISTING (\S+) func=(\d+) len=(\d+) body=(\d+) ", line)
            if m:
                # (the listing's name is the symbol's: wasm_func_N; the disassembly's is the function's)
                cur = ("deep", "out")[int(m.group(2))] if int(m.group(2)) < 2 else None
                if cur:
                    # the whole function's listing, then, where it was placed as a start and a full
                    # body, the start's: the whole one's code is the full body's.  Code starts at
                    # the check ahead of its symbol, where it has one
                    if cur not in seen:
                        base = start.get(cur + "$full", start.get(cur + "$entry", start.get(cur)))
                    else:
                        cur, base = cur + "'s start", start.get(cur)
                    seen[cur] = [base, int(m.group(4)), 0]
                    if base is None:
                        fails.append("%s is not in the disassembly" % cur)
                        cur = None
                    elif cur == "out" and base + int(m.group(4)) < start[cur]:
                        fails.append("out's body is listed at %d, ahead of its symbol at %d"
                                     % (int(m.group(4)), start[cur] - base))
                continue
            m = re.match(r"^@([0-9a-f]+) v\d+ (\w+)[=]", line)
            if m and cur and m.group(2) == "mul":
                off = int(m.group(1), 16)
                seen[cur][2] += 1
                op = at.get(seen[cur][0] + off)
                if op not in ("mul", "madd", "msub"):
                    fails.append("%s's mul is listed at %#x, where the code has %s" % (cur, off, op))
        for f in ("out", "deep", "deep's start"):
            if f not in seen:
                fails.append("no listing of %s" % f)
            elif not seen[f][2]:
                fails.append("the listing of %s has no mul" % f)
        if "out" in seen and "out$entry" not in start:
            fails.append("out has no check ahead of its symbol to count in")
    for f in fails:
        print("FAIL[listing]: " + f)
    print("listing: the offsets of an exported function and a called one: %s"
          % ("the code's" if not fails else "%d failure(s)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
