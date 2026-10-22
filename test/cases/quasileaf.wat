;; Functions that never return but through a tail call, with a runtime call
;; partway.
;;
;; A function whose only calls are panics -- `call $panic; unreachable` --
;; keeps the instance and the memory base in caller-saved x9 and x10, since
;; nothing after the panic needs them.  warmc used to recognise such a call as
;; one at the end of a block that never reaches a return; but a block that
;; ends in a tail call never reaches one either, so every function here
;; qualified, kept the instance in x9 across data.drop, memory.grow or
;; table.get, and then read a global through the clobbered register (wasm-smith
;; found it).  Each test function ends in a tail call to $pong, and $ping and
;; $pong call each other, so neither can be inlined away or turned into a loop
;; of its own.
(module
  (memory 1)
  (data $d "abc")
  (table $t 2 funcref)
  (elem declare func $pong)
  (global $g (mut i32) (i32.const 7))
  (global $k (mut i64) (i64.const 1000))

  (func $pong (param $n i32) (param $acc i32) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $ping (i32.sub (local.get $n) (i32.const 1))
      (i32.add (local.get $acc) (global.get $g))))

  ;; data.drop in a block of its own, then a global and memory read
  (func $ping (param $n i32) (param $acc i32) (result i32)
    (if (i32.and (local.get $n) (i32.const 1)) (then (data.drop $d)))
    (global.set $g (i32.add (global.get $g) (i32.const 3)))
    (i32.store (i32.const 16) (i32.add (i32.load (i32.const 16)) (local.get $n)))
    (return_call $pong (local.get $n)
      (i32.add (local.get $acc) (i32.load (i32.const 16)))))

  ;; memory.grow's result, then a global read and a store through the base
  (func $grow (param $x i32) (result i32)
    (local $r i32)
    (if (local.get $x) (then (local.set $r (memory.grow (i32.const 0)))))
    (i32.store (i32.const 32) (i32.add (local.get $r) (global.get $g)))
    (global.set $k (i64.add (global.get $k) (i64.extend_i32_u (local.get $r))))
    (return_call $pong (i32.const 5) (i32.load (i32.const 32))))

  ;; table.get, then the instance for the global and the null test
  (func $tget (param $x i32) (result i32)
    (local $f funcref)
    (if (local.get $x) (then (local.set $f (table.get $t (i32.const 1)))))
    (global.set $g (i32.add (global.get $g) (ref.is_null (local.get $f))))
    (return_call $pong (i32.const 3) (global.get $g)))

  (func (export "ping_even") (result i32) (call $ping (i32.const 6) (i32.const 0)))
  (func (export "ping_odd") (result i32) (call $ping (i32.const 9) (i32.const 1)))
  (func (export "grow_yes") (result i32) (call $grow (i32.const 1)))
  (func (export "grow_no") (result i32) (call $grow (i32.const 0)))
  (func (export "tget_yes") (result i32)
    (table.set $t (i32.const 1) (ref.func $pong))
    (call $tget (i32.const 1)))
  (func (export "tget_no") (result i32) (call $tget (i32.const 0)))
  (func (export "k") (result i64) (global.get $k)))
