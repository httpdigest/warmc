"""Generate a module that puts the register allocator under real pressure.

More simultaneously-live values than there are registers forces spilling,
eviction and rematerialisation -- the paths where allocation bugs hide and
which ordinary test cases never reach.

Twenty-four such functions, all in *one* module, because on macOS the first
execution of a freshly linked binary costs a fifth of a second of signature
validation and that validation does not run concurrently with itself: a suite
that links one executable per case pays for the cases one at a time however
many cores it has.  Allocation is per function, so nothing is lost by sharing
a module -- and the helpers and the memory were identical across them anyway.
"""
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/testrun/pressure"
os.makedirs(OUT, exist_ok=True)

def hot(name, k, call, floats):
    t = "f64" if floats else "i32"
    L = [f' (func ${name} (param $n i32) (result i32)']
    for i in range(k):
        L.append(f"  (local $v{i} {t})")
    L.append("  (local $acc i32)")
    for i in range(k):
        L.append(f"  (local.set $v{i} ({t}.const {i * 7 + 1}))")
    L.append("  (block $done (loop $l")
    L.append("    (br_if $done (i32.eqz (local.get $n)))")
    for i in range(k):
        if floats:
            L.append(f"    (local.set $v{i} (f64.add (local.get $v{i}) "
                     f"(f64.mul (local.get $v{(i + 3) % k}) (f64.const 1.25))))")
        else:
            L.append(f"    (local.set $v{i} (i32.add (local.get $v{i}) "
                     f"(i32.mul (local.get $v{(i + 3) % k}) (i32.const 3))))")
    if call:
        if floats:
            L.append("    (local.set $v0 (call $fside (local.get $v0)))")
        else:
            L.append("    (local.set $v0 (call $side (local.get $v0)))")
        ints = " ".join(f"(i32.const {i + 1})" for i in range(11))
        flts = " ".join(f"(f64.const {i + 1}.5)" for i in range(10))
        L.append(f"    (local.set $acc (i32.add (local.get $acc) (call $wide {ints} {flts})))")
    if floats:
        L.append("    (f64.store (i32.and (i32.mul (i32.trunc_f64_s (local.get $v1)) "
                 "(i32.const 8)) (i32.const 0xff8)) (local.get $v2))")
    else:
        L.append("    (i32.store (i32.and (i32.mul (local.get $v1) (i32.const 4)) "
                 "(i32.const 0xffc)) (local.get $v2))")
    for i in range(k):
        if floats:
            L.append(f"    (local.set $v{i} (f64.div (local.get $v{i}) "
                     f"(f64.add (f64.abs (local.get $v{(i + 5) % k})) (f64.const 1.75))))")
        else:
            L.append(f"    (local.set $v{i} (i32.xor (local.get $v{i}) "
                     f"(i32.rotl (local.get $v{(i + 5) % k}) (i32.const {i % 31}))))")
    L.append("    (local.set $n (i32.sub (local.get $n) (i32.const 1)))")
    L.append("    (br $l)))")
    acc = "(local.get $acc)"
    for i in range(k):
        v = f"(i32.trunc_f64_s (f64.trunc (local.get $v{i})))" if floats else f"(local.get $v{i})"
        acc = f"(i32.add {acc} {v})"
    L.append(f"  {acc})")
    L.append(f' (export "{name}" (func ${name}))')
    return "\n".join(L)

