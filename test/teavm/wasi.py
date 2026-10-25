"""Turn a module TeaVM's Wasm GC backend wrote into a WASI command.

TeaVM 0.16 has no WASI target.  Its `wasm-gc` one writes a module for a
JavaScript host -- TeaVM's own loader, core/src/main/js/wasm-gc-runtime/
runtime.ts in its repository -- and everything outside the program comes in
through imports: the linear memory (`env.memory`) and where its heap begins,
`System.out` a UTF-16 code unit at a time (`teavmConsole.putcharStdout`),
`Math.sin` (`teavmMath.sin`), the clock, and the JS strings and objects its
JSO layer makes at start-up to export `main` to JavaScript.  No engine outside
a browser or Node provides those, so a TeaVM module does not run under warmc or
wasmtime as it comes.

This defines every one of them inside the module instead, in terms of
wasi_snapshot_preview1 and of wasm, and adds a `_start` that runs `main` the way
the loader's `main([])` does.  TeaVM's own code is left exactly as TeaVM wrote
it: the module is printed with wasm-tools, its import lines are replaced by
functions, globals and a memory of the same names, and it is assembled again,
so every index stays the reference it was.  What the corpus measures is
TeaVM's output, with the host calls a few hundred lines of wasm beside it.

What each host call becomes:

  * the memory is defined and exported as `memory`, for WASI; its heap starts
    past TeaVM's data and a region of this adapter's own (buffers, messages);
  * `putcharStdout` and `putcharStderr` encode the code units as UTF-8 and
    write a line at a time, as the loader's console.log does, the rest at exit;
  * `currentTimeMillis` and `performance.now()`, which is what System.nanoTime
    asks JSO for, read WASI's realtime and monotonic clocks;
  * `Math.sqrt`, `floor`, `ceil` and `abs` are wasm instructions, and
    `Math.random` is xorshift128+ seeded from random_get, as V8's is;
  * the other `Math` functions -- sin, cos, exp, log, pow and the rest --
    call test/teavm/FdLibm.java, compiled by TeaVM into the module beside the
    program: fdlibm, which is what Java's StrictMath is and what V8 ports;
  * the `wasm:js-string` builtins work on JS strings that are wasm arrays of
    UTF-16 code units, which is all a JS string is to the module;
  * JSO's start-up calls -- creating the exported class, defining `main` on it
    -- make placeholder objects, and the few JSO calls TeaVM's class library
    makes on its own (performance.now, the length of main's argument array)
    answer as JavaScript would;
  * weak references stay strong, which the Java memory model allows.

Any other host call means the program uses JavaScript itself -- JSO, the DOM,
threads on JS timers -- and has no WASI meaning: it stops the program with a
message naming the call, exit status 69 (sysexits' EX_UNAVAILABLE; the WASI
hosts keep 70 to 74 for their own failures).  An exception main does not
catch is printed as the loader reports it and exits 1.

Usage: python3 test/teavm/wasi.py IN.wasm OUT.wasm [MAIN_CLASS]

MAIN_CLASS (default `Main`) is the class TeaVM was given as the entry point.
"""

import json
import os
import re
import subprocess
import sys

# ---------------------------------------------------------------- binary

def uleb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7f) << s
        s += 7
        if not x & 0x80:
            return r, i

def enc(n):
    out = bytearray()
    while True:
        x = n & 0x7f
        n >>= 7
        if n:
            out.append(x | 0x80)
        else:
            out.append(x)
            return bytes(out)

def sections(b):
    i = 8
    while i < len(b):
        n, j = uleb(b, i + 1)
        yield b[i], i, j, j + n
        i = j + n

def custom(b, want):
    """The payload of the custom section named `want`, or None."""
    for sid, _, j, e in sections(b):
        if sid == 0:
            n, k = uleb(b, j)
            if b[k:k + n] == want:
                return b[k + n:e]
    return None

# The name section's subsections that hold a name map, and those that hold an
# indirect one (a map per function or per type).
NAME_MAPS = {1, 4, 5, 6, 7, 8, 9, 11}
NAME_INDIRECT = {2, 3, 10}

def drop_synthetic(p, indirect):
    """A name map without the names wasm-tools made up (`#func12`): the ones
    its printer gave what TeaVM left unnamed, which its parser then kept."""
    n, i = uleb(p, 0)
    ents = []
    for _ in range(n):
        idx, i = uleb(p, i)
        if indirect:
            m, i = uleb(p, i)
            sub = []
            for _ in range(m):
                a, i = uleb(p, i)
                ln, i = uleb(p, i)
                if not p[i:i + ln].startswith(b"#"):
                    sub.append((a, p[i:i + ln]))
                i += ln
            if sub:
                ents.append((idx, sub))
        else:
            ln, i = uleb(p, i)
            if not p[i:i + ln].startswith(b"#"):
                ents.append((idx, p[i:i + ln]))
            i += ln
    out = bytearray(enc(len(ents)))
    for idx, v in ents:
        out += enc(idx)
        if indirect:
            out += enc(len(v))
            for a, nm in v:
                out += enc(a) + enc(len(nm)) + nm
        else:
            out += enc(len(v)) + v
    return bytes(out)

def rewrite_names(b, fix):
    """The module with each map of its name section passed through `fix`."""
    for sid, s, j, e in sections(b):
        if sid != 0:
            continue
        n, k = uleb(b, j)
        if b[k:k + n] != b"name":
            continue
        k += n
        out = bytearray()
        while k < e:
            sub = b[k]
            n, q = uleb(b, k + 1)
            body = b[q:q + n]
            if sub in NAME_MAPS:
                body = fix(body, False)
            elif sub in NAME_INDIRECT:
                body = fix(body, True)
            out += bytes([sub]) + enc(len(body)) + body
            k = q + n
        payload = enc(4) + b"name" + bytes(out)
        return b[:s] + b"\x00" + enc(len(payload)) + payload + b[e:]
    return b

# ---------------------------------------------------------------- text

