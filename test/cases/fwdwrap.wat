;; Store-to-load forwarding where the load's i32 add is the smaller constant
;; (same_access_after): Go's callee, once inlined, loads an argument the
;; caller stored at sp + 8 from (sp - 8) + 16.  The two are one address
;; unless sp - 8 wraps, and an access at sp - 8 that ran -- the callee's
;; store of its return PC -- shows it did not.  Without one, or with one at
;; another address, the load must stay (test/cases_traps/traps.wat has
;; those at sp = 4, where they trap).
(module
  (memory 3)
  (global $sp (mut i32) (i32.const 1024))
  (global $v (mut i32) (i32.const 42))

  ;; the inlined call: argument, return PC, argument read back
  (func $call (param $sp i32) (result i32)
    (local $c i32)
    (i32.store offset=8 (local.get $sp) (global.get $v))
    (local.set $c (i32.sub (local.get $sp) (i32.const 8)))
    (i64.store (local.get $c) (i64.const 7))
    (i32.add (i32.load offset=16 (local.get $c)) (i32.const 1)))

  (func (export "go_arg") (result i32) (call $call (global.get $sp)))

  ;; no access at sp - 8 before the load
  (func $nowit (param $sp i32) (result i32)
    (i32.store offset=8 (local.get $sp) (global.get $v))
    (i32.load offset=16 (i32.sub (local.get $sp) (i32.const 8))))
  (func (export "no_witness") (result i32) (call $nowit (global.get $sp)))

  ;; an access at sp itself proves nothing about sp - 8
  (func $otherwit (param $sp i32) (result i32)
    (i32.store offset=8 (local.get $sp) (global.get $v))
    (i64.store (local.get $sp) (i64.const 7))
    (i32.load offset=16 (i32.sub (local.get $sp) (i32.const 8))))
  (func (export "other_witness") (result i32) (call $otherwit (global.get $sp)))

  ;; the witness after the store, a load of another size, a store the
  ;; later one overwrites, and the largest step taken (65535) and one past it
  (func (export "witness_between") (result i32)
    (local $s i32) (local $c i32)
    (local.set $s (global.get $sp))
    (local.set $c (i32.sub (local.get $s) (i32.const 24)))
    (i32.store8 (local.get $c) (i32.const 1))
    (i64.store offset=16 (local.get $s) (i64.const 0x1_0000_0005))
    (i32.add (i32.wrap_i64 (i64.load offset=40 (local.get $c)))
             (i32.load offset=40 (local.get $c))))
  (func (export "dead_store") (result i32)
    (local $s i32) (local $c i32)
    (local.set $s (global.get $sp))
    (i32.store offset=8 (local.get $s) (i32.const 1))
    (local.set $c (i32.sub (local.get $s) (i32.const 8)))
    (i32.store (local.get $c) (i32.const 3))
    (i32.store offset=16 (local.get $c) (i32.const 2))
    (i32.add (i32.load offset=8 (local.get $s)) (i32.load (local.get $c))))
  (func (export "step_65535") (result i32)
    (local $s i32) (local $c i32)
    (local.set $s (i32.add (global.get $sp) (i32.const 70000)))
    (i32.store offset=65535 (local.get $s) (global.get $v))
    (local.set $c (i32.sub (local.get $s) (i32.const 65535)))
    (i32.store8 (local.get $c) (i32.const 9))
    (i32.load offset=131070 (local.get $c)))
  (func (export "step_65536") (result i32)
    (local $s i32) (local $c i32)
    (local.set $s (i32.add (global.get $sp) (i32.const 70000)))
    (i32.store offset=65536 (local.get $s) (global.get $v))
    (local.set $c (i32.sub (local.get $s) (i32.const 65536)))
    (i32.store8 (local.get $c) (i32.const 9))
    (i32.load offset=131072 (local.get $c)))

  ;; a loop of inlined calls, each frame 16 below the last
  (func (export "frames") (result i32)
    (local $s i32) (local $c i32) (local $n i32) (local $acc i32)
    (local.set $s (i32.add (global.get $sp) (i32.const 4096)))
    (local.set $n (i32.const 100))
    (loop $l
      (i32.store offset=8 (local.get $s) (local.get $n))
      (local.set $c (i32.sub (local.get $s) (i32.const 8)))
      (i64.store (local.get $c) (i64.extend_i32_u (local.get $acc)))
      (local.set $acc (i32.add (local.get $acc)
                               (i32.mul (i32.load offset=16 (local.get $c))
                                        (i32.wrap_i64 (i64.load (local.get $c))))))
      (local.set $acc (i32.add (local.get $acc) (i32.const 1)))
      (local.set $s (i32.sub (local.get $s) (i32.const 16)))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (local.get $acc))
)
