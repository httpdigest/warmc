;; -O3's addr-nowrap (opt.c pass_addr_nowrap): an access at x + c, x the C
;; stack pointer or an address made from it, c a positive constant under
;; 64 KiB with the offset it adds to, is the access at x with c in its
;; offset, as if the 32-bit add did not wrap.  An out-param an inlined
;; callee writes through the pointer its caller handed it, sp + 16, is then
;; the slot the caller reads at sp offset 16, which the memory pass forwards
;; (outparam, and chain's two adds).  Left as they are: an address made from
;; a parameter (heap) or another global (other), a negative step (below).
;; The module's stack pointer is $sp, which its functions take frames from.
;; An address that wraps reads near the bottom of memory below -O3 and traps
;; past the top at -O3 (wrapframe).  The memory pass takes any x + c at
;; offset k for x at offset k + c at -O3, with no access at x before to show
;; that the add did not wrap: fwd's load is the value its store wrote, where
;; wasm's wraps round and the load traps -- not past 64 KiB (fwdfar).
;;
;; stats -O2: addr_nowrap=0
;; stats -O3: addr_nowrap=5
;;
;; run: outparam 5 => i32:11
;; run: outparam -3 => i32:-5
;; run: chain 7 => i32:14
;; run: heap 0 => i32:2
;; run: below 0 => i32:258
;; run: wrapframe => i32:1
;; run -O3: wrapframe => trap: out of bounds memory access
;; run: other => i32:5
;; run: fwd 0 9 => i32:9
;; run: fwd 0xfffffff8 9 => trap: out of bounds memory access
;; run -O3: fwd 0xfffffff8 9 => i32:9
;; run: fwdfar 0xffff0000 9 => trap: out of bounds memory access

(module
  (memory 2)
  (global $sp (mut i32) (i32.const 0x10000))
  ;; a global something else sets, so that no pass takes it for a constant
  (global $hb (mut i32) (i32.const 0x100))
  ;; the stack pointer wrapframe sets, eight bytes below the top of the
  ;; 32-bit index space, read from memory so that nothing folds it
  (data (i32.const 0) "\f8\ff\ff\ff\02\00\00\00\03\00\00\00\04\00\00\00\01\00\00\00")
  (data (i32.const 0xfff0) "\02\01\00\00")
  (data (i32.const 0x104) "\05\00\00\00")
  ;; writes a and a + 1 through p
  (func $put (param $p i32) (param $a i32)
    (i32.store (local.get $p) (local.get $a))
    (i32.store offset=4 (local.get $p) (i32.add (local.get $a) (i32.const 1))))
  (func $outparam (export "outparam") (param $a i32) (result i32)
    (local $f i32) (local $r i32)
    (global.set $sp (local.tee $f (i32.sub (global.get $sp) (i32.const 32))))
    (call $put (i32.add (local.get $f) (i32.const 16)) (local.get $a))
    (local.set $r (i32.add (i32.load offset=16 (local.get $f)) (i32.load offset=20 (local.get $f))))
    (global.set $sp (i32.add (local.get $f) (i32.const 32)))
    (local.get $r))
  ;; a slot at (sp + 8) + 4, stored and read back
  (func $chain (export "chain") (param $a i32) (result i32)
    (local $f i32) (local $r i32)
    (global.set $sp (local.tee $f (i32.sub (global.get $sp) (i32.const 32))))
    (i32.store (i32.add (i32.add (local.get $f) (i32.const 8)) (i32.const 4)) (local.get $a))
    (local.set $r (i32.add (i32.load offset=12 (local.get $f)) (local.get $a)))
    (global.set $sp (i32.add (local.get $f) (i32.const 32)))
    (local.get $r))
  (func $heap (export "heap") (param $p i32) (result i32)
    (i32.load (i32.add (local.get $p) (i32.const 4))))
  (func $sethb (export "sethb") (param $v i32) (global.set $hb (local.get $v)))
  (func $other (export "other") (result i32)
    (i32.load (i32.add (global.get $hb) (i32.const 4))))
  ;; a store at p + 16 and a load at p offset 16
  (func $fwd (export "fwd") (param $p i32) (param $a i32) (result i32)
    (i32.store (i32.add (local.get $p) (i32.const 16)) (local.get $a))
    (i32.load offset=16 (local.get $p)))
  (func $fwdfar (export "fwdfar") (param $p i32) (param $a i32) (result i32)
    (i32.store (i32.add (local.get $p) (i32.const 0x10000)) (local.get $a))
    (i32.load offset=0x10000 (local.get $p)))
  ;; the word 16 bytes below the stack pointer, 0xfff0
  (func $below (export "below") (result i32)
    (i32.load (i32.add (global.get $sp) (i32.const -16))))
  ;; the stack pointer at 2^32 - 8 and a word 24 bytes above it: 16 in
  ;; wasm, past the top at -O3
  (func $wrapframe (export "wrapframe") (result i32)
    (local $old i32) (local $r i32)
    (local.set $old (global.get $sp))
    (global.set $sp (i32.load (i32.const 0)))
    (local.set $r (i32.load (i32.add (global.get $sp) (i32.const 24))))
    (global.set $sp (local.get $old))
    (local.get $r))
)
