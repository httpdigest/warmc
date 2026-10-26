;; Calls of Go's that never return normally (opt.c nr_scan, nr_solve,
;; fold_flag_branches), in a module of Go's (a go:buildid section) whose
;; functions of Go's type, (i32) -> i32, return 0 to say they returned and 1
;; to say the stack unwinds.  Each export tests a call's flag, 100 for an
;; unwind and 200 for a return, so a function taken wrongly for one that
;; never returns normally gives 100 where it returned.
;;
;; Never return normally: $unwinds (a 1), $panics (a trap or a 1), $chain
;; (its 0 only past a call of $unwinds), and $loopa and $loopb, each of whose
;; 0 waits on the other's -- the greatest such set, which they recurse until
;; the stack runs out of.  May: $param_ret, whose 0 comes from a block's
;; result, one edge into its return; $unknown, which returns what it
;; computes; $even and $odd, whose recursion ends in a 0; and $tail, a tail
;; call.  The exports are of Go's type too, so gosp.c leaves the module, and
;; the flags, as they are (go_noret_sp.wat has them packed).
;;
;; stats -O1: go_noret_calls=7
;;
;; run: call_unwinds 0 => i32:100
;; run: call_panics 0 => i32:100
;; run: call_panics 1 => trap: unreachable
;; run: call_param 1 => i32:200
;; run: call_param 0 => i32:100
;; run: call_unknown 2 => i32:200
;; run: call_unknown 3 => i32:100
;; run: call_chain 0 => i32:100
;; run: call_even 4 => i32:200
;; run: call_even 5 => i32:200
;; run: call_tail 2 => i32:200
;; run: call_tail 3 => i32:100
;; run: call_loop 0 => trap: call stack exhausted
(module
  (@custom "go:buildid" "warmc-test")
  (type $go (func (param i32) (result i32)))

  (func $unwinds (type $go) (i32.const 1))
  (func $panics (type $go)
    (if (local.get 0) (then unreachable))
    (i32.const 1))
  (func $param_ret (type $go)
    (block $b (result i32)
      (drop (br_if $b (i32.const 0) (local.get 0)))
      (i32.const 1)))
  (func $unknown (type $go) (i32.and (local.get 0) (i32.const 1)))
  (func $chain (type $go)
    (if (call $unwinds (local.get 0)) (then (return (i32.const 1))))
    (i32.const 0))
  (func $even (type $go)
    (if (i32.eqz (local.get 0)) (then (return (i32.const 0))))
    (if (call $odd (i32.sub (local.get 0) (i32.const 1))) (then (return (i32.const 1))))
    (i32.const 0))
  (func $odd (type $go)
    (if (i32.eqz (local.get 0)) (then (return (i32.const 0))))
    (if (call $even (i32.sub (local.get 0) (i32.const 1))) (then (return (i32.const 1))))
    (i32.const 0))
  (func $loopa (type $go)
    (if (call $loopb (i32.add (local.get 0) (i32.const 1))) (then (return (i32.const 1))))
    (i32.const 0))
  (func $loopb (type $go)
    (if (call $loopa (i32.add (local.get 0) (i32.const 1))) (then (return (i32.const 1))))
    (i32.const 0))
  (func $tail (type $go) (return_call $unknown (local.get 0)))

  (func (export "call_unwinds") (type $go)
    (if (result i32) (call $unwinds (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_panics") (type $go)
    (if (result i32) (call $panics (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_param") (type $go)
    (if (result i32) (call $param_ret (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_unknown") (type $go)
    (if (result i32) (call $unknown (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_chain") (type $go)
    (if (result i32) (call $chain (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_even") (type $go)
    (if (result i32) (call $even (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_tail") (type $go)
    (if (result i32) (call $tail (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_loop") (type $go)
    (if (result i32) (call $loopa (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
)