def sexprs(s):
    """The top-level parenthesised groups of `s`, as strings."""
    out, depth, start = [], 0, None
    i = 0
    while i < len(s):
        c = s[i]
        if c == '"':
            i += 1
            while s[i] != '"':
                i += 2 if s[i] == "\\" else 1
        elif c == "(":
            if depth == 0:
                start = i
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                out.append(s[start:i + 1])
        i += 1
    return out

class Sig:
    """A function's parameter and result types, as text."""
    def __init__(self, text):
        self.typeuse = ""
        self.params, self.results = [], []
        for g in sexprs(text):
            head = g[1:].split(None, 1)
            if head[0] == "type":
                self.typeuse = g
            elif head[0] == "param":
                rest = head[1] if len(head) > 1 else ""
                if rest.startswith("$"):
                    rest = rest.split(None, 1)[1]
                self.params.append(rest.strip()[:-1].strip())
            elif head[0] == "result":
                self.results.append(head[1].strip()[:-1].strip())

    def header(self):
        out = self.typeuse
        for p in self.params:
            out += " (param %s)" % p
        for r in self.results:
            out += " (result %s)" % r
        return out

IMPORT = re.compile(r'^  \(import "([^"]*)" "([^"]*)" \((func|global|memory|table|tag) (\$\S+) \(;(\d+);\)(.*)\)\)$')
FUNC = re.compile(r'^  \(func (\$\S+) \(;(\d+);\)(.*)$')
EXPORT = re.compile(r'^  \(export "([^"]*)" \((func|global|memory|table|tag) (\$\S+|\d+)\)\)$')
TAG = re.compile(r'^  \(tag (\$\S+) \(;(\d+);\)(.*)\)$')

def ref_index(t):
    """`$x` out of `(ref null $x)` or `(ref $x)`."""
    m = re.match(r"\(ref (?:null )?(\$\S+)\)$", t)
    return m.group(1) if m else None

# ---------------------------------------------------------------- adapter

STDOUT_BUF = 4096
STDERR_BUF = 1024

class Layout:
    """Where the adapter's own memory goes: past TeaVM's data, before its heap."""
    def __init__(self, data_size):
        a = (data_size + 15) & ~15
        self.iov = a              # one iovec
        self.nw = a + 8           # fd_write's count
        self.scratch = a + 16     # 16 bytes for clocks and random_get
        self.out = a + 32
        self.err = self.out + STDOUT_BUF
        self.msgs = self.err + STDERR_BUF
        self.text = bytearray()

    def string(self, s):
        """An address and a length for `s`, in the adapter's data."""
        b = s.encode()
        at = self.text.find(b)
        if at < 0:
            at = len(self.text)
            self.text += b
        return self.msgs + at, len(b)

    def end(self):
        return (self.msgs + len(self.text) + 15) & ~15

def wat_bytes(b):
    return "".join(chr(c) if 32 <= c < 127 and c not in (34, 92) else "\\%02x" % c
                   for c in b)

# Every host function this knows.  Each takes the import's signature and the
# layout and returns the function's body, or None for a call that has no
# meaning outside JavaScript.

def ext(v):
    return "(extern.convert_any %s)" % v

def num(v):
    return ext("(struct.new $__w.num %s)" % v)

def obj(kind):
    return ext("(struct.new $__w.obj (i32.const %d))" % kind)

# The JS objects the host makes, by kind: one it knows nothing more of,
# `performance`, main's argument array, and a function returning its argument.
OBJ_OTHER, OBJ_PERFORMANCE, OBJ_ARGS, OBJ_IDENTITY = 0, 1, 2, 3

def is_name(lay, s, text):
    p, n = lay.string(text)
    return "(call $__w.str_is %s (i32.const %d) (i32.const %d))" % (s, p, n)

def h_math1(op):
    return lambda sig, lay: "(%s.%s (local.get 0))" % (sig.params[0], op)

def h_fdlibm(sig, lay, fn):
    args = " ".join("(local.get %d)" % i for i in range(len(sig.params)))
    return "(call %s %s)" % (fn, args)

def h_weak_ref(sig, lay):
    return ext("(local.get 0)")

def h_deref(sig, lay):
    r = sig.results[0]
    return "(ref.cast %s (any.convert_extern (local.get 0)))" % r

def h_global(sig, lay):
    return ("(if (result externref) %s (then %s) (else (call $__w.unsupported_jso (local.get 0)) (unreachable)))"
            % (is_name(lay, "(local.get 0)", "performance"), obj(OBJ_PERFORMANCE)))

def h_call_method0(sig, lay):
    # performance.now(): the one method TeaVM's class library calls by itself
    return ("(if (result externref) (i32.and (call $__w.is_obj (local.get 0) (i32.const %d)) %s)"
            " (then %s) (else (call $__w.unsupported_jso (local.get 1)) (unreachable)))"
            % (OBJ_PERFORMANCE, is_name(lay, "(local.get 1)", "now"),
               num("(call $__w.now)")))

def h_get_property(sig, lay):
    # main's argument array, whose length unwrapStringArray asks for: none
    return ("(if (result externref) (i32.and (call $__w.is_obj (local.get 0) (i32.const %d)) %s)"
            " (then %s) (else (call $__w.unsupported_jso (local.get 1)) (unreachable)))"
            % (OBJ_ARGS, is_name(lay, "(local.get 1)", "length"), num("(f64.const 0)")))

def h_wrap(sig, lay):
    t = sig.params[0]
    v = {"i32": "(f64.convert_i32_s (local.get 0))", "f32": "(f64.promote_f32 (local.get 0))",
         "f64": "(local.get 0)"}[t]
    return num(v)

def h_unwrap(sig, lay):
    t = sig.results[0]
    v = "(call $__w.num_of (local.get 0))"
    return {"i32": "(i32.trunc_sat_f64_s %s)" % v, "f32": "(f32.demote_f64 %s)" % v,
            "f64": v}[t]

def h_unwrap_bool(sig, lay):
    return "(f64.ne (call $__w.num_of (local.get 0)) (f64.const 0))"

