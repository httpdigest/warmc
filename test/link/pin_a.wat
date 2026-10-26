;; The defining side of the pinned-register pair; see pin_b.wat.  A's memory
;; reads 10 at address 0 and its global holds 1000, where B's read 20 and
;; 2000, so a function that runs with the other instance's x19 or x20 says so
;; in its result.
(module
  (type $wide (func (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (type $cb (func (param i32) (result i32)))
  (memory (export "mem") 1)
  (data (i32.const 0) "\0a")
  (global $g (mut i32) (i32.const 1000))
  (tag $t (export "t") (param i32))
  (elem declare func $down $given)

  ;; Three functions whose only way out of A is a reference: a global's
  ;; initialiser, a table's, a ref.func an export returns.  Each needs the
  ;; entry check all the same, or B's call enters it with B's registers.
  (func $by_global (type $cb) (i32.add (local.get 0) (i32.load8_u (i32.const 0))))
  (func $by_table (type $cb) (i32.add (local.get 0) (global.get $g)))
  (func $given (type $cb) (i32.mul (local.get 0) (i32.load8_u (i32.const 0))))
  (global (export "gref") (ref $cb) (ref.func $by_global))
  (table (export "tref") 1 1 (ref $cb) (ref.func $by_table))
  (func (export "give") (result (ref $cb)) (ref.func $given))

  ;; Ten integer parameters, the last three past x7 on the stack, each
  ;; weighted by its position, plus A's memory and global: 385 + 10 + 1000
  ;; for 1 to 10.
  (func $wide (export "wide") (type $wide)
    (i32.add (i32.add (i32.add (i32.add (local.get 0) (i32.mul (local.get 1) (i32.const 2))) (i32.add (i32.mul (local.get 2) (i32.const 3)) (i32.mul (local.get 3) (i32.const 4)))) (i32.add (i32.add (i32.mul (local.get 4) (i32.const 5)) (i32.mul (local.get 5) (i32.const 6))) (i32.add (i32.mul (local.get 6) (i32.const 7)) (i32.mul (local.get 7) (i32.const 8))))) (i32.add (i32.add (i32.mul (local.get 8) (i32.const 9)) (i32.mul (local.get 9) (i32.const 10))) (i32.add (i32.load8_u (i32.const 0)) (global.get $g)))))

  ;; Floats past v7 and integers past x7 at once: four stack slots.
  (func (export "fwide") (param f64 f64 f64 f64 f64 f64 f64 f64 f64 f64 i32 i32 i32 i32 i32 i32 i32 i32 i32) (result f64)
    (f64.add (f64.add (f64.add (f64.add (f64.mul (local.get 0) (f64.const 1)) (f64.mul (local.get 1) (f64.const 2))) (f64.add (f64.mul (local.get 2) (f64.const 3)) (f64.mul (local.get 3) (f64.const 4)))) (f64.add (f64.add (f64.mul (local.get 4) (f64.const 5)) (f64.mul (local.get 5) (f64.const 6))) (f64.add (f64.mul (local.get 6) (f64.const 7)) (f64.mul (local.get 7) (f64.const 8))))) (f64.add (f64.add (f64.mul (local.get 8) (f64.const 9)) (f64.mul (local.get 9) (f64.const 10))) (f64.convert_i32_s (i32.add (i32.add (i32.add (i32.add (i32.mul (local.get 10) (i32.const 1)) (i32.mul (local.get 11) (i32.const 2))) (i32.add (i32.mul (local.get 12) (i32.const 3)) (i32.mul (local.get 13) (i32.const 4)))) (i32.add (i32.add (i32.mul (local.get 14) (i32.const 5)) (i32.mul (local.get 15) (i32.const 6))) (i32.add (i32.mul (local.get 16) (i32.const 7)) (i32.mul (local.get 17) (i32.const 8))))) (i32.add (i32.mul (local.get 18) (i32.const 9)) (i32.load8_u (i32.const 0))))))))

  ;; More results than registers, through the area x8 points at, which the
  ;; caller's frame holds.
  (func (export "multi") (param $x i32) (result i32 i64 f64 i32 i32 i32 i32 i32 i32 i32)
    (local.get $x)
    (i64.mul (i64.extend_i32_u (local.get $x)) (i64.const 2))
    (f64.mul (f64.convert_i32_u (local.get $x)) (f64.const 3))
    (i32.add (local.get $x) (i32.const 1))
    (i32.add (local.get $x) (i32.const 2))
    (i32.add (local.get $x) (i32.const 3))
    (i32.add (local.get $x) (i32.const 4))
    (i32.add (local.get $x) (i32.const 5))
    (i32.add (local.get $x) (i32.const 6))
    (i32.load8_u (i32.const 0)))

  (func (export "raise") (param i32)
    (throw $t (i32.add (local.get 0) (i32.load8_u (i32.const 0)))))

  ;; B's callback, entered from here: a crossing the other way.  A's memory
  ;; is read after it comes back.
  (func (export "bounce") (param $f (ref $cb)) (param $n i32) (result i32)
    (i32.add (call_ref $cb (local.get $n) (local.get $f))
             (i32.load8_u (i32.const 0))))

  ;; A million tail calls through a reference to a function of A's own, from
  ;; A's own code: the entry check passes them straight into the body, so the
  ;; stack does not grow.
  (func $down (type $cb)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.load8_u (i32.const 0)))
      (else (return_call_ref $cb (i32.sub (local.get 0) (i32.const 1)) (ref.func $down)))))
  (func (export "deep") (param i32) (result i32)
    (return_call $down (local.get 0)))

  ;; A function of no parameters, entered from B, that tail-calls one with
  ;; three stack arguments: they go where its own would have arrived, in
  ;; the area warmc_xenter made for it, which has to be room enough.
  (func $own_wide (type $wide)
    (i32.add (local.get 9) (i32.add (local.get 8) (local.get 7))))
  (func (export "tail_more") (result i32)
    (return_call $own_wide (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
      (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10)))
)
