;; A library in GHC's shape, whose trampoline tramp.c turns into tail
;; calls, and whose import the host (test/shared/tramphost.c) answers by
;; calling back into it: $a is inside the import while the host runs
;; another trampoline through tr_run, from $c.  An import counts as running
;; the loop again because an export does: $a stores its register into the
;; global before the call and loads it again after, so the run inside sees
;; what $a made of it and $a goes on with what that run left.
(module
  (type $stg (func (result i32)))
  (type $c1 (func (param i32)))
  (import "env" "back" (func $back (param i32)))
  (memory 1)
  (global $r1 (mut i32) (i32.const 0))
  (table 8 funcref)
  (elem (i32.const 1) $a $b $c $show)

  (func $a (type $stg)
    (global.set $r1 (i32.add (global.get $r1) (i32.const 3)))
    (call $back (i32.const 3))
    (i32.const 2))
  (func $b (type $stg)
    (global.set $r1 (i32.mul (global.get $r1) (i32.const 5)))
    (i32.const 4))
  (func $c (type $stg)
    (global.set $r1 (i32.add (global.get $r1) (i32.const 100)))
    (i32.const 0))
  (func $show (type $stg)
    (i32.store (i32.const 0) (global.get $r1))
    (i32.const 0))

  (func $run (type $c1) (param $f i32)
    (loop $l
      (local.set $f (call_indirect (type $stg) (local.get $f)))
      (br_if $l (local.get $f))))

  (func (export "tr_run") (param i32) (call $run (local.get 0)))
  ;; two runs from $a, each with one from $c inside:
  ;; (3 + 100) * 5 = 515, then (515 + 3 + 100) * 5 = 3090
  (func (export "tr_go") (result i32)
    (call $run (i32.const 1))
    (call $run (i32.const 1))
    (i32.load (i32.const 0))))