def h_new_obj(sig, lay):
    return obj(OBJ_OTHER) if sig.results else ""

def h_create_function1(sig, lay):
    # new Function(arg, body): JSString's valueOf and stringValue are
    # `str => str`, and Throwable's constructor calls one with its class name
    return ("(if (result (ref extern)) (call $__w.is_identity (local.get 0) (local.get 1))"
            " (then %s) (else %s))" % (obj(OBJ_IDENTITY), obj(OBJ_OTHER)))

def h_call_function1(sig, lay):
    p, n = lay.string("a function made from JavaScript source")
    return ("(if (result externref) (call $__w.is_obj (local.get 0) (i32.const %d))"
            " (then (local.get 1)) (else (call $__w.unsupported (i32.const %d) (i32.const %d)) (unreachable)))"
            % (OBJ_IDENTITY, p, n))

JS_STRING = {
    "cast": lambda sig, lay: ext("(call $__w.s (local.get 0))"),
    "test": lambda sig, lay: "(ref.test (ref $__w.str) (any.convert_extern (local.get 0)))",
    "fromCharCode": lambda sig, lay:
        ext("(array.new_fixed $__w.str 1 (local.get 0))"),
    "fromCodePoint": lambda sig, lay: ext("(call $__w.from_code_point (local.get 0))"),
    "charCodeAt": lambda sig, lay:
        "(array.get_u $__w.str (call $__w.s (local.get 0)) (local.get 1))",
    "codePointAt": lambda sig, lay: "(call $__w.code_point_at (call $__w.s (local.get 0)) (local.get 1))",
    "length": lambda sig, lay: "(array.len (call $__w.s (local.get 0)))",
    "concat": lambda sig, lay:
        ext("(call $__w.concat (call $__w.s (local.get 0)) (call $__w.s (local.get 1)))"),
    "substring": lambda sig, lay:
        ext("(call $__w.substring (call $__w.s (local.get 0)) (local.get 1) (local.get 2))"),
    "equals": lambda sig, lay: "(call $__w.str_eq (local.get 0) (local.get 1))",
    "compare": lambda sig, lay:
        "(call $__w.str_cmp (call $__w.s (local.get 0)) (call $__w.s (local.get 1)))",
    "fromCharCodeArray": lambda sig, lay:
        ext("(call $__w.from_array (local.get 0) (local.get 1) (local.get 2))"),
    "intoCharCodeArray": lambda sig, lay:
        "(call $__w.into_array (call $__w.s (local.get 0)) (local.get 1) (local.get 2))",
}

HOST = {
    ("teavmConsole", "putcharStdout"): lambda sig, lay: "(call $__w.putchar (i32.const 1) (local.get 0))",
    ("teavmConsole", "putcharStderr"): lambda sig, lay: "(call $__w.putchar (i32.const 2) (local.get 0))",
    ("teavmDate", "currentTimeMillis"): lambda sig, lay: "(call $__w.millis)",
    ("teavmMemory", "notifyHeapResized"): lambda sig, lay: "",
    ("teavmMath", "sqrt"): h_math1("sqrt"),
    ("teavmMath", "ceil"): h_math1("ceil"),
    ("teavmMath", "floor"): h_math1("floor"),
    ("teavmMath", "fabs"): h_math1("abs"),
    ("teavmMath", "random"): lambda sig, lay: "(call $__w.random)",
    ("teavm", "isnan"): lambda sig, lay: "(%s.ne (local.get 0) (local.get 0))" % sig.params[0],
    ("teavm", "isfinite"): lambda sig, lay:
        "(%s.lt (%s.abs (local.get 0)) (%s.const inf))" % ((sig.params[0],) * 3),
    ("teavm", "takeStackTrace"): lambda sig, lay: "(ref.null extern)",
    ("teavm", "decorateException"): lambda sig, lay: "",
    ("teavm", "createWeakRef"): h_weak_ref,
    ("teavm", "createStringWeakRef"): h_weak_ref,
    ("teavm", "deref"): h_deref,
    ("teavm", "stringDeref"): h_deref,
    ("teavmJso", "stringBuiltinsSupported"): lambda sig, lay: "(i32.const 1)",
    ("teavmJso", "isUndefined"): lambda sig, lay: "(i32.const 0)",
    ("teavmJso", "createClass"): h_new_obj,
    ("teavmJso", "defineFunction"): h_new_obj,
    ("teavmJso", "defineStaticMethod"): h_new_obj,
    ("teavmJso", "defineMethod"): h_new_obj,
    ("teavmJso", "defineProperty"): h_new_obj,
    ("teavmJso", "defineStaticProperty"): h_new_obj,
    ("teavmJso", "global"): h_global,
    ("teavmJso", "callMethod0"): h_call_method0,
    ("teavmJso", "getProperty"): h_get_property,
    ("teavmJso", "callFunction1"): h_call_function1,
    ("teavmJso", "wrapInt"): h_wrap,
    ("teavmJso", "wrapDouble"): h_wrap,
    ("teavmJso", "wrapFloat"): h_wrap,
    ("teavmJso", "wrapBoolean"): h_wrap,
    ("teavmJso", "wrapByte"): h_wrap,
    ("teavmJso", "wrapShort"): h_wrap,
    ("teavmJso", "wrapChar"): h_wrap,
    ("teavmJso", "unwrapInt"): h_unwrap,
    ("teavmJso", "unwrapDouble"): h_unwrap,
    ("teavmJso", "unwrapFloat"): h_unwrap,
    ("teavmJso", "unwrapByte"): h_unwrap,
    ("teavmJso", "unwrapShort"): h_unwrap,
    ("teavmJso", "unwrapChar"): h_unwrap,
    ("teavmJso", "unwrapBoolean"): h_unwrap_bool,
}

