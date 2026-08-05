(module
  (table 8 funcref)
  (elem (i32.const 0) $sq $cube $neg $id)
  (elem func $sq $neg)

  (type $unary (func (param i32) (result i32)))
  (type $binary (func (param i32 i32) (result i32)))

  (func $sq   (param i32) (result i32) (i32.mul (local.get 0) (local.get 0)))
  (func $cube (param i32) (result i32) (i32.mul (local.get 0) (i32.mul (local.get 0) (local.get 0))))
  (func $neg  (param i32) (result i32) (i32.sub (i32.const 0) (local.get 0)))
  (func $id   (param i32) (result i32) (local.get 0))

  ;; more integer parameters than there are argument registers
  (func $many_i (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)
    (i32.add (i32.add (i32.add (i32.add (local.get 0) (local.get 1))
                               (i32.add (local.get 2) (local.get 3)))
                      (i32.add (i32.add (local.get 4) (local.get 5))
                               (i32.add (local.get 6) (local.get 7))))
             (i32.add (local.get 8) (local.get 9))))

  ;; more float parameters than there are float argument registers
  (func $many_f (param f64 f64 f64 f64 f64 f64 f64 f64 f64 f64) (result f64)
    (f64.add (f64.add (f64.add (f64.add (local.get 0) (local.get 1))
                               (f64.add (local.get 2) (local.get 3)))
                      (f64.add (f64.add (local.get 4) (local.get 5))
                               (f64.add (local.get 6) (local.get 7))))
             (f64.add (local.get 8) (local.get 9))))

  ;; mixed integer and float parameters, spilling both classes
  (func $mixed (param i32 f64 i64 f32 i32 f64 i32 f32 i32 i32 f64 i64) (result i64)
    (i64.add
      (i64.add (i64.extend_i32_s (local.get 0)) (local.get 2))
      (i64.add
        (i64.trunc_f64_s (f64.add (local.get 1) (local.get 5)))
        (i64.add (i64.extend_i32_s (i32.add (local.get 4) (local.get 6)))
                 (i64.add (local.get 11)
                          (i64.trunc_f32_s (f32.add (local.get 3) (local.get 7))))))))

  ;; multi-value results, including more than fit in registers
  (func $multi3 (param i32) (result i32 i64 f64)
    (local.get 0)
    (i64.extend_i32_s (local.get 0))
    (f64.convert_i32_s (local.get 0)))

  (func $multi6 (result i32 i32 i32 i32 i32 i32)
    (i32.const 1) (i32.const 2) (i32.const 3)
    (i32.const 4) (i32.const 5) (i32.const 6))

  ;; mutual recursion
  (func $even (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else (call $odd (i32.sub (local.get 0) (i32.const 1))))))
  (func $odd (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (call $even (i32.sub (local.get 0) (i32.const 1))))))

  (func $apply (param $f i32) (param $x i32) (result i32)
    (call_indirect (type $unary) (local.get $x) (local.get $f)))

  ;; ---- exports ----
  (func (export "sq5")   (result i32) (call $sq (i32.const 5)))
  (func (export "cube3") (result i32) (call $cube (i32.const 3)))

  (func (export "many_i") (result i32)
    (call $many_i (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                  (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                  (i32.const 9) (i32.const 10)))
  (func (export "many_f") (result i64)
    (i64.reinterpret_f64
      (call $many_f (f64.const 1.5) (f64.const 2.5) (f64.const 3.5) (f64.const 4.5)
                    (f64.const 5.5) (f64.const 6.5) (f64.const 7.5) (f64.const 8.5)
                    (f64.const 9.5) (f64.const 10.5))))
  (func (export "mixed") (result i64)
    (call $mixed (i32.const 1) (f64.const 2.0) (i64.const 3) (f32.const 4.0)
                 (i32.const 5) (f64.const 6.0) (i32.const 7) (f32.const 8.0)
                 (i32.const 9) (i32.const 10) (f64.const 11.0) (i64.const 12)))

  (func (export "multi3") (result i32 i64 f64) (call $multi3 (i32.const -3)))
  (func (export "multi6") (result i32 i32 i32 i32 i32 i32) (call $multi6))
  (func (export "multi_sum") (result i64)
    (local $a i32) (local $b i64) (local $c f64)
    (call $multi3 (i32.const 7))
    (local.set $c) (local.set $b) (local.set $a)
    (i64.add (i64.extend_i32_s (local.get $a))
             (i64.add (local.get $b) (i64.trunc_f64_s (local.get $c)))))

  (func (export "even_10") (result i32) (call $even (i32.const 10)))
  (func (export "odd_10")  (result i32) (call $odd (i32.const 10)))
  (func (export "even_101") (result i32) (call $even (i32.const 101)))

  (func (export "ind_sq")   (result i32) (call $apply (i32.const 0) (i32.const 6)))
  (func (export "ind_cube") (result i32) (call $apply (i32.const 1) (i32.const 4)))
  (func (export "ind_neg")  (result i32) (call $apply (i32.const 2) (i32.const 42)))
  (func (export "ind_id")   (result i32) (call $apply (i32.const 3) (i32.const 77)))

  (func (export "tbl_init") (result i32)
    (table.init 1 (i32.const 4) (i32.const 0) (i32.const 2))
    (call $apply (i32.const 4) (i32.const 9)))
  (func (export "tbl_copy") (result i32)
    (table.copy (i32.const 6) (i32.const 0) (i32.const 2))
    (call $apply (i32.const 6) (i32.const 11)))
  (func (export "tbl_size") (result i32) (table.size 0))

  ;; deep recursion to make sure frames are laid out correctly
  (func $depth (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (i32.const 1) (call $depth (i32.sub (local.get 0) (i32.const 1)))))))
  (func (export "depth_1000") (result i32) (call $depth (i32.const 1000)))


  ;; --------------------------------------- values that outlive a call
  ;;
  ;; A value live across a call may only sit in a callee-saved register, and
  ;; which values those are is a liveness question: what is live immediately
  ;; after each call returns.  Answering it any other way -- "is a call inside
  ;; my numbered interval" -- over-marks, which costs registers, and answering
  ;; it too narrowly loses a value to the callee.  Each export below reads a
  ;; value after a call through a *different* path, so a liveness walk that
  ;; misses one path returns the callee's leftovers instead.  Sixteen live
  ;; values is more than there are caller-saved registers, so anything that is
  ;; wrongly allowed into one really does get clobbered.

  ;; Read straight after the call, in the same block.
  (func (export "live_across_call") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $e i32) (local $f i32) (local $g i32) (local $h i32)
    (local.set $a (i32.const 11)) (local.set $b (i32.const 22))
    (local.set $c (i32.const 33)) (local.set $d (i32.const 44))
    (local.set $e (i32.const 55)) (local.set $f (i32.const 66))
    (local.set $g (i32.const 77)) (local.set $h (i32.const 88))
    (drop (call $many_i (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                        (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                        (i32.const 9) (i32.const 10)))
    (i32.add (i32.add (i32.add (local.get $a) (local.get $b))
                      (i32.add (local.get $c) (local.get $d)))
             (i32.add (i32.add (local.get $e) (local.get $f))
                      (i32.add (local.get $g) (local.get $h)))))  ;; 396

  ;; Read only on the far side of a merge, so the value reaches its use as a
  ;; block-parameter argument on an edge rather than as an operand.
  (func (export "live_across_call_edge") (param) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $e i32) (local $f i32) (local $g i32) (local $h i32)
    (local $r i32)
    (local.set $a (i32.const 11)) (local.set $b (i32.const 22))
    (local.set $c (i32.const 33)) (local.set $d (i32.const 44))
    (local.set $e (i32.const 55)) (local.set $f (i32.const 66))
    (local.set $g (i32.const 77)) (local.set $h (i32.const 88))
    (local.set $r (call $many_i (i32.const 1) (i32.const 2) (i32.const 3)
                                (i32.const 4) (i32.const 5) (i32.const 6)
                                (i32.const 7) (i32.const 8) (i32.const 9)
                                (i32.const 10)))
    (if (i32.eq (local.get $r) (i32.const 55))
      (then (local.set $a (i32.add (local.get $a) (local.get $b))))
      (else (local.set $a (i32.add (local.get $a) (local.get $c)))))
    (i32.add (i32.add (i32.add (local.get $a) (local.get $b))
                      (i32.add (local.get $c) (local.get $d)))
             (i32.add (i32.add (local.get $e) (local.get $f))
                      (i32.add (local.get $g) (local.get $h)))))  ;; 396+22

  ;; Read only by the comparison a branch absorbed, so the value is live at a
  ;; terminator and nowhere else after the call.
  (func (export "live_across_call_fused") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $e i32) (local $f i32) (local $g i32) (local $h i32)
    (local.set $a (i32.const 11)) (local.set $b (i32.const 22))
    (local.set $c (i32.const 33)) (local.set $d (i32.const 44))
    (local.set $e (i32.const 55)) (local.set $f (i32.const 66))
    (local.set $g (i32.const 77)) (local.set $h (i32.const 88))
    (drop (call $many_i (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                        (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                        (i32.const 9) (i32.const 10)))
    (if (i32.lt_u (local.get $a) (local.get $b))
      (then (local.set $d (i32.add (local.get $d) (i32.const 1000)))))
    (if (i32.gt_u (local.get $e) (local.get $f))
      (then (local.set $d (i32.add (local.get $d) (i32.const 2000)))))
    (i32.add (i32.add (i32.add (local.get $c) (local.get $d))
                      (i32.add (local.get $g) (local.get $h)))
             (local.get $e)))                                     ;; 33+1044+77+88+55

  ;; A loop that calls, with the accumulator written at the bottom of the body
  ;; and replaced at the top of the next -- dead at every call it appears to
  ;; span.  Nothing here can go wrong; it is the shape the precision is for,
  ;; and it is here so the shape has a test at all.
  (func (export "acc_not_across_call") (result i32)
    (local $i i32) (local $acc i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 5)))
      (local.set $acc (call $sq (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                             ;; 16

  ;; The call's own result is defined by the call, so the call cannot have
  ;; clobbered it -- marking it would cost a callee-saved register for nothing.
  (func (export "call_result_free") (result i32)
    (i32.add (call $sq (i32.const 7)) (call $cube (i32.const 3))))  ;; 49+27
)
