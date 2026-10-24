;; Function references across core instances: one taken in A and tested,
;; called or put in a table by B is of the type A gave it -- ref.test on one
;; returned by A is 1, as it is in A.
(component
  (core module $A
    (type $ft (func (param i32) (result i32)))
    (func $dbl (type $ft) (i32.mul (local.get 0) (i32.const 2)))
    (func $tri (type $ft) (i32.mul (local.get 0) (i32.const 3)))
    (table (export "t") 1 funcref)
    (elem (table 0) (i32.const 0) func $dbl)
    (elem declare func $dbl $tri)
    (func (export "fn") (result funcref) (ref.func $dbl))
    (func (export "fn3") (result funcref) (ref.func $tri))
    (func (export "fnt") (result funcref) (table.get 0 (i32.const 0)))
    (func (export "selftest") (result i32) (ref.test (ref $ft) (ref.func $tri))))
  (core instance $a (instantiate $A))
  (core module $B
    (type $ft (func (param i32) (result i32)))
    (import "a" "fn" (func $fn (result funcref)))
    (import "a" "fn3" (func $fn3 (result funcref)))
    (import "a" "fnt" (func $fnt (result funcref)))
    (func (export "t1") (result i32) (ref.test (ref $ft) (call $fn)))
    (func (export "t3") (result i32) (ref.test (ref $ft) (call $fn3)))
    (func (export "tt") (result i32) (ref.test (ref $ft) (call $fnt))))
  (core instance $b (instantiate $B (with "a" (instance $a))))
  (func (export "t1") (result u32) (canon lift (core func $b "t1")))
  (func (export "t3") (result u32) (canon lift (core func $b "t3")))
  (func (export "tt") (result u32) (canon lift (core func $b "tt")))
  (func (export "selftest") (result u32) (canon lift (core func $a "selftest"))))