def host_body(module, name, sig, lay, fdlibm):
    if module == "wasm:js-string":
        h = JS_STRING.get(name)
        return h(sig, lay) if h else None
    if module == "teavmJso" and name == "createFunction1":
        return h_create_function1(sig, lay)
    if module == "teavmJso" and name.startswith("createFunction"):
        return h_new_obj(sig, lay)
    if module == "teavmMath" and name in fdlibm:
        return h_fdlibm(sig, lay, fdlibm[name])
    h = HOST.get((module, name))
    return h(sig, lay) if h else None

# The adapter's own functions.  {...} fields are filled from the layout.
RUNTIME = r"""
  (type $__w.str (array (mut i16)))
  (type $__w.num (struct (field f64)))
  (type $__w.obj (struct (field i32)))
  (global $__w.out_len (mut i32) (i32.const 0))
  (global $__w.err_len (mut i32) (i32.const 0))
  (global $__w.out_hi (mut i32) (i32.const 0))
  (global $__w.err_hi (mut i32) (i32.const 0))
  (global $__w.rng0 (mut i64) (i64.const 0))
  (global $__w.rng1 (mut i64) (i64.const 0))
  (global $__w.t0 (mut i64) (i64.const -1))

  ;; Write all of [ptr, ptr+len) to fd, or as much as the host takes.
  (func $__w.write (param $fd i32) (param $ptr i32) (param $len i32)
    (local $n i32)
    (block $done
      (loop $more
        (br_if $done (i32.eqz (local.get $len)))
        (i32.store (i32.const {iov}) (local.get $ptr))
        (i32.store (i32.const {iov4}) (local.get $len))
        (br_if $done (call $__w.fd_write (local.get $fd) (i32.const {iov}) (i32.const 1) (i32.const {nw})))
        (local.set $n (i32.load (i32.const {nw})))
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $ptr (i32.add (local.get $ptr) (local.get $n)))
        (local.set $len (i32.sub (local.get $len) (local.get $n)))
        (br $more))))

  (func $__w.flush (param $fd i32)
    (if (i32.eq (local.get $fd) (i32.const 1))
      (then
        (if (global.get $__w.out_hi)
          (then (global.set $__w.out_hi (i32.const 0)) (call $__w.code_point (i32.const 1) (i32.const 0xfffd))))
        (call $__w.write (i32.const 1) (i32.const {out}) (global.get $__w.out_len))
        (global.set $__w.out_len (i32.const 0)))
      (else
        (if (global.get $__w.err_hi)
          (then (global.set $__w.err_hi (i32.const 0)) (call $__w.code_point (i32.const 2) (i32.const 0xfffd))))
        (call $__w.write (i32.const 2) (i32.const {err}) (global.get $__w.err_len))
        (global.set $__w.err_len (i32.const 0)))))

  (func $__w.byte (param $fd i32) (param $b i32)
    (if (i32.eq (local.get $fd) (i32.const 1))
      (then
        (if (i32.ge_u (global.get $__w.out_len) (i32.const {out_size}))
          (then
            (call $__w.write (i32.const 1) (i32.const {out}) (global.get $__w.out_len))
            (global.set $__w.out_len (i32.const 0))))
        (i32.store8 (i32.add (i32.const {out}) (global.get $__w.out_len)) (local.get $b))
        (global.set $__w.out_len (i32.add (global.get $__w.out_len) (i32.const 1))))
      (else
        (if (i32.ge_u (global.get $__w.err_len) (i32.const {err_size}))
          (then
            (call $__w.write (i32.const 2) (i32.const {err}) (global.get $__w.err_len))
            (global.set $__w.err_len (i32.const 0))))
        (i32.store8 (i32.add (i32.const {err}) (global.get $__w.err_len)) (local.get $b))
        (global.set $__w.err_len (i32.add (global.get $__w.err_len) (i32.const 1))))))

  ;; One code point as UTF-8.
  (func $__w.code_point (param $fd i32) (param $c i32)
    (if (i32.lt_u (local.get $c) (i32.const 0x80))
      (then (call $__w.byte (local.get $fd) (local.get $c)) (return)))
    (if (i32.lt_u (local.get $c) (i32.const 0x800))
      (then
        (call $__w.byte (local.get $fd) (i32.or (i32.const 0xc0) (i32.shr_u (local.get $c) (i32.const 6))))
        (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (local.get $c) (i32.const 0x3f))))
        (return)))
    (if (i32.lt_u (local.get $c) (i32.const 0x10000))
      (then
        (call $__w.byte (local.get $fd) (i32.or (i32.const 0xe0) (i32.shr_u (local.get $c) (i32.const 12))))
        (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (i32.shr_u (local.get $c) (i32.const 6)) (i32.const 0x3f))))
        (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (local.get $c) (i32.const 0x3f))))
        (return)))
    (call $__w.byte (local.get $fd) (i32.or (i32.const 0xf0) (i32.shr_u (local.get $c) (i32.const 18))))
    (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (i32.shr_u (local.get $c) (i32.const 12)) (i32.const 0x3f))))
    (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (i32.shr_u (local.get $c) (i32.const 6)) (i32.const 0x3f))))
    (call $__w.byte (local.get $fd) (i32.or (i32.const 0x80) (i32.and (local.get $c) (i32.const 0x3f)))))

  ;; One UTF-16 code unit: a surrogate pair is one code point, a lone
  ;; surrogate U+FFFD, as console.log writes them; a line is written when it
  ;; ends, as console.log writes one.
  (func $__w.putchar (param $fd i32) (param $c i32)
    (local $hi i32)
    (local.set $c (i32.and (local.get $c) (i32.const 0xffff)))
    (if (i32.eq (local.get $fd) (i32.const 1))
      (then (local.set $hi (global.get $__w.out_hi)) (global.set $__w.out_hi (i32.const 0)))
      (else (local.set $hi (global.get $__w.err_hi)) (global.set $__w.err_hi (i32.const 0))))
    (if (local.get $hi)
      (then
        (if (i32.eq (i32.and (local.get $c) (i32.const 0xfc00)) (i32.const 0xdc00))
          (then
            (call $__w.code_point (local.get $fd)
              (i32.add (i32.const 0x10000)
                (i32.or (i32.shl (i32.sub (local.get $hi) (i32.const 0xd800)) (i32.const 10))
                        (i32.sub (local.get $c) (i32.const 0xdc00)))))
            (return)))
        (call $__w.code_point (local.get $fd) (i32.const 0xfffd))))
    (if (i32.eq (i32.and (local.get $c) (i32.const 0xfc00)) (i32.const 0xd800))
      (then
        (if (i32.eq (local.get $fd) (i32.const 1))
          (then (global.set $__w.out_hi (local.get $c)))
          (else (global.set $__w.err_hi (local.get $c))))
        (return)))
    (if (i32.eq (i32.and (local.get $c) (i32.const 0xfc00)) (i32.const 0xdc00))
      (then (local.set $c (i32.const 0xfffd))))
    (call $__w.code_point (local.get $fd) (local.get $c))
    (if (i32.eq (local.get $c) (i32.const 10))
      (then (call $__w.flush (local.get $fd)))))

  ;; Date.getTime(): whole milliseconds of the realtime clock.
  (func $__w.millis (result f64)
    (drop (call $__w.clock_time_get (i32.const 0) (i64.const 1000000) (i32.const {scratch})))
    (f64.convert_i64_s (i64.div_s (i64.load (i32.const {scratch})) (i64.const 1000000))))

  ;; performance.now(): milliseconds of the monotonic clock since the first call.
  (func $__w.now (result f64)
    (local $t i64)
    (drop (call $__w.clock_time_get (i32.const 1) (i64.const 1) (i32.const {scratch})))
    (local.set $t (i64.load (i32.const {scratch})))
    (if (i64.lt_s (global.get $__w.t0) (i64.const 0))
      (then (global.set $__w.t0 (local.get $t))))
    (f64.div (f64.convert_i64_s (i64.sub (local.get $t) (global.get $__w.t0))) (f64.const 1e6)))

  ;; Math.random(): xorshift128+, the 53 high bits of each step.
  (func $__w.random (result f64)
    (local $s0 i64) (local $s1 i64)
    (if (i64.eqz (i64.or (global.get $__w.rng0) (global.get $__w.rng1)))
      (then
        (drop (call $__w.random_get (i32.const {scratch}) (i32.const 16)))
        (global.set $__w.rng0 (i64.load (i32.const {scratch})))
        (global.set $__w.rng1 (i64.load (i32.const {scratch8})))
        (if (i64.eqz (i64.or (global.get $__w.rng0) (global.get $__w.rng1)))
          (then (global.set $__w.rng0 (i64.const 1))))))
    (local.set $s1 (global.get $__w.rng0))
    (local.set $s0 (global.get $__w.rng1))
    (global.set $__w.rng0 (local.get $s0))
    (local.set $s1 (i64.xor (local.get $s1) (i64.shl (local.get $s1) (i64.const 23))))
    (global.set $__w.rng1
      (i64.xor (i64.xor (local.get $s1) (local.get $s0))
               (i64.xor (i64.shr_u (local.get $s1) (i64.const 17)) (i64.shr_u (local.get $s0) (i64.const 26)))))
    (f64.mul (f64.convert_i64_u (i64.shr_u (i64.add (global.get $__w.rng1) (local.get $s0)) (i64.const 11)))
             (f64.const 0x1p-53)))

  ;; A JS string, which traps on anything else, as the builtins do.
  (func $__w.s (param $v externref) (result (ref $__w.str))
    (ref.cast (ref $__w.str) (any.convert_extern (local.get $v))))

  (func $__w.concat (param $a (ref $__w.str)) (param $b (ref $__w.str)) (result (ref $__w.str))
    (local $r (ref $__w.str))
    (local.set $r (array.new_default $__w.str (i32.add (array.len (local.get $a)) (array.len (local.get $b)))))
    (array.copy $__w.str $__w.str (local.get $r) (i32.const 0) (local.get $a) (i32.const 0) (array.len (local.get $a)))
    (array.copy $__w.str $__w.str (local.get $r) (array.len (local.get $a)) (local.get $b) (i32.const 0) (array.len (local.get $b)))
    (local.get $r))

  ;; The builtin's substring: the end clamped to the length, and empty where
  ;; the start is past it -- or past the length, which is then past the end.
  (func $__w.substring (param $s (ref $__w.str)) (param $from i32) (param $to i32) (result (ref $__w.str))
    (local $n i32) (local $r (ref $__w.str))
    (local.set $n (array.len (local.get $s)))
    (if (i32.gt_u (local.get $to) (local.get $n)) (then (local.set $to (local.get $n))))
    (if (i32.ge_u (local.get $from) (local.get $to))
      (then (return (array.new_fixed $__w.str 0))))
    (local.set $r (array.new_default $__w.str (i32.sub (local.get $to) (local.get $from))))
    (array.copy $__w.str $__w.str (local.get $r) (i32.const 0) (local.get $s) (local.get $from) (i32.sub (local.get $to) (local.get $from)))
    (local.get $r))

  (func $__w.from_code_point (param $c i32) (result (ref $__w.str))
    (if (i32.gt_u (local.get $c) (i32.const 0x10ffff)) (then (unreachable)))
    (if (i32.lt_u (local.get $c) (i32.const 0x10000))
      (then (return (array.new_fixed $__w.str 1 (local.get $c)))))
    (local.set $c (i32.sub (local.get $c) (i32.const 0x10000)))
    (array.new_fixed $__w.str 2
      (i32.or (i32.const 0xd800) (i32.shr_u (local.get $c) (i32.const 10)))
      (i32.or (i32.const 0xdc00) (i32.and (local.get $c) (i32.const 0x3ff)))))

  (func $__w.code_point_at (param $s (ref $__w.str)) (param $i i32) (result i32)
    (local $c i32) (local $d i32)
    (local.set $c (array.get_u $__w.str (local.get $s) (local.get $i)))
    (if (i32.and (i32.eq (i32.and (local.get $c) (i32.const 0xfc00)) (i32.const 0xd800))
                 (i32.lt_u (i32.add (local.get $i) (i32.const 1)) (array.len (local.get $s))))
      (then
        (local.set $d (array.get_u $__w.str (local.get $s) (i32.add (local.get $i) (i32.const 1))))
        (if (i32.eq (i32.and (local.get $d) (i32.const 0xfc00)) (i32.const 0xdc00))
          (then
            (return (i32.add (i32.const 0x10000)
              (i32.or (i32.shl (i32.sub (local.get $c) (i32.const 0xd800)) (i32.const 10))
                      (i32.sub (local.get $d) (i32.const 0xdc00)))))))))
    (local.get $c))

  ;; equals takes null, which equals only null; anything else must be a string.
  (func $__w.str_eq (param $a externref) (param $b externref) (result i32)
    (if (i32.eqz (ref.is_null (local.get $a))) (then (drop (call $__w.s (local.get $a)))))
    (if (i32.eqz (ref.is_null (local.get $b))) (then (drop (call $__w.s (local.get $b)))))
    (if (i32.or (ref.is_null (local.get $a)) (ref.is_null (local.get $b)))
      (then (return (i32.and (ref.is_null (local.get $a)) (ref.is_null (local.get $b))))))
    (i32.eqz (call $__w.str_cmp (call $__w.s (local.get $a)) (call $__w.s (local.get $b)))))

  (func $__w.str_cmp (param $a (ref $__w.str)) (param $b (ref $__w.str)) (result i32)
    (local $i i32) (local $n i32) (local $x i32) (local $y i32)
    (local.set $n (array.len (local.get $a)))
    (if (i32.lt_u (array.len (local.get $b)) (local.get $n)) (then (local.set $n (array.len (local.get $b)))))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $x (array.get_u $__w.str (local.get $a) (local.get $i)))
        (local.set $y (array.get_u $__w.str (local.get $b) (local.get $i)))
        (if (i32.ne (local.get $x) (local.get $y))
          (then (return (select (i32.const -1) (i32.const 1) (i32.lt_u (local.get $x) (local.get $y))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.sub (i32.gt_u (array.len (local.get $a)) (array.len (local.get $b)))
             (i32.lt_u (array.len (local.get $a)) (array.len (local.get $b)))))

  ;; Whether string v is the n bytes of ASCII at p.
  (func $__w.str_is (param $v externref) (param $p i32) (param $n i32) (result i32)
    (local $s (ref null $__w.str)) (local $i i32)
    (if (i32.eqz (ref.test (ref $__w.str) (any.convert_extern (local.get $v))))
      (then (return (i32.const 0))))
    (local.set $s (ref.cast (ref $__w.str) (any.convert_extern (local.get $v))))
    (if (i32.ne (array.len (local.get $s)) (local.get $n)) (then (return (i32.const 0))))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.ne (array.get_u $__w.str (local.get $s) (local.get $i))
                    (i32.load8_u (i32.add (local.get $p) (local.get $i))))
          (then (return (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.const 1))

  (func $__w.is_obj (param $v externref) (param $kind i32) (result i32)
    (if (i32.eqz (ref.test (ref $__w.obj) (any.convert_extern (local.get $v))))
      (then (return (i32.const 0))))
    (i32.eq (struct.get $__w.obj 0 (ref.cast (ref $__w.obj) (any.convert_extern (local.get $v))))
            (local.get $kind)))

  ;; Whether body, a function's source, is `return arg;` for an argument
  ;; there is: with none, `return ;` gives undefined.
  (func $__w.is_identity (param $arg externref) (param $body externref) (result i32)
    (local $a (ref null $__w.str)) (local $b (ref null $__w.str)) (local $i i32) (local $n i32)
    (if (i32.eqz (i32.and (ref.test (ref $__w.str) (any.convert_extern (local.get $arg)))
                          (ref.test (ref $__w.str) (any.convert_extern (local.get $body)))))
      (then (return (i32.const 0))))
    (local.set $a (ref.cast (ref $__w.str) (any.convert_extern (local.get $arg))))
    (local.set $b (ref.cast (ref $__w.str) (any.convert_extern (local.get $body))))
    (local.set $n (array.len (local.get $a)))
    (if (i32.eqz (local.get $n)) (then (return (i32.const 0))))
    (if (i32.ne (array.len (local.get $b)) (i32.add (local.get $n) (i32.const 8)))
      (then (return (i32.const 0))))
    (if (i32.eqz (call $__w.str_is (extern.convert_any (call $__w.substring (ref.as_non_null (local.get $b)) (i32.const 0) (i32.const 7)))
                                   (i32.const {msg_return}) (i32.const 7)))
      (then (return (i32.const 0))))
    (if (i32.ne (array.get_u $__w.str (local.get $b) (i32.add (local.get $n) (i32.const 7))) (i32.const 59))
      (then (return (i32.const 0))))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.ne (array.get_u $__w.str (local.get $a) (local.get $i))
                    (array.get_u $__w.str (local.get $b) (i32.add (local.get $i) (i32.const 7))))
          (then (return (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.const 1))

  ;; A JS number; null, as undefined arrives, is NaN, and Int unwraps it to 0.
  (func $__w.num_of (param $v externref) (result f64)
    (if (ref.is_null (local.get $v)) (then (return (f64.const nan))))
    (struct.get $__w.num 0 (ref.cast (ref $__w.num) (any.convert_extern (local.get $v)))))

  ;; Write the bytes at [p, p+n) to stderr.
  (func $__w.say (param $p i32) (param $n i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (call $__w.byte (i32.const 2) (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next))))

  ;; A JS string to stderr.
  (func $__w.say_str (param $v externref)
    (local $s (ref null $__w.str)) (local $i i32)
    (if (i32.eqz (ref.test (ref $__w.str) (any.convert_extern (local.get $v)))) (then (return)))
    (local.set $s (ref.cast (ref $__w.str) (any.convert_extern (local.get $v))))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (array.len (local.get $s))))
        (call $__w.putchar (i32.const 2) (array.get_u $__w.str (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next))))

  ;; A host call with no meaning outside JavaScript: say which, and stop.
  (func $__w.unsupported (param $p i32) (param $n i32)
    (call $__w.flush (i32.const 1))
    (call $__w.say (i32.const {msg_pre}) (i32.const {msg_pre_n}))
    (call $__w.say (local.get $p) (local.get $n))
    (call $__w.say (i32.const {msg_post}) (i32.const {msg_post_n}))
    (call $__w.flush (i32.const 2))
    (call $__w.proc_exit (i32.const 69))
    (unreachable))

  ;; JSO asked for a property or global of JavaScript's by name.
  (func $__w.unsupported_jso (param $name externref)
    (call $__w.flush (i32.const 1))
    (call $__w.say (i32.const {msg_jso}) (i32.const {msg_jso_n}))
    (call $__w.say_str (local.get $name))
    (call $__w.say (i32.const {msg_post}) (i32.const {msg_post_n}))
    (call $__w.flush (i32.const 2))
    (call $__w.proc_exit (i32.const 69))
    (unreachable))
"""

