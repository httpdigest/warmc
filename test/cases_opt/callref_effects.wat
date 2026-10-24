;; Effects of a call_ref (opt.c ref_effects): a call through a function
;; reference can do what any function a reference is taken of can -- here
;; one a global holds, which writes memory, and one that writes a global --
;; not only what table 0's entries can, as for a call_indirect through it.
;; Table 0 holds only $nop, which writes nothing; taking that for every
;; call_ref kept a global across one (memopt), hoisted a load out of a loop
;; with one (LICM), and kept a global in a register through one (gloop).
;; run: f => i32:11
;; run: l 5 => i32:10
;; run: gl 5 => i32:505
(module
  (type $t (func))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $nop)
  (elem declare func $writer $gwriter)
  (global $g (mut (ref null $t)) (ref.func $gwriter))
  (global $h (mut (ref null $t)) (ref.func $writer))
  (global $x (mut i32) (i32.const 0))
  (func $nop)
  (func $writer (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (i32.const 1))))
  (func $gwriter (global.set $x (i32.add (global.get $x) (i32.const 1))))
  ;; global read across call_ref
  (func (export "f") (result i32)
    (local $a i32)
    (global.set $x (i32.const 10))
    (call_ref $t (global.get $g))
    (global.get $x))
  ;; load in a loop with call_ref: LICM
  (func (export "l") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (i32.store (i32.const 0) (i32.const 0))
    (loop $lp
      (local.set $s (i32.add (local.get $s) (i32.load (i32.const 0))))
      (call_ref $t (global.get $h))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $lp (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  ;; global in a loop with call_ref: gloop
  (func (export "gl") (param $n i32) (result i32)
    (local $i i32)
    (global.set $x (i32.const 0))
    (loop $lp
      (global.set $x (i32.add (global.get $x) (i32.const 100)))
      (call_ref $t (global.get $g))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $lp (i32.lt_u (local.get $i) (local.get $n))))
    (global.get $x))
)
