"""The TeaVM adapter (test/teavm/wasi.py) and the math it calls.

  * FdLibm.java against java.lang.StrictMath on the JVM, bit for bit
    (FdLibmCheck.java): it needs a JDK and the interop jar `make teavm`
    fetches, and is left out without them.
  * test/teavm/cases: modules shaped as TeaVM's wasm-gc target writes one,
    adapted and run under wasmtime and under warmc at -O0 and -O2, their
    stdout, exit status and stderr checked: every host call the adapter
    defines, a call it has no meaning for, an exception main lets out.  Most
    of these no Java program reaches on its own, so the Java corpora cannot
    show them.
  * The modules the adapter must refuse, and the names it keeps.
  * TeaVM with test/teavm/patches applied, on a Java program for each of its
    fixes (test/teavm/fixes): built by TeaVM's wasm-gc target as the program
    needs, with --strict or without, and run under wasmtime and warmc, each
    run's stdout, stderr and exit status the JVM's for the same classes.
    TeaVM's name section lists every map in index order.  It needs a JDK and
    `make teavm`.

Usage: python3 test/run_teavm.py
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")
CASES = os.path.join(ROOT, "test", "teavm", "cases")
sys.path.insert(0, os.path.join(ROOT, "test", "teavm"))
import wasi  # noqa: E402

TOOLS = wasi.wasm_tools()

def pinned(name):
    with open(os.path.join(ROOT, "Makefile")) as f:
        for line in f:
            if line.startswith(name + " "):
                return line.split(":=", 1)[1].strip()
    raise SystemExit("teavm: no %s in the Makefile" % name)

def host_flags():
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"', shell=True,
                       capture_output=True, text=True, cwd=ROOT,
                       env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    return r.stdout.split()

fails = []

def fail(msg):
    fails.append(msg)
    print("FAIL[teavm] " + msg)

def assemble(wat, out):
    """A .wat file, or module text given inline, to a binary module."""
    if wat.startswith("("):
        r = subprocess.run([TOOLS, "parse", "-o", out], input=wat.encode(), capture_output=True)
    else:
        r = subprocess.run([TOOLS, "parse", "-o", out, wat], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("teavm: cannot assemble %s: %s" % (wat[:60], r.stderr.decode()[:300]))

# name: (main class, exit status, stderr wanted in full or as a prefix (...))
RUNS = {
    "host": ("Main", 0, "E\n"),
    "unsupported": ("Main", 69, "teavm-wasi: teavmJso.callMethod1 has no meaning outside JavaScript\n"),
    "jsoname": ("Main", 69, "teavm-wasi: JavaScript's window has no meaning outside JavaScript\n"),
    "funcsrc": ("Main", 69, "teavm-wasi: a function made from JavaScript source has no meaning outside JavaScript\n"),
    "uncaught": ("Main", 1, "Uncaught Java exception: böom❗\n"),
    "uncaught_nomsg": ("Main", 1, "Uncaught Java exception\n"),
    "other_main": ("App", 0, ""),
    "bigdata": ("Main", 0, ""),
    "jsoprop": ("Main", 69, "teavm-wasi: JavaScript's lengt has no meaning outside JavaScript\n"),
    "jsomethod": ("Main", 69, "teavm-wasi: JavaScript's mark has no meaning outside JavaScript\n"),
    # a trap's status and message are each engine's own
    "trap": ("Main", None, None),
}

STDOUT = {
    "unsupported": b"ok",
    "jsoname": b"\n",
    "funcsrc": b"xy",
    "uncaught": b"pre",
    "uncaught_nomsg": b"\n",
    "other_main": b"A\n",
    "bigdata": b"data11\n",
    "jsoprop": b"",
    "jsomethod": b"",
    "trap": b"ok\n",
}

# new Function(ARG, BODY) applied to "Q": the identity prints Q, anything
# else stops the program.  One case for each test the identity has to pass.
FUNCSRC = [
    ("y", "return y;", True),
    ("str", "return str;", True),
    ("ab", "return ab;", True),
    ("y", "return 1;", False),      # the argument, character for character
    ("ab", "return ba;", False),
    ("y", "return y", False),       # the length: shorter
    ("y", "return y;;", False),     # and longer
    ("y", "retvrn y;", False),      # the `return `
    ("y", "return yx", False),      # the semicolon
    ("", "return ;", False),        # an argument at all
]

FUNCSRC_WAT = '''(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "wasm:js-string" "charCodeAt" (func $charCodeAt (param externref i32) (result i32)))
  (import "teavmJso" "createFunction1" (func $createFunction1 (param externref externref) (result (ref extern))))
  (import "teavmJso" "callFunction1" (func $callFunction1 (param externref externref) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (data (i32.const 0) "Q%s%s")
  (@custom "teavm.memoryRequirements" "{\\"min\\":1, \\"dataSize\\":64, \\"shared\\": false}")
  (func $mk (param $p i32) (param $n i32) (result (ref extern))
    (local $a (ref $chars)) (local $i i32)
    (local.set $a (array.new_default $chars (local.get $n)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (array.set $chars (local.get $a) (local.get $i)
          (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $fromArray (local.get $a) (i32.const 0) (local.get $n)))
  (func $Main::main_$caller$exported$0 (param $args externref)
    (call $putchar (call $charCodeAt (call $callFunction1
      (call $createFunction1 (call $mk (i32.const 1) (i32.const %d)) (call $mk (i32.const %d) (i32.const %d)))
      (call $mk (i32.const 0) (i32.const 1))) (i32.const 0)))
    (call $putchar (i32.const 10))))'''

# A string builtin's misuse traps, as the JS String Builtins proposal says:
# each of these is the body of a main that must not get past it.
TRAPS = [
    "(drop (call $fromCodePoint (i32.const 0x110000)))",
    "(drop (call $charCodeAt (call $s) (i32.const 3)))",
    "(drop (call $codePointAt (call $s) (i32.const -1)))",
    "(drop (call $length (call $wrapInt (i32.const 1))))",
    "(drop (call $length (ref.null extern)))",
    "(drop (call $concat (ref.null extern) (call $s)))",
    "(drop (call $concat (call $s) (call $wrapInt (i32.const 1))))",
    "(drop (call $substring (ref.null extern) (i32.const 0) (i32.const 0)))",
    "(drop (call $cast (ref.null extern)))",
    "(drop (call $equals (call $wrapInt (i32.const 1)) (ref.null extern)))",
    "(drop (call $equals (ref.null extern) (call $wrapInt (i32.const 1))))",
    "(drop (call $compare (ref.null extern) (call $s)))",
    "(drop (call $fromArray (array.new_default $chars (i32.const 4)) (i32.const 3) (i32.const 2)))",
    "(drop (call $fromArray (array.new_default $chars (i32.const 4)) (i32.const 0) (i32.const 5)))",
    "(drop (call $fromArray (ref.null $chars) (i32.const 0) (i32.const 0)))",
    "(drop (call $intoArray (call $s) (array.new_default $chars (i32.const 4)) (i32.const 2)))",
    "(drop (call $intoArray (call $s) (ref.null $chars) (i32.const 0)))",
]

TRAP_WAT = '''(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "wasm:js-string" "fromCodePoint" (func $fromCodePoint (param i32) (result (ref extern))))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "wasm:js-string" "intoCharCodeArray" (func $intoArray (param externref (ref null $chars) i32) (result i32)))
  (import "wasm:js-string" "charCodeAt" (func $charCodeAt (param externref i32) (result i32)))
  (import "wasm:js-string" "codePointAt" (func $codePointAt (param externref i32) (result i32)))
  (import "wasm:js-string" "length" (func $length (param externref) (result i32)))
  (import "wasm:js-string" "concat" (func $concat (param externref externref) (result (ref extern))))
  (import "wasm:js-string" "substring" (func $substring (param externref i32 i32) (result (ref extern))))
  (import "wasm:js-string" "equals" (func $equals (param externref externref) (result i32)))
  (import "wasm:js-string" "compare" (func $compare (param externref externref) (result i32)))
  (import "wasm:js-string" "cast" (func $cast (param externref) (result (ref extern))))
  (import "teavmJso" "wrapInt" (func $wrapInt (param i32) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\\"min\\":1, \\"dataSize\\":0, \\"shared\\": false}")
  (func $s (result (ref extern))
    (call $fromArray (array.new_fixed $chars 3 (i32.const 97) (i32.const 98) (i32.const 99)) (i32.const 0) (i32.const 3)))
  (func $Main::main_$caller$exported$0 (param $args externref)
    %s
    (call $putchar (i32.const 33))))'''

def run_traps(work, flags):
    # a name each: Windows can still hold an executable just run, and refuse
    # to let the next case's be written over it
    for i, body in enumerate(TRAPS):
        src = os.path.join(work, "trap_%d.wasm" % i)
        assemble(TRAP_WAT % body, src)
        adapted = os.path.join(work, "trap_%d.w.wasm" % i)
        with open(src, "rb") as f:
            err = wasi.write(f.read(), adapted)
        if err:
            fail("%s: the adapter refused it: %s" % (body, err))
            continue
        exe = os.path.join(work, "trap_%d" % i)
        r = subprocess.run([WARMC] + flags + ["-O2", "--wasi", "-o", exe, adapted],
                           capture_output=True, text=True)
        if r.returncode != 0:
            fail("%s: warmc: %s" % (body, r.stderr.strip()[:300]))
            continue
        for who, cmd in (("wasmtime", ["wasmtime", adapted]), ("warmc", [exe])):
            r = subprocess.run(cmd, capture_output=True)
            if r.returncode == 0 or r.stdout:
                fail("%s under %s: exit %d stdout %r, wanted a trap"
                     % (body, who, r.returncode, r.stdout))

def run_random(work):
    """Two runs, two first draws: the generator is seeded from random_get."""
    src = os.path.join(work, "random.wasm")
    assemble(os.path.join(CASES, "random.wat"), src)
    adapted = os.path.join(work, "random.w.wasm")
    with open(src, "rb") as f:
        err = wasi.write(f.read(), adapted)
    if err:
        fail("random: the adapter refused it: %s" % err)
        return
    a = subprocess.run(["wasmtime", adapted], capture_output=True).stdout
    b = subprocess.run(["wasmtime", adapted], capture_output=True).stdout
    if len(a) != 17 or a == b:
        fail("random: two runs drew %r and %r" % (a, b))

def run_funcsrc(work, flags):
    for i, (arg, body, identity) in enumerate(FUNCSRC):
        src = os.path.join(work, "funcsrc_%d.wasm" % i)
        assemble(FUNCSRC_WAT % (arg, body, len(arg), 1 + len(arg), len(body)), src)
        adapted = os.path.join(work, "funcsrc_%d.w.wasm" % i)
        with open(src, "rb") as f:
            err = wasi.write(f.read(), adapted)
        if err:
            fail("funcsrc %r %r: the adapter refused it: %s" % (arg, body, err))
            continue
        exe = os.path.join(work, "funcsrc_%d" % i)
        r = subprocess.run([WARMC] + flags + ["-O2", "--wasi", "-o", exe, adapted],
                           capture_output=True, text=True)
        if r.returncode != 0:
            fail("funcsrc %r %r: warmc: %s" % (arg, body, r.stderr.strip()[:300]))
            continue
        for who, cmd in (("wasmtime", ["wasmtime", adapted]), ("warmc", [exe])):
            r = subprocess.run(cmd, capture_output=True)
            want = (0, b"Q\n") if identity else (69, b"")
            if (r.returncode, r.stdout) != want:
                fail("new Function(%r, %r) under %s: exit %d stdout %r, wanted %s"
                     % (arg, body, who, r.returncode, r.stdout, want))

def run_cases(work, flags):
    for name, (main_class, want_rc, want_err) in RUNS.items():
        src = os.path.join(work, name + ".wasm")
        assemble(os.path.join(CASES, name + ".wat"), src)
        adapted = os.path.join(work, name + ".w.wasm")
        with open(src, "rb") as f:
            err = wasi.write(f.read(), adapted, main_class)
        if err:
            fail("%s: the adapter refused it: %s" % (name, err))
            continue
        if name == "host":
            with open(os.path.join(CASES, "host.out"), "rb") as f:
                want_out = f.read()
        else:
            want_out = STDOUT[name]
        runs = [("wasmtime", ["wasmtime", adapted])]
        for level in ("-O0", "-O2"):
            exe = os.path.join(work, "%s%s" % (name, level))
            r = subprocess.run([WARMC] + flags + [level, "--wasi", "-o", exe, adapted],
                               capture_output=True, text=True)
            if r.returncode != 0:
                fail("%s %s: warmc: %s" % (name, level, r.stderr.strip()[:300]))
                continue
            runs.append(("warmc " + level, [exe]))
        for who, cmd in runs:
            r = subprocess.run(cmd, capture_output=True)
            got_err = r.stderr.decode("utf-8", "replace")
            if want_rc is None:
                if r.returncode == 0:
                    fail("%s under %s: exit 0, wanted a trap" % (name, who))
            elif r.returncode != want_rc:
                fail("%s under %s: exit %d, wanted %d (stderr %r)"
                     % (name, who, r.returncode, want_rc, got_err[:200]))
            if r.stdout != want_out:
                fail("%s under %s: stdout %r, wanted %r"
                     % (name, who, r.stdout[:120], want_out[:120]))
            if want_err is not None and got_err != want_err:
                fail("%s under %s: stderr %r, wanted %r" % (name, who, got_err[:200], want_err))

# What the adapter must refuse, and the reason it gives.
REFUSE = {
    "no memoryRequirements section": ('''(module (import "env" "memory" (memory 1))
        (type $o (sub (struct))) (tag (export "teavm.javaException") (param (ref null $o)))
        (func $Main::main_$caller$exported$0 (param externref)))''',
        "no teavm.memoryRequirements section"),
    "no memory": ('''(module (type $o (sub (struct))) (tag (export "teavm.javaException") (param (ref null $o)))
        (@custom "teavm.memoryRequirements" "{\\"min\\":1, \\"dataSize\\":0, \\"shared\\": false}")
        (func $Main::main_$caller$exported$0 (param externref)))''',
        "imports no memory from env"),
    "a table import": ('''(module (import "env" "memory" (memory 1)) (import "env" "tbl" (table 1 funcref))
        (type $o (sub (struct))) (tag (export "teavm.javaException") (param (ref null $o)))
        (@custom "teavm.memoryRequirements" "{\\"min\\":1, \\"dataSize\\":0, \\"shared\\": false}")
        (func $Main::main_$caller$exported$0 (param externref)))''',
        "cannot define import env.tbl (table)"),
    "no exception tag": ('''(module (import "env" "memory" (memory 1))
        (@custom "teavm.memoryRequirements" "{\\"min\\":1, \\"dataSize\\":0, \\"shared\\": false}")
        (func $Main::main_$caller$exported$0 (param externref)))''',
        "no teavm.javaException tag exported"),
}

def run_refusals(work):
    for what, (wat, reason) in REFUSE.items():
        src = os.path.join(work, "refuse.wasm")
        assemble(wat, src)
        with open(src, "rb") as f:
            err = wasi.write(f.read(), os.path.join(work, "refuse.w.wasm"))
        if not err or reason not in err:
            fail("a module with %s: the adapter said %r, wanted %r" % (what, err, reason))
    with open(os.path.join(work, "other_main.wasm"), "rb") as f:
        err = wasi.write(f.read(), os.path.join(work, "x.wasm"), "Main")
    if not err or "is Main the main class" not in err:
        fail("other_main as Main: the adapter said %r" % err)

def out_of_order(b):
    """The name map entries of module b that follow one of a higher index,
    as (subsection, index) or (subsection, index, inner index)."""
    bad = []
    def check(p, indirect, sub):
        n, i = wasi.uleb(p, 0)
        last = -1
        for _ in range(n):
            idx, i = wasi.uleb(p, i)
            if idx <= last:
                bad.append((sub, idx))
            last = idx
            if indirect:
                m, i = wasi.uleb(p, i)
                inner = -1
                for _ in range(m):
                    a, i = wasi.uleb(p, i)
                    ln, i = wasi.uleb(p, i)
                    i += ln
                    if a <= inner:
                        bad.append((sub, idx, a))
                    inner = a
            else:
                ln, i = wasi.uleb(p, i)
                i += ln
    for sid, _, j, e in wasi.sections(b):
        if sid != 0:
            continue
        n, k = wasi.uleb(b, j)
        if b[k:k + n] != b"name":
            continue
        k += n
        while k < e:
            sub = b[k]
            n, q = wasi.uleb(b, k + 1)
            if sub in wasi.NAME_MAPS or sub in wasi.NAME_INDIRECT:
                check(b[q:q + n], sub in wasi.NAME_INDIRECT, sub)
            k = q + n
    return bad

def run_names(work):
    src = os.path.join(work, "host.wasm")
    with open(src, "rb") as f:
        b = f.read()
    out = os.path.join(work, "names.w.wasm")
    err = wasi.write(b, out)
    if err:
        fail("names: the adapter refused host: %s" % err)
        return
    with open(out, "rb") as f:
        if out_of_order(f.read()):
            fail("names: the adapted module's name section is out of index order")
    r = subprocess.run([TOOLS, "print", out], capture_output=True, text=True)
    for want in ("(func $Main::main_$caller$exported$0", "(func $mk", "(func $__w.start",
                 "(import \"wasi_snapshot_preview1\" \"fd_write\" (func $__w.fd_write"):
        if want not in r.stdout:
            fail("names: %r not in the adapted module's names" % want)
    if re.search(r'\$"?#(func|type|local|label|global|field|elem|data|tag)\d', r.stdout):
        fail("names: the adapted module names something after wasm-tools' made-up names")
    # what the loader was to supply is all defined now, and the math is
    # reached through teavmMath alone
    for gone in ('"warmc.fdlibm.', '"teavm.imports"', '(import "teavm', '(import "env"'):
        if gone in r.stdout:
            fail("adapted module still has %s" % gone)
    if '(export "memory"' not in r.stdout or '(export "_start"' not in r.stdout:
        fail("adapted module exports no memory or no _start")

TEAVM = os.path.join(ROOT, "build", "tools", "teavm-" + pinned("TEAVM_VERSION"))
FIXES = os.path.join(ROOT, "test", "teavm", "fixes")
# Each program in test/teavm/fixes and the builds of it, by TeaVM's wasm-gc
# target without --strict (wasm) and with it (strict).  A zero divisor and a
# null dereference are exceptions with --strict, traps without, so a program
# that catches one is built with it.
PATCHED = [
    ("division", ("strict",)),
    ("division_nonzero", ("wasm",)),
    ("unsigned", ("strict",)),
    ("math", ("wasm",)),
    ("arrayname", ("wasm", "strict")),
    ("flush", ("wasm", "strict")),
]

def run_patched(work, flags):
    stamp = os.path.join(TEAVM, "patched", ".complete")
    if not shutil.which("javac") or not os.path.exists(stamp):
        print("teavm: the patched TeaVM left out (needs a JDK and `make teavm`)")
        return 0
    patches = os.path.join(ROOT, "test", "teavm", "patches")
    made_from = [patches, os.path.join(ROOT, "test", "teavm", "patch.py")] + [
        os.path.join(patches, f) for f in os.listdir(patches)]
    if os.path.getmtime(stamp) < max(os.path.getmtime(p) for p in made_from):
        fail("TeaVM's patched classes are older than test/teavm/patches: run `make teavm`")
        return 0
    modes = sorted({m for _, ms in PATCHED for m in ms})
    driver = os.path.join(work, "driver")
    r = subprocess.run(["javac", "--release", "17", "-nowarn", "-cp", os.path.join(TEAVM, "*"),
                        "-d", driver, os.path.join(ROOT, "test", "teavm", "TeaVMBuild.java")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        fail("TeaVMBuild.java: javac: %s" % r.stderr[:400])
        return 0
    cp = os.pathsep.join([os.path.join(TEAVM, "patched"), os.path.join(TEAVM, "*"), driver])
    runs, refs = 0, {}
    for mode in modes:
        names = [n for n, ms in PATCHED if mode in ms]
        out = {n: os.path.join(work, "%s.%s" % (n, mode)) for n in names}
        cmd = ["java", "-cp", cp, "warmc.teavm.TeaVMBuild", "--teavm", TEAVM]
        if mode == "strict":
            cmd.append("--strict")
        cmd += ["--support", os.path.join(ROOT, "test", "teavm", "FdLibm.java")]
        r = subprocess.run(cmd + ["%s=%s" % (os.path.join(FIXES, n), out[n]) for n in names],
                           capture_output=True, text=True)
        if r.returncode != 0:
            fail("TeaVM, %s: %s" % (mode, r.stderr[:1500]))
        for n in names:
            if not os.path.isdir(os.path.join(out[n], "classes")):
                continue
            if n not in refs:
                refs[n] = subprocess.run(["java", "-cp", os.path.join(out[n], "classes"), "Main"],
                                         capture_output=True)
            ref = refs[n]
            wasm = os.path.join(out[n], "main.wasm")
            if not os.path.exists(wasm):
                continue
            with open(wasm, "rb") as f:
                b = f.read()
            bad = out_of_order(b)
            if bad:
                fail("%s, %s: TeaVM's name section is out of index order at %s" % (n, mode, bad[:4]))
            # without --strict a zero divisor traps, unchecked, as a project
            # builds by default and javabench is measured
            text = subprocess.run([TOOLS, "print", wasm], capture_output=True, text=True).stdout
            if mode == "wasm" and "call $org.teavm.backend.wasm.runtime.WasmGCSupport::divisionByZero" in text:
                fail("%s, wasm: a division checks its divisor without --strict" % n)
            adapted = os.path.join(work, "%s.%s.w.wasm" % (n, mode))
            err = wasi.write(b, adapted)
            if err:
                fail("%s, %s: the adapter refused it: %s" % (n, mode, err))
                continue
            tried = [("under wasmtime", ["wasmtime", adapted])]
            exe = os.path.join(work, "%s.%s.warmc" % (n, mode))
            r = subprocess.run([WARMC] + flags + ["-O2", "--wasi", "-o", exe, adapted],
                               capture_output=True, text=True)
            if r.returncode != 0:
                fail("%s, %s: warmc: %s" % (n, mode, r.stderr.strip()[:300]))
            else:
                tried.append(("under warmc -O2", [exe]))
            for who, run in tried:
                got = subprocess.run(run, capture_output=True)
                runs += 1
                for what, a, b in (("exit status", got.returncode, ref.returncode),
                                   ("stdout", got.stdout, ref.stdout), ("stderr", got.stderr, ref.stderr)):
                    if a != b:
                        if isinstance(a, bytes):
                            k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                            a, b = a[max(0, k - 40):k + 80], b[max(0, k - 40):k + 80]
                        fail("%s, %s, %s: %s %r, the JVM's %r" % (n, mode, who, what, a, b))
                        break
    return runs

def run_fdlibm(work):
    version = pinned("TEAVM_VERSION")
    interop = os.path.join(ROOT, "build", "tools", "teavm-" + version,
                           "teavm-interop-%s.jar" % version)
    if not shutil.which("javac") or not os.path.exists(interop):
        print("teavm: fdlibm left out (needs a JDK and `make teavm`)")
        return
    classes = os.path.join(work, "fdlibm")
    src = [os.path.join(ROOT, "test", "teavm", f) for f in ("FdLibm.java", "FdLibmCheck.java")]
    r = subprocess.run(["javac", "--release", "17", "-nowarn", "-cp", interop, "-d", classes] + src,
                       capture_output=True, text=True)
    if r.returncode != 0:
        fail("fdlibm: javac: %s" % r.stderr[:400])
        return
    r = subprocess.run(["java", "-cp", classes + os.pathsep + interop, "warmc.teavm.FdLibmCheck",
                        os.environ.get("FDLIBM_COUNT", "100000")], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        fail("fdlibm: FdLibm.java differs from StrictMath")

def main():
    if not os.path.exists(WARMC):
        print("teavm: %s not built" % WARMC, file=sys.stderr)
        return 1
    if not shutil.which("wasmtime"):
        print("teavm: wasmtime not installed, skipping", file=sys.stderr)
        return 0
    work = tempfile.mkdtemp(prefix="teavm.", dir=os.path.join(ROOT, BUILD))
    try:
        run_fdlibm(work)
        flags = host_flags()
        run_cases(work, flags)
        run_funcsrc(work, flags)
        run_traps(work, flags)
        run_random(work)
        run_refusals(work)
        run_names(work)
        patched = run_patched(work, flags)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("teavm: %d failure(s)" % len(fails))
        return 1
    print("teavm: %d adapted modules agree under wasmtime and warmc -O0/-O2, %d functions from "
          "source told apart, %d misused string builtins trap, %d modules refused as they "
          "should be, %d runs of the patched TeaVM's programs as the JVM runs them"
          % (len(RUNS), len(FUNCSRC), len(TRAPS), len(REFUSE) + 1, patched))
    return 0

if __name__ == "__main__":
    sys.exit(main())