def runtime(lay):
    pre = lay.string("teavm-wasi: ")
    jso = lay.string("teavm-wasi: JavaScript's ")
    post = lay.string(" has no meaning outside JavaScript\n")
    ret = lay.string("return ")
    return RUNTIME.format(
        iov=lay.iov, iov4=lay.iov + 4, nw=lay.nw, scratch=lay.scratch,
        scratch8=lay.scratch + 8, out=lay.out, err=lay.err,
        out_size=STDOUT_BUF, err_size=STDERR_BUF,
        msg_pre=pre[0], msg_pre_n=pre[1], msg_jso=jso[0], msg_jso_n=jso[1],
        msg_post=post[0], msg_post_n=post[1], msg_return=ret[0])

def into_array_funcs(arr):
    """fromCharCodeArray and intoCharCodeArray, on TeaVM's char array type.
    Each traps where the builtin does by the copy alone: a null array, a range
    past its end, and -- through a length that is negative as i32 -- a start
    past the end."""
    return r"""
  (func $__w.from_array (param $a (ref null {arr})) (param $from i32) (param $to i32) (result (ref $__w.str))
    (local $r (ref $__w.str))
    (local.set $r (array.new_default $__w.str (i32.sub (local.get $to) (local.get $from))))
    (array.copy $__w.str {arr} (local.get $r) (i32.const 0) (local.get $a) (local.get $from) (i32.sub (local.get $to) (local.get $from)))
    (local.get $r))

  (func $__w.into_array (param $s (ref $__w.str)) (param $a (ref null {arr})) (param $at i32) (result i32)
    (array.copy {arr} $__w.str (local.get $a) (local.get $at) (local.get $s) (i32.const 0) (array.len (local.get $s)))
    (array.len (local.get $s)))
""".replace("{arr}", arr)

