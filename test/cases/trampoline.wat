;; A trampoline in GHC's shape, which tramp.c turns into tail calls: $run
;; calls through table 0 with type () -> i32 until a function returns 0,
;; each function returning the index of the next, and $sp, $hp and $r1 the
;; virtual registers only those functions touch.  The functions end every
;; way a body can: a return of a constant, of an index loaded from memory
;; as GHC's info tables hold them, a value left at the end, a branch to the
;; function's own label with it, 0 from inside a loop; one calls a helper
;; outside the table, one is entered from two places.  The first sets the
;; registers up, the last leaves them in memory, and a second run starts
;; from what the first wrote back to their globals.  And (i32, i32, i32) ->
;; i32, the rewritten functions' new shape, is also a C function pointer type
;; here; that a call through it that lands on one of them still traps is
;; test/cases_traps/tramptraps.wat's.

(module
  (type $stg (func (result i32)))
  (type $c3 (func (param i32 i32 i32) (result i32)))
  (memory 1)
  (global $sp (mut i32) (i32.const 0))
  (global $hp (mut i32) (i32.const 0))
  (global $r1 (mut i32) (i32.const 0))
  (global $steps (mut i32) (i32.const 0))
  (table 9 funcref)
  (elem (i32.const 1) $push $sum $loop $done $branchy $fall $mix3 $init)
  ;; the info table: at 64, the index a computed return loads
  (data (i32.const 64) "\03\00\00\00")

  ;; outside the table: a C helper the functions call
  (func $mix (param $x i32) (result i32)
    (i32.xor (i32.mul (local.get $x) (i32.const 31)) (i32.const 7)))

  ;; 1: pushes r1 onto the stack, allocates a word, returns a constant
  (func $push (type $stg)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 4)))
    (i32.store (global.get $sp) (global.get $r1))
    (i32.store (global.get $hp) (global.get $r1))
    (global.set $hp (i32.add (global.get $hp) (i32.const 4)))
    (global.set $r1 (call $mix (global.get $r1)))
    (if (i32.lt_u (global.get $sp) (i32.const 1000))
      (then (return (i32.const 5))))
    (return (i32.const 1)))

  ;; 2: sums the stack into r1, then a computed return through memory
  (func $sum (type $stg)
    (local $p i32)
    (local.set $p (global.get $sp))
    (global.set $r1 (i32.const 0))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $p) (i32.const 1024)))
        (global.set $r1 (i32.add (global.get $r1) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (br $l)))
    (global.set $sp (i32.const 1024))
    (return (i32.load (i32.const 64))))

  ;; 3: counts steps, returning from inside its loop
  (func $loop (type $stg)
    (loop $l
      (global.set $steps (i32.add (global.get $steps) (i32.const 1)))
      (if (i32.ge_u (global.get $steps) (i32.const 3))
        (then (return (i32.const 4))))
      (br $l))
    (i32.const 0))

  ;; 4: ends the run, leaving the registers in memory for the caller
  (func $done (type $stg)
    (global.set $r1 (i32.add (global.get $r1) (global.get $steps)))
    (i32.store (i32.const 128) (global.get $r1))
    (i32.store (i32.const 132) (global.get $sp))
    (i32.store (i32.const 136) (global.get $hp))
    (i32.const 0))

  ;; 5: a branch to its own label carries the index
  (func $branchy (type $stg)
    (global.set $hp (i32.add (global.get $hp) (i32.const 8)))
    (br 0 (i32.const 6)))

  ;; 6: the index is the value left at the end, computed
  (func $fall (type $stg)
    (i32.add (i32.const 2) (i32.and (global.get $r1) (i32.const 0))))

  ;; 7: a C function pointer's type: (i32, i32, i32) -> i32
  (func $mix3 (param i32 i32 i32) (result i32)
    (i32.add (local.get 0) (i32.mul (local.get 1) (local.get 2))))

  ;; 8: sets the registers up
  (func $init (type $stg)
    (global.set $sp (i32.const 1024))
    (global.set $hp (i32.const 2048))
    (global.set $r1 (i32.const 11))
    (global.set $steps (i32.const 0))
    (i32.const 1))

  (func $run (param $f i32) (result i32)
    (block $end
      (br_if $end (i32.eqz (local.get $f)))
      (loop $l
        (local.set $f (call_indirect (type $stg) (local.get $f)))
        (br_if $l (local.get $f))))
    (i32.const 0))

  (func $result (result i32)
    (i32.add (i32.mul (i32.load (i32.const 128)) (i32.const 1000003))
             (i32.add (i32.mul (i32.load (i32.const 132)) (i32.const 101))
                      (i32.load (i32.const 136)))))

  (func (export "trampolined") (result i32)
    (drop (call $run (i32.const 8)))
    (call $result))

  (func (export "resumed") (result i32)
    (drop (call $run (i32.const 8)))
    (drop (call $run (i32.const 2)))
    (call $result))

  (func (export "c_pointer") (result i32)
    (call_indirect (type $c3) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 7)))

)
