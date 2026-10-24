;; A store that a later one overwrites is dead only if nothing between can
;; trap (memopt, opt.c): each export stores 1 to 16, does what traps for
;; the argument -- a load or store out of bounds, a trapping truncation --
;; and stores 2.  After the trap, 16 holds 1.  wload's load may read 16, so
;; it keeps the store anyway; wfar's, from another memory, cannot.  wset and
;; wget trap on a null struct for 0, in the branches memopt has for a GC
;; field, ahead of the rest.  wstore8's store traps itself: on AArch64 a
;; wider one has its check before it with -fexact-trap, which keeps the
;; store as the load's trap does, but nothing tests one byte, and x64 tests
;; nothing.
;; run: wstore:70000 r => trap 1
;; run: wload:70000 r => trap 1
;; run: wfar:70000 r => trap 1
;; run: wfar:20 r => 0 2
;; run: wtrunc:0x7fc00000 r => trap 1
;; run: wstore:20 r => 0 2
;; run: wstore8:70000 r => trap 1
;; run: wset:0 r => trap 1
;; run: wget:0 r => trap 1
;; run: wset:1 wget:1 r => 0 0 2
(module
  (memory (export "memory") 1 1)
  (memory $other 1 1)
  (type $s (struct (field (mut i32))))
  (func $obj (param $x i32) (result (ref null $s))
    (if (result (ref null $s)) (local.get $x)
      (then (struct.new $s (i32.const 0)))
      (else (ref.null $s))))
  (func (export "wset") (param $x i32) (result i32)
    (local $r (ref null $s))
    (local.set $r (call $obj (local.get $x)))
    (i32.store (i32.const 16) (i32.const 1))
    (struct.set $s 0 (local.get $r) (i32.const 7))
    (i32.store (i32.const 16) (i32.const 2))
    (i32.const 0))
  (func (export "wget") (param $x i32) (result i32)
    (local $r (ref null $s)) (local $v i32)
    (local.set $r (call $obj (local.get $x)))
    (i32.store (i32.const 16) (i32.const 1))
    (local.set $v (struct.get $s 0 (local.get $r)))
    (i32.store (i32.const 16) (i32.const 2))
    (local.get $v))
  (func (export "wload") (param $p i32) (result i32)
    (local $x i32)
    (i32.store (i32.const 16) (i32.const 1))
    (local.set $x (i32.load (local.get $p)))
    (i32.store (i32.const 16) (i32.const 2))
    (local.get $x))
  (func (export "wfar") (param $p i32) (result i32)
    (local $x i32)
    (i32.store (i32.const 16) (i32.const 1))
    (local.set $x (i32.load $other (local.get $p)))
    (i32.store (i32.const 16) (i32.const 2))
    (local.get $x))
  (func (export "wstore8") (param $p i32) (result i32)
    (i32.store (i32.const 16) (i32.const 1))
    (i32.store8 (local.get $p) (i32.const 5))
    (i32.store (i32.const 16) (i32.const 2))
    (i32.const 0))
  (func (export "wstore") (param $p i32) (result i32)
    (i32.store (i32.const 16) (i32.const 1))
    (i32.store (local.get $p) (i32.const 5))
    (i32.store (i32.const 16) (i32.const 2))
    (i32.const 0))
  (func (export "wtrunc") (param $p i32) (result i32)
    (local $x i32)
    (i32.store (i32.const 16) (i32.const 1))
    (local.set $x (i32.trunc_f32_s (f32.reinterpret_i32 (local.get $p))))
    (i32.store (i32.const 16) (local.get $x))
    (local.get $x))
  (func (export "r") (result i32) (i32.load (i32.const 16)))
)