WASI = r"""
  (import "wasi_snapshot_preview1" "fd_write" (func $__w.fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get" (func $__w.clock_time_get (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "random_get" (func $__w.random_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $__w.proc_exit (param i32)))
"""

class Unsupported(Exception):
    pass

def adapt(wasm, main_class="Main", tools="wasm-tools"):
    """The WASI module for TeaVM module `wasm` (bytes), as text."""
    req = custom(wasm, b"teavm.memoryRequirements")
    if req is None:
        raise Unsupported("no teavm.memoryRequirements section: not a module "
                          "TeaVM's wasm-gc target wrote")
    req = json.loads(req.decode())
    lay = Layout(req.get("dataSize", 0))

    r = subprocess.run([tools, "print", "--name-unnamed"], input=wasm,
                       capture_output=True, check=True)
    lines = r.stdout.decode().split("\n")
    if not lines[0].startswith("(module") or lines[-2] != ")":
        raise Unsupported("unexpected module text from %s print" % tools)

    # Signatures by name, and names by kind and index: wasm-tools names an
    # export's item by index where it has no name of its own to print, which
    # it does for a tag even with --name-unnamed.
    funcs, tags, exports, by_index = {}, {}, {}, {}
    for ln in lines:
        m = IMPORT.match(ln)
        if m:
            by_index[(m.group(3), m.group(5))] = m.group(4)
            if m.group(3) == "func":
                funcs[m.group(4)] = m.group(6)
            continue
        m = FUNC.match(ln)
        if m:
            funcs[m.group(1)] = m.group(3)
            by_index[("func", m.group(2))] = m.group(1)
            continue
        m = TAG.match(ln)
        if m:
            tags[m.group(1)] = m.group(3)
            by_index[("tag", m.group(2))] = m.group(1)
    for ln in lines:
        m = EXPORT.match(ln)
        if m:
            ref = m.group(3)
            exports[m.group(1)] = ref if ref.startswith("$") else by_index.get((m.group(2), ref))

    # The math TeaVM compiled from FdLibm.java, exported as warmc.fdlibm.NAME.
    fdlibm = {n[len("warmc.fdlibm."):]: f for n, f in exports.items()
              if n.startswith("warmc.fdlibm.")}

    out, defs, arrays, missing = [], [], set(), []
    memory = heap = None
    for ln in lines[1:-2]:
        m = IMPORT.match(ln)
        if not m:
            if ln.startswith('  (@custom "teavm.imports"'):
                continue            # what the loader was to supply, all defined here now
            m = EXPORT.match(ln)
            if m and m.group(1).startswith("warmc.fdlibm."):
                continue            # reached through teavmMath, if at all
            out.append(ln)
            continue
        module, name, kind, ident, _, rest = m.groups()
        # The memory and the heap's start are filled in last, once every
        # message the adapter keeps in its region is known.
        if (module, name, kind) == ("env", "memory", "memory"):
            memory = (ident, rest.split())
            defs.append(None)
        elif (module, kind) == ("teavmMemory", "global") and name in ("heapOffset", "maxSize"):
            if name == "heapOffset":
                heap = (len(defs), ident, rest)
            defs.append("  (global %s%s (i32.const %d))" % (ident, rest, 0x7fffffff))
        elif kind == "func":
            sig = Sig(rest)
            body = host_body(module, name, sig, lay, fdlibm)
            if module == "wasm:js-string" and name in ("fromCharCodeArray", "intoCharCodeArray"):
                p = sig.params[0 if name == "fromCharCodeArray" else 1]
                arrays.add(ref_index(p))
            if body is None:
                missing.append("%s.%s" % (module, name))
                p, n = lay.string("%s.%s" % (module, name))
                body = "(call $__w.unsupported (i32.const %d) (i32.const %d)) (unreachable)" % (p, n)
            defs.append("  (func %s %s %s)" % (ident, sig.header(), body))
        else:
            raise Unsupported("cannot define import %s.%s (%s)" % (module, name, kind))
    if memory is None:
        raise Unsupported("the module imports no memory from env")
    if len(arrays) > 1:
        raise Unsupported("TeaVM passed more than one array type to the string builtins")

    entry = "$%s::main_$caller$exported$0" % main_class
    if entry not in funcs:
        raise Unsupported("no %s: is %s the main class TeaVM was given?" % (entry, main_class))
    tag = exports.get("teavm.javaException")
    if tag is None or tag not in tags:
        raise Unsupported("no teavm.javaException tag exported")
    exn = Sig(tags[tag]).params[0]
    message = exports.get("teavm.exceptionMessage")
    to_js = exports.get("teavm.stringToJs")

    uncaught = ['    (call $__w.flush (i32.const 1))']
    p, n = lay.string("Uncaught Java exception")
    uncaught.append('    (call $__w.say (i32.const %d) (i32.const %d))' % (p, n))
    if message and to_js:
        msig, jsig = Sig(funcs[message]), Sig(funcs[to_js])
        p, n = lay.string(": ")
        uncaught.append(
            '    (local.set $m (call %s (ref.cast %s (local.get $e))))\n'
            '    (if (i32.eqz (ref.is_null (local.get $m)))\n'
            '      (then (call $__w.say (i32.const %d) (i32.const %d))\n'
            '            (call $__w.say_str (call %s (ref.cast %s (local.get $m))))))'
            % (message, msig.params[0], p, n, to_js, jsig.params[0]))
        mresult = msig.results[0]
    else:
        mresult = "anyref"
    uncaught.append('    (call $__w.byte (i32.const 2) (i32.const 10))')
    uncaught.append('    (call $__w.flush (i32.const 2))')
    uncaught.append('    (call $__w.proc_exit (i32.const 1))')

    start = """
  ;; main([]), as TeaVM's loader calls it; an exception it lets out is
  ;; reported as the loader reports it, and the program exits 1.
  (func $__w.start (export "_start")
    (local $e %s) (local $m %s)
    (block $ok
      (local.set $e
        (block $thrown (result %s)
          (try_table (catch %s $thrown)
            (call %s %s))
          (br $ok)))
%s)
    (call $__w.flush (i32.const 1))
    (call $__w.flush (i32.const 2)))
""" % (exn, mresult, exn, tag, entry, obj(OBJ_ARGS), "\n".join(uncaught))

    # An export can be what declares a function for ref.func; one removed
    # above is declared instead, where anything takes a reference to it.
    refd = [f for f in fdlibm.values() if ("ref.func " + f) in r.stdout.decode()]
    rt = runtime(lay)
    if refd:
        rt += "  (elem declare func %s)\n" % " ".join(refd)
    if arrays:
        rt += into_array_funcs(arrays.pop())
    rt += start
    data = '  (data $__w.text (i32.const %d) "%s")' % (lay.msgs, wat_bytes(bytes(lay.text)))

    ident, limits = memory
    shared = "shared" in limits
    limits = [x for x in limits if x != "shared"]
    lo = int(limits[0]) + (lay.end() - lay.iov + 0xffff) // 0x10000
    defs[defs.index(None)] = '  (memory %s (export "memory") %d %s%s)' % (
        ident, lo, " ".join(limits[1:]), " shared" if shared else "")
    if heap:
        at, ident, rest = heap
        defs[at] = "  (global %s%s (i32.const %d))" % (ident, rest, lay.end())

    text = "\n".join([lines[0], WASI.strip("\n")] + defs + out + [rt, data, ")", ""])
    return text, missing

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def wasm_tools():
    """The wasm-tools `make wasm-tools` pins, or else whichever is on PATH."""
    pinned = os.path.join(ROOT, os.environ.get("BUILD", "build"), "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")
    return pinned if os.path.exists(pinned) else "wasm-tools"

