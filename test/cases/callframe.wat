;; What a call costs where it is not made.  Four rules in the arm64 back end
;; and one in the optimiser, each with the functions that must take it and
;; the ones that must not:
;;
;;   * A quasi-leaf, whose every call is a panic's -- `call; unreachable` --
;;     is compiled as a leaf and makes each such call in a frame of its own
;;     (cold_calls_only in src/isel.c): its own frame torn down first, the
;;     callee-saved registers its caller's again, a frame record pushed.  An
;;     exception thrown from that call unwinds through the record into a
;;     catcher that keeps values in callee-saved registers (cold_*), so a
;;     teardown that left the quasi-leaf's own values in them, or a record
;;     the unwinder cannot read, shows as a wrong sum.  $sum14 holds fourteen
;;     values at once, more than the caller-saved registers, and calls its
;;     panics directly and through the table; $raise_tail's tail call writes
;;     ten arguments where its own arrived, the cold frame's outgoing area,
;;     which must not reach the record above it.
;;   * An early return tested ahead of the prologue (early_ret_shape): the
;;     entry block tests parameters and branches to a return that calls
;;     nothing.  $walk sums a tree, `if (n == 0) return 0`; the others test
;;     eqz, a parameter itself, a compare with a constant, a negative one
;;     (cmn), with another parameter, i64, and return a parameter, nothing,
;;     or acc + n through the return block's own parameters ($fib).  Those
;;     that must keep the test after the prologue: one that loads before it,
;;     one whose return block stores, one whose result is a float, and one
;;     that tests its ninth parameter, which arrives on the stack.
;;   * A zero crossing calls takes no callee-saved register and is read as
;;     the zero register ($zeros: an argument, a stored value, the result).
;;   * A global written twice in a block with nothing between that reads it,
;;     calls or can trap keeps only the second write (global_dse in
;;     src/opt.c); one read between ($gdse_get) or a call that reads it
;;     ($gdse_call) keeps both.
(module
  (type $t1 (func (param i32) (result i32)))
  (type $t2 (func (param i32 i32) (result i32)))
  (type $tv (func (param i32)))
  (tag $boom (param i32))
  (memory 1)
  (table 8 funcref)
  (elem (i32.const 0) func $sum14 $raise $quasi_ind $walk $early_mix)
  (global $slot_sum14 (mut i32) (i32.const 0))
  (global $slot_raise (mut i32) (i32.const 1))
  (global $slot_quasi (mut i32) (i32.const 2))
  (global $slot_mix (mut i32) (i32.const 4))
  (global $g (mut i32) (i32.const 100))
  (global $seen (mut i32) (i32.const 0))

  ;; recursive, so that it is not inlined into the panic paths
  (func $raise (param $x i32) (result i32)
    (if (i32.gt_u (local.get $x) (i32.const 0x7fffff00))
      (then (return (call $raise (i32.shr_u (local.get $x) (i32.const 1))))))
    (throw $boom (i32.mul (local.get $x) (i32.const 3))))

  (func $raise10 (param $a i32) (param $b i32) (param $c i32) (param $d i32)
                 (param $e i32) (param $f i32) (param $g i32) (param $h i32)
                 (param $i i32) (param $j i32) (result i32)
    (throw $boom
      (i32.add (i32.add (i32.add (local.get $a) (i32.mul (local.get $b) (i32.const 3)))
                        (i32.add (i32.mul (local.get $c) (i32.const 5)) (local.get $d)))
               (i32.add (i32.add (i32.mul (local.get $e) (i32.const 7)) (local.get $f))
                        (i32.add (i32.add (i32.mul (local.get $g) (i32.const 11)) (local.get $h))
                                 (i32.add (i32.mul (local.get $i) (i32.const 13))
                                          (i32.mul (local.get $j) (i32.const 17))))))))

  (func $raise_tail (param $x i32) (result i32)
    (return_call $raise10 (local.get $x) (i32.add (local.get $x) (i32.const 1))
      (i32.add (local.get $x) (i32.const 2)) (i32.add (local.get $x) (i32.const 3))
      (i32.add (local.get $x) (i32.const 4)) (i32.add (local.get $x) (i32.const 5))
      (i32.add (local.get $x) (i32.const 6)) (i32.add (local.get $x) (i32.const 7))
      (i32.add (local.get $x) (i32.const 8)) (i32.add (local.get $x) (i32.const 9))))

  ;; fourteen values live at once, two cold calls and one through the table
  (func $sum14 (param $a i32) (param $b i32) (result i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32)
    (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32)
    (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32)
    (local.set $v0 (i32.mul (local.get $a) (i32.const 3)))
    (local.set $v1 (i32.xor (local.get $b) (i32.const 0x55)))
    (local.set $v2 (i32.add (local.get $a) (local.get $b)))
    (local.set $v3 (i32.mul (local.get $b) (i32.const 7)))
    (local.set $v4 (i32.sub (local.get $a) (i32.const 9)))
    (local.set $v5 (i32.rotl (local.get $a) (i32.const 5)))
    (local.set $v6 (i32.mul (local.get $v2) (local.get $v1)))
    (local.set $v7 (i32.shr_u (local.get $b) (i32.const 2)))
    (local.set $v8 (i32.or (local.get $a) (i32.const 0x100)))
    (local.set $v9 (i32.mul (local.get $v0) (local.get $v3)))
    (local.set $v10 (i32.xor (local.get $v4) (local.get $v5)))
    (local.set $v11 (i32.add (local.get $v6) (i32.const 1234)))
    (local.set $v12 (i32.mul (local.get $v7) (local.get $v8)))
    (local.set $v13 (i32.sub (local.get $v9) (local.get $v10)))
    (if (i32.gt_u (local.get $b) (i32.const 1000))
      (then (drop (call $raise_tail (local.get $a))) (unreachable)))
    (if (i32.eq (local.get $a) (i32.const 77))
      (then (drop (call $raise (i32.add (local.get $v13) (local.get $v2)))) (unreachable)))
    (if (i32.eq (local.get $a) (i32.const 78))
      (then (drop (call_indirect (type $t1) (local.get $v11) (global.get $slot_raise)))
            (unreachable)))
    (i32.add
      (i32.add (i32.add (i32.add (local.get $v0) (local.get $v1))
                        (i32.mul (local.get $v2) (local.get $v3)))
               (i32.add (i32.xor (local.get $v4) (local.get $v5))
                        (i32.mul (local.get $v6) (local.get $v7))))
      (i32.add (i32.add (i32.xor (local.get $v8) (local.get $v9))
                        (i32.mul (local.get $v10) (local.get $v11)))
               (i32.add (local.get $v12) (local.get $v13)))))

  ;; a quasi-leaf that touches memory, its cold call a direct one
  (func $quasi_ind (param $p i32) (result i32)
    (if (i32.ge_u (local.get $p) (i32.const 4096))
      (then (drop (call $raise (local.get $p))) (unreachable)))
    (i32.add (i32.load (local.get $p)) (i32.const 1)))

  ;; The catcher: values live across the call in callee-saved registers, and
  ;; read again once the exception has unwound back here.
  (func $catch (param $a i32) (param $b i32) (result i32)
    (local $k0 i32) (local $k1 i32) (local $k2 i32) (local $k3 i32) (local $k4 i32)
    (local.set $k0 (i32.mul (local.get $a) (i32.const 1000003)))
    (local.set $k1 (i32.xor (local.get $b) (i32.const 0x1234567)))
    (local.set $k2 (i32.add (local.get $a) (i32.const 0x777)))
    (local.set $k3 (i32.rotl (local.get $b) (i32.const 9)))
    (local.set $k4 (i32.mul (local.get $k0) (local.get $k1)))
    (i32.add
      (block $h (result i32)
        (try_table (catch $boom $h)
          (return (i32.add
            (call_indirect (type $t2) (local.get $a) (local.get $b) (global.get $slot_sum14))
            (i32.add (i32.add (local.get $k0) (local.get $k1))
                     (i32.add (i32.xor (local.get $k2) (local.get $k3)) (local.get $k4))))))
        (i32.const -1))
      (i32.add (i32.add (local.get $k0) (i32.mul (local.get $k1) (i32.const 3)))
               (i32.add (i32.xor (local.get $k2) (local.get $k3)) (local.get $k4)))))

  (func $catch_q (param $p i32) (result i32)
    (local $k0 i32) (local $k1 i32)
    (local.set $k0 (i32.mul (local.get $p) (i32.const 31)))
    (local.set $k1 (i32.xor (local.get $p) (i32.const 0xabcdef)))
    (i32.add
      (block $h (result i32)
        (try_table (catch $boom $h)
          (return (i32.add (call_indirect (type $t1) (local.get $p) (global.get $slot_quasi))
                           (i32.mul (local.get $k0) (local.get $k1)))))
        (i32.const -1))
      (i32.sub (local.get $k0) (local.get $k1))))

  (func (export "cold_plain") (result i32) (call $catch (i32.const 5) (i32.const 6)))
  (func (export "cold_direct") (result i32) (call $catch (i32.const 77) (i32.const 6)))
  (func (export "cold_indirect") (result i32) (call $catch (i32.const 78) (i32.const 40)))
  (func (export "cold_tail") (result i32) (call $catch (i32.const 12) (i32.const 5000)))
  (func (export "cold_mem_ok") (result i32)
    (i32.store (i32.const 64) (i32.const 0x4242))
    (call $catch_q (i32.const 64)))
  (func (export "cold_mem_raise") (result i32) (call $catch_q (i32.const 9000)))
  (func (export "cold_loop") (result i32)
    (local $i i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (i32.const 90)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (call $catch (local.get $i) (i32.mul (local.get $i) (i32.const 23)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; ---- early returns ----

  ;; nodes at 1024 + 16k: left, right, value
  (func $walk (param $n i32) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (i32.const 0))))
    (i32.add (i32.load offset=8 (local.get $n))
      (i32.add (call $walk (i32.load (local.get $n)))
               (call $walk (i32.load offset=4 (local.get $n))))))

  (func $build (param $k i32) (param $depth i32) (result i32)
    (local $n i32)
    (if (i32.eqz (local.get $depth)) (then (return (i32.const 0))))
    (local.set $n (i32.add (i32.const 1024) (i32.mul (global.get $seen) (i32.const 16))))
    (global.set $seen (i32.add (global.get $seen) (i32.const 1)))
    (i32.store offset=8 (local.get $n) (i32.add (i32.mul (local.get $k) (i32.const 7)) (local.get $depth)))
    (i32.store (local.get $n)
      (call $build (i32.add (local.get $k) (i32.const 1)) (i32.sub (local.get $depth) (i32.const 1))))
    (i32.store offset=4 (local.get $n)
      (call $build (i32.add (local.get $k) (i32.const 2)) (i32.sub (local.get $depth) (i32.const 1))))
    (local.get $n))

  (func (export "walk") (result i32)
    (global.set $seen (i32.const 0))
    (call $walk (call $build (i32.const 1) (i32.const 9))))

  ;; acc + n, where the entry passes the return block 0 and n
  (func $fib (param $n i32) (result i64)
    (local $acc i64)
    (block $done
      (loop $l
        (br_if $done (i32.lt_u (local.get $n) (i32.const 2)))
        (local.set $acc (i64.add (local.get $acc)
          (call $fib (i32.sub (local.get $n) (i32.const 1)))))
        (local.set $n (i32.sub (local.get $n) (i32.const 2)))
        (br $l)))
    (i64.add (local.get $acc) (i64.extend_i32_u (local.get $n))))
  (func (export "fib") (result i64) (call $fib (i32.const 24)))

  ;; a parameter itself as the test, and the return of another parameter
  (func $early_param (param $x i32) (param $y i32) (result i32)
    (if (local.get $x) (then (return (local.get $y))))
    (i32.add (call $early_param (i32.const 1) (i32.add (local.get $y) (i32.const 3)))
             (call $early_param (i32.const 1) (local.get $y))))
  ;; a negative constant
  (func $early_neg (param $x i32) (result i32)
    (if (i32.lt_s (local.get $x) (i32.const -5)) (then (return (i32.const 7))))
    (i32.add (call $early_neg (i32.sub (local.get $x) (i32.const 4))) (local.get $x)))
  ;; i64, against another parameter, and an expression of both
  (func $early_i64 (param $a i64) (param $b i64) (result i64)
    (if (i64.ge_u (local.get $a) (local.get $b))
      (then (return (i64.xor (i64.sub (local.get $a) (i64.const 5)) (local.get $b)))))
    (i64.add (call $early_i64 (i64.add (local.get $a) (i64.const 3)) (local.get $b))
             (local.get $a)))
  ;; nothing returned
  (func $early_void (param $p i32) (param $n i32)
    (if (i32.eqz (local.get $n)) (then (return)))
    (i32.store (local.get $p) (i32.add (i32.load (local.get $p)) (local.get $n)))
    (call $early_void (local.get $p) (i32.sub (local.get $n) (i32.const 1))))
  ;; the test reached through the table, so nothing inlines it
  (func $early_mix (param $x i32) (result i32)
    (if (i32.ne (local.get $x) (i32.const 300)) (then (return (i32.const -1))))
    (call $early_neg (i32.const 3)))
  (func (export "early_tests") (result i64)
    (i32.store (i32.const 128) (i32.const 0))
    (call $early_void (i32.const 128) (i32.const 50))
    (i64.add
      (i64.extend_i32_u
        (i32.add (i32.add (call $early_param (i32.const 0) (i32.const 11))
                          (call $early_neg (i32.const 40)))
                 (i32.add (i32.load (i32.const 128))
                          (i32.add (call_indirect (type $t1) (i32.const 300) (global.get $slot_mix))
                                   (call_indirect (type $t1) (i32.const 301) (global.get $slot_mix))))))
      (i64.add (call $early_i64 (i64.const 1) (i64.const 40))
               (call $early_i64 (i64.const 0x123456789) (i64.const 9)))))

  ;; ...and the ones that must keep their test after the prologue
  (func $early_load (param $p i32) (param $n i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load (local.get $p)))
    (if (i32.eqz (local.get $n)) (then (return (i32.const 5))))
    (i32.add (local.get $v) (call $early_load (local.get $p) (i32.sub (local.get $n) (i32.const 1)))))
  (func $early_store (param $p i32) (param $n i32) (result i32)
    (if (i32.eqz (local.get $n)) (then (i32.store (local.get $p) (i32.const 99)) (return (i32.const 2))))
    (i32.add (local.get $n) (call $early_store (local.get $p) (i32.sub (local.get $n) (i32.const 1)))))
  (func $early_float (param $x i32) (param $f f64) (result f64)
    (if (i32.eqz (local.get $x)) (then (return (local.get $f))))
    (f64.add (call $early_float (i32.sub (local.get $x) (i32.const 1)) (f64.mul (local.get $f) (f64.const 1.5)))
             (f64.const 0.25)))
  (func $early_ninth (param $a i32) (param $b i32) (param $c i32) (param $d i32)
                     (param $e i32) (param $f i32) (param $g i32) (param $h i32)
                     (param $n i32) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (local.get $h))))
    (i32.add (local.get $a)
      (call $early_ninth (local.get $b) (local.get $c) (local.get $d) (local.get $e)
        (local.get $f) (local.get $g) (local.get $h) (i32.add (local.get $a) (local.get $n))
        (i32.sub (local.get $n) (i32.const 1)))))
  (func (export "early_kept") (result i64)
    (i32.store (i32.const 192) (i32.const 13))
    (i64.add
      (i64.extend_i32_u
        (i32.add (i32.add (call $early_load (i32.const 192) (i32.const 6))
                          (call $early_store (i32.const 200) (i32.const 9)))
                 (i32.add (i32.load (i32.const 200))
                          (call $early_ninth (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                            (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 12)))))
      (i64.trunc_f64_s (call $early_float (i32.const 10) (f64.const 3.0)))))

  ;; ---- a zero across calls ----
  (func $take (param $x i32) (param $p i32) (result i32)
    (i32.store (local.get $p) (i32.add (i32.load (local.get $p)) (i32.add (local.get $x) (i32.const 1))))
    (i32.load (local.get $p)))
  (func $zeros (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $s (i32.add (local.get $s) (call $take (i32.const 0) (local.get $p))))
      (i64.store offset=8 (local.get $p) (i64.const 0))
      (i32.store offset=16 (local.get $p) (i32.const 0))
      (local.set $s (i32.add (local.get $s) (call $take (i32.const 0) (i32.add (local.get $p) (i32.const 4)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (if (i32.gt_u (local.get $s) (i32.const 1000000)) (then (return (local.get $s))))
    (i32.const 0))
  (func (export "zeros") (result i64)
    (i64.store (i32.const 256) (i64.const -1))
    (i64.store (i32.const 264) (i64.const -1))
    (i32.store (i32.const 272) (i32.const -1))
    (i64.add (i64.extend_i32_u (call $zeros (i32.const 256) (i32.const 30)))
      (i64.add (i64.load (i32.const 256))
        (i64.add (i64.load (i32.const 264)) (i64.extend_i32_u (i32.load (i32.const 272)))))))

  ;; ---- two writes of a global ----
  (func $reader (result i32)
    (global.set $seen (i32.add (global.get $seen) (global.get $g)))
    (global.get $seen))
  (func $gdse_two (param $x i32)
    (global.set $g (local.get $x))
    (global.set $g (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 8))))
  (func $gdse_get (param $x i32) (result i32)
    (local $y i32)
    (global.set $g (local.get $x))
    (local.set $y (global.get $g))
    (global.set $g (i32.add (local.get $y) (i32.const 1)))
    (local.get $y))
  (func $gdse_call (param $x i32) (result i32)
    (local $r i32)
    (global.set $g (local.get $x))
    (local.set $r (call $reader))
    (global.set $g (i32.add (local.get $x) (i32.const 1)))
    (local.get $r))
  (func (export "gdse") (result i32)
    (global.set $seen (i32.const 0))
    (call $gdse_two (i32.const 21))
    (i32.add (global.get $g)
      (i32.add (i32.mul (call $gdse_get (i32.const 40)) (i32.const 100))
        (i32.add (i32.mul (global.get $g) (i32.const 10000))
          (i32.add (call $gdse_call (i32.const 7)) (i32.mul (global.get $g) (i32.const 7))))))))