def hotv(name, k, call):
    """k simultaneously-live v128 values, optionally across a call.  A vector
    live over a call cannot use v8-v15 (callees preserve only the low
    halves), so this is the shape that forces the allocator's
    caller-save-around-call and spill paths for the vector class.  Lane 3 is
    folded into the result on purpose: it lives in the half of the register
    that a callee is allowed to destroy."""
    L = [f' (func ${name} (param $n i32) (result i32)']
    for i in range(k):
        L.append(f"  (local $v{i} v128)")
    L.append("  (local $acc i32)")
    for i in range(k):
        L.append(f"  (local.set $v{i} (i32x4.splat (i32.const {i * 7 + 1})))")
    L.append("  (block $done (loop $l")
    L.append("    (br_if $done (i32.eqz (local.get $n)))")
    for i in range(k):
        L.append(f"    (local.set $v{i} (i32x4.add (local.get $v{i}) "
                 f"(i32x4.mul (local.get $v{(i + 3) % k}) "
                 f"(i32x4.splat (i32.const 3)))))")
    if call:
        L.append("    (local.set $acc (i32.add (local.get $acc) "
                 "(call $side (local.get $acc))))")
    for i in range(k):
        L.append(f"    (local.set $v{i} (v128.xor (local.get $v{i}) "
                 f"(i8x16.swizzle (local.get $v{(i + 5) % k}) "
                 f"(local.get $v{(i + 1) % k}))))")
    L.append("    (local.set $n (i32.sub (local.get $n) (i32.const 1)))")
    L.append("    (br $l)))")
    acc = "(local.get $acc)"
    for i in range(k):
        acc = (f"(i32.add {acc} (i32.add "
               f"(i32x4.extract_lane 0 (local.get $v{i})) "
               f"(i32x4.extract_lane 3 (local.get $v{i}))))")
    L.append(f"  {acc})")
    L.append(f' (export "{name}" (func ${name}))')
    return "\n".join(L)

def bigframe(name, k):
    L = [f' (func ${name} (param $n i32) (result i32)']
    for i in range(k):
        L.append(f"  (local $v{i} i32)")
    L.append("  (local $acc i32)")
    for i in range(k):
        L.append(f"  (local.set $v{i} (i32.add (local.get $n) (i32.const {i * 3})))")

    L.append("  (local.set $acc (i32.add (call_indirect (type $two) (i32.const 0))))")
    acc = "(local.get $acc)"
    for i in range(k):
        acc = f"(i32.add {acc} (local.get $v{i}))"
    L.append(f"  {acc})")
    L.append(f' (export "{name}" (func ${name}))')
    return "\n".join(L)

def bigtailcall(name, k):
    L = [f' (func ${name} (param $n i32) (result i32)']
    for i in range(k):
        L.append(f"  (local $v{i} i32)")
    for i in range(k):
        L.append(f"  (local.set $v{i} (i32.add (local.get $n) (i32.const {i})))")

    acc = "(i32.const 0)"
    for i in range(k):
        acc = f"(i32.add {acc} (local.get $v{i}))"
    ints = " ".join(f"(local.get $v{i})" for i in range(10)) + " " + acc
    flts = " ".join(f"(f64.convert_i32_s (local.get $v{i}))" for i in range(10))
    L.append(f"  (return_call $wide {ints} {flts}))")
    L.append(f' (export "{name}" (func ${name}))')
    return "\n".join(L)