def write(wasm, dst, main_class="Main"):
    """Write the WASI module for TeaVM module `wasm` (bytes) to dst, in one
    step, so a reader sees the old file or the new one.  None, or what went
    wrong."""
    tools = wasm_tools()
    try:
        text, _ = adapt(wasm, main_class, tools)
    except Unsupported as e:
        return str(e)
    except subprocess.CalledProcessError as e:
        return e.stderr.decode()[:400]
    part = "%s.%d.part" % (dst, os.getpid())
    r = subprocess.run([tools, "parse", "-o", part], input=text.encode(),
                       capture_output=True)
    if r.returncode != 0:
        return "the adapted module does not assemble:\n" + r.stderr.decode()[:1200]
    with open(part, "rb") as f:
        out = rewrite_names(f.read(), drop_synthetic)
    with open(part, "wb") as f:
        f.write(out)
    r = subprocess.run([tools, "validate", part], capture_output=True)
    if r.returncode != 0:
        os.remove(part)
        return "the adapted module is not valid:\n" + r.stderr.decode()[:1200]
    os.replace(part, dst)
    return None

def main():
    args = sys.argv[1:]
    if len(args) not in (2, 3):
        print(__doc__.strip().split("\n\n")[-2], file=sys.stderr)
        return 2
    with open(args[0], "rb") as f:
        err = write(f.read(), args[1], args[2] if len(args) == 3 else "Main")
    if err:
        print("teavm-wasi: %s: %s" % (args[0], err), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
