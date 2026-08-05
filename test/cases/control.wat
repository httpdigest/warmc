(module
  ;; ---- helpers exercised through zero-argument exports ----

  (func $sum_to (param $n i32) (result i32)
    (local $i i32) (local $acc i32)
    (block $done
      (loop $again
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $acc (i32.add (local.get $acc) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $again)))
    (local.get $acc))

  (func $classify (param $x i32) (result i32)
    (block $d
      (block $c
        (block $b
          (block $a
            (br_table $a $b $c $d (local.get $x)))
          (return (i32.const 100)))
        (return (i32.const 200)))
      (return (i32.const 300)))
    (i32.const 400))

  ;; `if` with block parameters and a result
  (func $pick (param $c i32) (param $a i32) (param $b i32) (result i32)
    (local $t i32)
    (local.get $a)
    (local.get $b)
    (if (param i32 i32) (result i32) (local.get $c)
      (then (drop))
      (else (local.set $t) (drop) (local.get $t))))

  ;; iterative factorial with a plain loop
  (func $fact2 (param $n i32) (result i64)
    (local $acc i64)
    (local.set $acc (i64.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $acc (i64.mul (local.get $acc) (i64.extend_i32_u (local.get $n))))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $acc))

  ;; a loop whose label carries values across the back edge
  (func $lp (param $n i32) (result i32)
    (local $a i32) (local $b i32)
    (local.get $n)
    (i32.const 0)
    (loop $l (param i32 i32) (result i32)
      (local.set $b)
      (local.set $a)
      (if (result i32) (i32.eqz (local.get $a))
        (then (local.get $b))
        (else
          (i32.sub (local.get $a) (i32.const 1))
          (i32.add (local.get $b) (local.get $a))
          (br $l)))))

  ;; multi-value block results
  (func $swap2 (param i32 i64) (result i64 i32)
    (local.get 0)
    (local.get 1)
    (block (param i32 i64) (result i64 i32)
      (local.set 1)
      (local.set 0)
      (local.get 1)
      (local.get 0)))

  ;; nested blocks branching several levels out
  (func $deep (param $x i32) (result i32)
    (block $out (result i32)
      (block (result i32)
        (block (result i32)
          (block (result i32)
            (if (i32.eqz (local.get $x)) (then (br $out (i32.const 99))))
            (i32.const 11))
          (i32.const 1) (i32.add))
        (i32.const 2) (i32.add))
      (i32.const 4) (i32.add)))

  ;; unreachable code after a branch must still parse and lay out correctly
  (func $after_br (result i32)
    (block (result i32)
      (br 0 (i32.const 42))
      (unreachable)))

  (func $select_i (param i32 i32 i32) (result i32)
    (select (local.get 1) (local.get 2) (local.get 0)))
  (func $select_f (param i32 f64 f64) (result i64)
    (i64.reinterpret_f64 (select (local.get 1) (local.get 2) (local.get 0))))

  ;; deep operand stack, forcing register spills
  (func $spill (result i32)
    (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
    (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
    (i32.const 9) (i32.const 10) (i32.const 11) (i32.const 12)
    (i32.const 13) (i32.const 14) (i32.const 15) (i32.const 16)
    (i32.const 17) (i32.const 18) (i32.const 19) (i32.const 20)
    (i32.mul) (i32.mul) (i32.mul) (i32.mul) (i32.mul)
    (i32.mul) (i32.mul) (i32.mul) (i32.mul) (i32.mul)
    (i32.mul) (i32.mul) (i32.mul) (i32.mul) (i32.mul)
    (i32.mul) (i32.mul) (i32.mul) (i32.mul))

  (func $fspill (result i64)
    (f64.const 1.5) (f64.const 2.5) (f64.const 3.5) (f64.const 4.5)
    (f64.const 5.5) (f64.const 6.5) (f64.const 7.5) (f64.const 8.5)
    (f64.const 9.5) (f64.const 10.5) (f64.const 11.5) (f64.const 12.5)
    (f64.const 13.5) (f64.const 14.5) (f64.const 15.5) (f64.const 16.5)
    (f64.const 17.5) (f64.const 18.5) (f64.const 19.5) (f64.const 20.5)
    (f64.const 21.5) (f64.const 22.5) (f64.const 23.5) (f64.const 24.5)
    (f64.const 25.5) (f64.const 26.5) (f64.const 27.5) (f64.const 28.5)
    (f64.const 29.5) (f64.const 30.5) (f64.const 31.5) (f64.const 32.5)
    (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add)
    (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add)
    (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add)
    (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add) (f64.add)
    (f64.add) (f64.add) (f64.add)
    (i64.reinterpret_f64))

  ;; mixed int/float stack interleaving
  (func $mixed (result i32)
    (local $a i32) (local $b f64)
    (local.set $a (i32.const 3))
    (local.set $b (f64.const 2.5))
    (i32.add
      (local.get $a)
      (i32.trunc_f64_s
        (f64.mul (local.get $b) (f64.convert_i32_s (local.get $a))))))

  ;; ---- exports ----
  (func (export "sum_0")   (result i32) (call $sum_to (i32.const 0)))
  (func (export "sum_1")   (result i32) (call $sum_to (i32.const 1)))
  (func (export "sum_100") (result i32) (call $sum_to (i32.const 100)))
  (func (export "sum_neg") (result i32) (call $sum_to (i32.const -5)))

  (func (export "cls_0") (result i32) (call $classify (i32.const 0)))
  (func (export "cls_1") (result i32) (call $classify (i32.const 1)))
  (func (export "cls_2") (result i32) (call $classify (i32.const 2)))
  (func (export "cls_3") (result i32) (call $classify (i32.const 3)))
  (func (export "cls_9") (result i32) (call $classify (i32.const 9)))
  (func (export "cls_neg") (result i32) (call $classify (i32.const -1)))

  (func (export "pick_t") (result i32) (call $pick (i32.const 1) (i32.const 5) (i32.const 6)))
  (func (export "pick_f") (result i32) (call $pick (i32.const 0) (i32.const 5) (i32.const 6)))

  (func (export "fact_0")  (result i64) (call $fact2 (i32.const 0)))
  (func (export "fact_1")  (result i64) (call $fact2 (i32.const 1)))
  (func (export "fact_10") (result i64) (call $fact2 (i32.const 10)))
  (func (export "fact_20") (result i64) (call $fact2 (i32.const 20)))

  (func (export "swap_a") (result i64 i32) (call $swap2 (i32.const 9) (i64.const -1)))
  (func (export "lp_0")  (result i32) (call $lp (i32.const 0)))
  (func (export "lp_1")  (result i32) (call $lp (i32.const 1)))
  (func (export "lp_10") (result i32) (call $lp (i32.const 10)))

  (func (export "deep_0") (result i32) (call $deep (i32.const 0)))
  (func (export "deep_1") (result i32) (call $deep (i32.const 1)))

  (func (export "after_br") (result i32) (call $after_br))

  (func (export "sel_t") (result i32) (call $select_i (i32.const 1) (i32.const 111) (i32.const 222)))
  (func (export "sel_f") (result i32) (call $select_i (i32.const 0) (i32.const 111) (i32.const 222)))
  (func (export "sel_ft") (result i64) (call $select_f (i32.const 1) (f64.const 1.25) (f64.const -8.5)))
  (func (export "sel_ff") (result i64) (call $select_f (i32.const 0) (f64.const 1.25) (f64.const -8.5)))

  (func (export "spill") (result i32) (call $spill))
  (func (export "fspill") (result i64) (call $fspill))
  (func (export "mixed") (result i32) (call $mixed))

  ;; A loop that is one block: the header is also the latch.  Rotating it
  ;; moves the back edge onto the copy, and the copy is a header of exactly
  ;; the same shape, so a rotation pass that does not mark what it has already
  ;; done never terminates on these -- the compiler used to spin forever on
  ;; the first of them rather than emit anything.
  (func (export "selfloop_count") (result i32)
    (local $i i32) (local $a i32)
    (local.set $i (i32.const 3))
    (loop $again
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $again (local.get $i)))
    (local.get $a))

  (func (export "selfloop_acc") (result i32)
    (local $i i32) (local $a i32)
    (local.set $i (i32.const 6))
    (loop $again
      (local.set $a (i32.add (local.get $a) (local.get $i)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $again (local.get $i)))
    (local.get $a))

  (func (export "selfloop_nested") (result i32)
    (local $i i32) (local $j i32) (local $a i32)
    (local.set $i (i32.const 4))
    (block $out (loop $outer
      (local.set $j (i32.const 3))
      (loop $inner
        (local.set $j (i32.sub (local.get $j) (i32.const 1)))
        (br_if $inner (local.get $j)))
      (local.set $a (i32.add (local.get $a) (i32.const 5)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $outer (local.get $i))))
    (local.get $a))

  (func (export "selfloop_zero") (result i32)
    (local $i i32)
    (local.set $i (i32.const 1))
    (loop $again
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $again (local.get $i)))
    (i32.const 77))

  ;; ---- nested block types that name a single value type ----
  ;;
  ;; A block type is either empty, a *type index*, or one value type written
  ;; inline.  The validator hands every frame an array of its result types, and
  ;; the inline form has no array of its own -- so it needs somewhere to keep
  ;; one, and that somewhere has to outlive every frame opened inside the
  ;; block.  One shared cell is not enough: the inner block overwrites it and
  ;; the outer frame ends up checking its results against the inner block's
  ;; type.  Valid modules were rejected and invalid ones accepted, and every
  ;; pair below has an inner type that differs from the outer one, which is
  ;; the only shape that shows it.
  (func (export "bt_i64_in_i32") (result i32)
    (block (result i32)
      (block (result i64) (i64.const 1))
      (drop)
      (i32.const 7)))

  (func (export "bt_i32_in_i64") (result i64)
    (block (result i64)
      (block (result i32) (i32.const 3))
      (drop)
      (i64.const 9)))

  (func (export "bt_f64_in_i32") (result i32)
    (block (result i32)
      (block (result f64) (f64.const 2.5))
      (drop)
      (i32.const 11)))

  ;; Three deep, so a fix that keeps only the innermost still fails.
  (func (export "bt_three_deep") (result i32)
    (block (result i32)
      (block (result i64)
        (block (result f32) (f32.const 1.5))
        (drop)
        (i64.const 4))
      (drop)
      (i32.const 13)))

  ;; The same through `if` and `loop`, whose frames take the same path.
  (func (export "bt_if_nested") (result i32)
    (if (result i32) (i32.const 1)
      (then
        (if (result f32) (i32.const 1)
          (then (f32.const 1.0))
          (else (f32.const 2.0)))
        (drop)
        (i32.const 17))
      (else (i32.const 0))))

  (func (export "bt_loop_nested") (result i64)
    (loop (result i64)
      (block (result i32) (i32.const 5))
      (drop)
      (i64.const 19)))

  ;; An inner block whose type matches the *function* result rather than the
  ;; enclosing block's, so a fix that reads the wrong one still answers.
  (func (export "bt_shadow_func") (result i32)
    (block (result i32)
      (block (result i32)
        (block (result i64) (i64.const 21))
        (drop)
        (i32.const 23))))
)