def farframe(name, ni, nf, nv, tail=False, params=False):
    """More slots than an arm64 ldr/str reaches from sp with its scaled
    offset -- 32 KB of them -- of every kind: i32, f64 and v128 values all
    live across a call that returns two results, so the slots are laid out
    past the rest of the frame and each far one is reached through a
    borrowed register.  The values mix n non-linearly and are folded in
    order, so that no pass can sum them away.  With tail, the result leaves
    through a tail call to $wide; with params, the function takes $wide's
    parameters, some of which arrive on the stack above the far frame."""
    sig = (f"(param {wide_params})" if params else "(param $n i32)")
    L = [f' (func ${name} {sig} (result i32)']
    if params:
        L.append("  (local $n i32)")
    for i in range(ni):
        L.append(f"  (local $v{i} i32)")
    for i in range(nf):
        L.append(f"  (local $f{i} f64)")
    for i in range(nv):
        L.append(f"  (local $w{i} v128)")
    L.append("  (local $acc i32) (local $accf f64) (local $accv v128)")
    if params:
        L.append("  (local.set $n (i32.add (local.get 9) "
                 "(i32.trunc_f64_s (local.get 20))))")
    for i in range(ni):
        L.append(f"  (local.set $v{i} (i32.xor (i32.mul (local.get $n) "
                 f"(i32.const {(i * 2654435761 + 1) & 0x7fffffff})) "
                 f"(i32.add (local.get $n) (i32.const {i}))))")
    for i in range(nf):
        L.append(f"  (local.set $f{i} (f64.mul (f64.convert_i32_s (i32.add "
                 f"(local.get $n) (i32.const {i}))) (f64.const 1.5)))")
    for i in range(nv):
        L.append(f"  (local.set $w{i} (v128.xor (i32x4.splat (local.get $n)) "
                 f"(v128.const i32x4 {i} {2 * i} {3 * i} {4 * i})))")
    L.append("  (local.set $acc (i32.add (call_indirect (type $two) (i32.const 0))))")
    for i in range(ni):
        L.append(f"  (local.set $acc (i32.add (i32.mul (local.get $acc) "
                 f"(i32.const 31)) (local.get $v{i})))")
    for i in range(nf):
        L.append(f"  (local.set $accf (f64.add (local.get $accf) (local.get $f{i})))")
    for i in range(nv):
        L.append(f"  (local.set $accv (i32x4.add (local.get $accv) (local.get $w{i})))")
    L.append("  (local.set $acc (i32.add (local.get $acc) (i32.add "
             "(i32.trunc_sat_f64_s (f64.div (local.get $accf) (f64.const 1000))) "
             "(i32.xor (i32x4.extract_lane 0 (local.get $accv)) "
             "(i32x4.extract_lane 3 (local.get $accv))))))")
    if tail:
        ints = " ".join(f"(local.get $v{i})" for i in range(10)) + " (local.get $acc)"
        flts = " ".join(f"(local.get $f{i})" for i in range(10))
        L.append(f"  (return_call $wide {ints} {flts}))")
    else:
        L.append("  (local.get $acc))")
    if not params:
        L.append(f' (export "{name}" (func ${name}))')
    return "\n".join(L)

wide_params = " ".join(["i32"] * 11 + ["f64"] * 10)
wide_body = ("(i32.add (local.get 0) (i32.add (local.get 7) (i32.add (local.get 8) "
             "(i32.add (local.get 9) (i32.add (local.get 10) "
             "(i32.trunc_f64_s (f64.add (local.get 19) (local.get 20))))))))")

parts = ["(module (memory 1 8)",
         " (type $two (func (result i32 i32)))",
         " (table 1 funcref) (elem (i32.const 0) $pair)",
         " (func $pair (type $two) (i32.const 3) (i32.const 4))",
         " (func $side (param i32) (result i32) (i32.add (local.get 0) (i32.const 1)))",
         " (func $fside (param f64) (result f64) (f64.add (local.get 0) (f64.const 1.5)))",
         f" (func $wide (param {wide_params}) (result i32) {wide_body})"]
made = []
for k in (6, 9, 12, 18, 26, 34):
    for call in (0, 1):
        for floats in (0, 1):
            name = "p%d_%d_%d" % (k, call, floats)
            parts.append(hot(name, k, call, floats))
            made.append(name)
for k in (12, 26, 34):
    for call in (0, 1):
        name = "pv%d_%d" % (k, call)
        parts.append(hotv(name, k, call))
        made.append(name)
parts.append(bigframe("bigframe", 400))
made.append("bigframe")
parts.append(bigtailcall("bigtailcall", 400))
made.append("bigtailcall")
parts.append(farframe("farframe", 2500, 1000, 400))
made.append("farframe")
parts.append(farframe("fartailcall", 2500, 1000, 400, tail=True))
made.append("fartailcall")
parts.append(farframe("farparams", 2500, 1000, 400, params=True))
parts.append(' (func $farcall (param $n i32) (result i32)\n'
             '  (call $farparams ' + " ".join(
                 ["(i32.add (local.get $n) (i32.const %d))" % i for i in range(11)] +
                 ["(f64.convert_i32_s (i32.add (local.get $n) (i32.const %d)))" % i
                  for i in range(10)]) + '))\n'
             ' (export "farcall" (func $farcall))')
made.append("farcall")
parts.append(")")

with open(os.path.join(OUT, "pressure.wat"), "w") as f:
    f.write("\n".join(parts) + "\n")
print(" ".join(made))
