;; go_noret.wat's calls with gosp.c's rewrite made: the functions of Go's
;; type are no export's, global 0 is the stack pointer, so each takes the
;; pointer as a second parameter and returns its flag in the high half of an
;; i64 (opt.c nr_ret_flag, nr_flag_call, flag_branch_call).  Never return
;; normally: $unwinds, $panics, $chain, $loopa and $loopb; may: $param_ret,
;; $unknown, $even and $odd.  Each export tests a call's flag, 100 for an
;; unwind and 200 for a return; their second parameter is not read.
;;
;; stats -O1: gosp_funcs=10 go_noret_calls=7
;;
;; run: call_unwinds 0 0 => i32:100
;; run: call_panics 0 0 => i32:100
;; run: call_panics 1 0 => trap: unreachable
;; run: call_param 1 0 => i32:200
;; run: call_param 0 0 => i32:100
;; run: call_unknown 2 0 => i32:200
;; run: call_unknown 3 0 => i32:100
;; run: call_chain 0 0 => i32:100
;; run: call_even 4 0 => i32:200
;; run: call_even 5 0 => i32:200
;; run: call_loop 0 0 => trap: call stack exhausted
(module
  (@custom "go:buildid" "warmc-test")
  (type $go (func (param i32) (result i32)))
  (type $ex (func (param i32 i32) (result i32)))
  (global $sp (mut i32) (i32.const 65536))
  (memory 1)

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
  (func $call (type $go) (local.get 0))

  (func (export "call_unwinds") (type $ex)
    (if (result i32) (call $unwinds (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_panics") (type $ex)
    (if (result i32) (call $panics (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_param") (type $ex)
    (if (result i32) (call $param_ret (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_unknown") (type $ex)
    (if (result i32) (call $unknown (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_chain") (type $ex)
    (if (result i32) (call $chain (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_even") (type $ex)
    (if (result i32) (call $even (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
  (func (export "call_loop") (type $ex)
    (if (result i32) (call $loopa (local.get 0)) (then (i32.const 100)) (else (i32.const 200))))
)
