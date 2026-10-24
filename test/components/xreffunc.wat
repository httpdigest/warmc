;; A function of one core instance taken by ref.func and handed to another,
;; which calls it: $dbl reads nothing of its instance but the reference
;; itself, and that is its own instance's cell, not the caller's -- a leaf
;; that took it from the caller's instance pointer handed out a funcref into
;; the caller's import area, and the call through it crashed.
(component
  (core module $A
    (type $ft (func (param i32) (result i32)))
    (func $dbl (type $ft) (i32.mul (local.get 0) (i32.const 2)))
    (elem declare func $dbl)
    (func (export "fn") (result (ref $ft)) (ref.func $dbl)))
  (core instance $a (instantiate $A))
  (core module $B
    (type $ft (func (param i32) (result i32)))
    (type $vt (func (result i32)))
    (import "a" "fn" (func $fn (result (ref $ft))))
    (global $x (mut i32) (i32.const 0))
    (func $other (type $vt) (i32.const 777))
    (elem declare func $other)
    (func (export "run") (param i32) (result i32) (call_ref $ft (local.get 0) (call $fn))))
  (core instance $b (instantiate $B (with "a" (instance $a))))
  (func (export "run") (param "x" u32) (result u32) (canon lift (core func $b "run"))))
