;; The traps of a trampoline tramp.c turns into tail calls.  $run calls
;; through table 0 with type () -> i32 until a function returns 0, and $sp
;; and $r1 are registers only those functions touch, so the functions take
;; them as arguments and tail-call the next themselves, through the table
;; when the index is computed.  Each call the loop would have made traps as
;; it did: on a slot holding a function of another type, an empty slot, one
;; past the table.  And a call through the type the functions now have the
;; shape of -- (i32, i32) -> i32, a C function pointer type here -- still
;; traps on them.  test/run_traps.sh runs each export.  The indices come
;; from memory, so no pass can fold them.
(module
  (type $stg (func (result i32)))
  (type $c2 (func (param i32 i32) (result i32)))
  (memory 1)
  (global $sp (mut i32) (i32.const 0))
  (global $r1 (mut i32) (i32.const 0))
  (table 8 funcref)
  (elem (i32.const 1) $start $next $done $add2)
  ;; the slot of an STG function, for the C pointer type's call
  (data (i32.const 8) "\01\00\00\00")

  ;; 1: sets the registers up, then goes where memory at 0 says
  (func $start (type $stg)
    (global.set $sp (i32.const 1024))
    (global.set $r1 (i32.const 5))
    (i32.load (i32.const 0)))

  ;; 2: works on them, then goes where memory at 4 says
  (func $next (type $stg)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 4)))
    (i32.store (global.get $sp) (global.get $r1))
    (global.set $r1 (i32.mul (global.get $r1) (i32.const 3)))
    (i32.load (i32.const 4)))

  ;; 3: ends the run
  (func $done (type $stg)
    (i32.store (i32.const 16) (global.get $r1))
    (i32.const 0))

  ;; 4: a C function
  (func $add2 (param i32 i32) (result i32) (i32.add (local.get 0) (local.get 1)))

  (func $run (param $f i32)
    (loop $l
      (local.set $f (call_indirect (type $stg) (local.get $f)))
      (br_if $l (local.get $f))))

  (func $go (param $first i32) (param $then i32)
    (i32.store (i32.const 0) (local.get $first))
    (i32.store (i32.const 4) (local.get $then))
    (call $run (i32.const 1)))

  (func (export "tramp_ok")
    (call $go (i32.const 2) (i32.const 3))
    (if (i32.ne (i32.load (i32.const 16)) (i32.const 15)) (then unreachable)))
  (func (export "tramp_x_type") (call $go (i32.const 2) (i32.const 4)))
  (func (export "tramp_x_null") (call $go (i32.const 5) (i32.const 3)))
  (func (export "tramp_x_oob")  (call $go (i32.const 2) (i32.const 8)))
  (func (export "tramp_x_far")  (call $go (i32.const 2) (i32.const 100000)))
  (func (export "tramp_c_on_stg") (result i32)
    (call_indirect (type $c2) (i32.const 1) (i32.const 2) (i32.load (i32.const 8))))
)
