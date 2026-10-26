;; Frames on AArch64 (isel.c emit_prologue, emit_teardown): the first store
;; of the save area moves sp down by all of it (stp ..., [sp, #-N]!) and the
;; last load gives it back (ldp ..., [sp], #N), with no sub or add of their
;; own, and a function with a call checks its stack with one cmp sp, x16 and
;; a branch to a trap stub after the body, which the check falls through.
;; Every saved register stays in the slot the unwinder reads it from, so a
;; throw through frames that save registers ($catch over $thrower), a tail
;; call that gives such a frame up ($tail), frames with spill slots below the
;; save area ($many_rec, fourteen loaded values live across its call) and
;; with float registers saved in it ($fmany_rec), and a recursion that runs
;; out of stack ($deep) all give their answers.
;;
;; fib, Rust's shape (rsbench/recurse): its frame and check in five
;; instructions, the return in three:
;; shape: fib sub\s+sp, sp=0
;; shape: fib add\s+sp, sp=0
;; shape: fib \[sp, #-0x[0-9a-f]+\]!=1
;; shape: fib \[sp\], #0x[0-9a-f]+=1
;; shape: fib cmp\s+sp, x16=1
;; shape: fib mov\s+x17, sp=0
;; many_rec: the save area by the first store, the spill slots below it by a
;; sub, and both given back the other way:
;; shape: many_rec sub\s+sp, sp, #0x=1
;; shape: many_rec add\s+sp, sp, #0x=1
;; shape: many_rec \[sp, #-0x[0-9a-f]+\]!=1
;; shape: many_rec \[sp\], #0x[0-9a-f]+=1
;;
;; run: fib 1 => i64:1
;; run: fib 20 => i64:6765
;; run: fib 25 => i64:75025
;; run: fibloop 1 => i64:1
;; run: fibloop 25 => i64:75025
;; run: tail 10 0 => i32:143
;; run: tail 20 5 => i32:17715
;; run: catch 5 => i32:1031
;; run: catch 0 => i32:1011
;; run: catch 40 => i32:1171
;; run: many 0 => i32:1
;; run: many 1 => i32:11343
;; run: many 30 => i32:147535806
;; run: fmany 9 => f64:0x40881a6240000000
;; run: deep 0 => trap: call stack exhausted
(module
  (memory 1)
  (tag $e (param i32))

  (func $fib (export "fib") (param i32) (result i64)
    (if (result i64) (i32.lt_u (local.get 0) (i32.const 2))
      (then (i64.extend_i32_u (local.get 0)))
      (else (i64.add (call $fib (i32.sub (local.get 0) (i32.const 1)))
                     (call $fib (i32.sub (local.get 0) (i32.const 2)))))))

  ;; fib as LLVM leaves Rust's: the second call a loop, which each call
  ;; enters, its frame and the loop's around a call
  (func $fibloop (export "fibloop") (param $n i32) (result i64)
    (local $acc i64)
    (if (i32.lt_u (local.get $n) (i32.const 2))
      (then (return (i64.extend_i32_u (local.get $n)))))
    (loop $l
      (local.set $acc (i64.add (local.get $acc)
                               (call $fibloop (i32.sub (local.get $n) (i32.const 1)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 2)))
      (br_if $l (i32.gt_u (local.get $n) (i32.const 1))))
    (i64.add (local.get $acc) (i64.extend_i32_u (local.get $n))))

  ;; the sum of fib(1..n) onto acc, a call and a tail call per step
  (func $tail (export "tail") (param i32 i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (local.get 1))
      (else
        (return_call $tail (i32.sub (local.get 0) (i32.const 1))
          (i32.add (local.get 1) (i32.wrap_i64 (call $fib (local.get 0))))))))

  ;; 1000 thrown from the bottom, through n frames that each keep 7n in a
  ;; callee-saved register across their call
  (func $thrower (param i32) (result i32)
    (local i32)
    (if (i32.eqz (local.get 0)) (then (throw $e (i32.const 1000))))
    (local.set 1 (i32.mul (local.get 0) (i32.const 7)))
    (i32.add (call $thrower (i32.sub (local.get 0) (i32.const 1))) (local.get 1)))

  (func $catch (export "catch") (param i32) (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (i32.mul (local.get 0) (i32.const 3)))
    (local.set $b (i32.add (local.get 0) (i32.const 11)))
    (i32.add
      (block $h (result i32)
        (try_table (catch $e $h)
          (drop (call $thrower (local.get 0))))
        (i32.const -1))
      (i32.add (local.get $a) (local.get $b))))

  ;; fourteen words loaded before the call, every one read after it, and the
  ;; result stored where a deeper level's loads will see it
  (func $many_rec (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $e i32)
    (local $f i32) (local $g i32) (local $h i32) (local $i i32) (local $j i32)
    (local $k i32) (local $l i32) (local $m i32) (local $o i32) (local $r i32)
    (if (i32.eqz (local.get $n)) (then (return (i32.const 1))))
    (local.set $a (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 0)) (i32.const 15)) (i32.const 2))))
    (local.set $b (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 1)) (i32.const 15)) (i32.const 2))))
    (local.set $c (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 2)) (i32.const 15)) (i32.const 2))))
    (local.set $d (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 3)) (i32.const 15)) (i32.const 2))))
    (local.set $e (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 4)) (i32.const 15)) (i32.const 2))))
    (local.set $f (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 5)) (i32.const 15)) (i32.const 2))))
    (local.set $g (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 6)) (i32.const 15)) (i32.const 2))))
    (local.set $h (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 7)) (i32.const 15)) (i32.const 2))))
    (local.set $i (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 8)) (i32.const 15)) (i32.const 2))))
    (local.set $j (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 9)) (i32.const 15)) (i32.const 2))))
    (local.set $k (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 10)) (i32.const 15)) (i32.const 2))))
    (local.set $l (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 11)) (i32.const 15)) (i32.const 2))))
    (local.set $m (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 12)) (i32.const 15)) (i32.const 2))))
    (local.set $o (i32.load (i32.shl (i32.and (i32.add (local.get $n) (i32.const 13)) (i32.const 15)) (i32.const 2))))
    (local.set $r (call $many_rec (i32.sub (local.get $n) (i32.const 1))))
    (local.set $r
      (i32.add (i32.mul (local.get $r) (i32.const 3))
        (i32.add
          (i32.add
            (i32.add (i32.add (local.get $a) (i32.mul (local.get $b) (i32.const 2)))
                     (i32.add (i32.mul (local.get $c) (i32.const 3)) (i32.mul (local.get $d) (i32.const 4))))
            (i32.add (i32.add (i32.mul (local.get $e) (i32.const 5)) (i32.mul (local.get $f) (i32.const 6)))
                     (i32.add (i32.mul (local.get $g) (i32.const 7)) (i32.mul (local.get $h) (i32.const 8)))))
          (i32.add
            (i32.add (i32.add (i32.mul (local.get $i) (i32.const 9)) (i32.mul (local.get $j) (i32.const 10)))
                     (i32.add (i32.mul (local.get $k) (i32.const 11)) (i32.mul (local.get $l) (i32.const 12))))
            (i32.add (i32.mul (local.get $m) (i32.const 13)) (i32.mul (local.get $o) (i32.const 14)))))))
    (i32.store (i32.shl (i32.and (local.get $n) (i32.const 15)) (i32.const 2)) (local.get $r))
    (local.get $r))

  ;; word i of the first sixteen is i*i + 3
  (func $many (export "many") (param i32) (result i32)
    (local $i i32)
    (loop $fill
      (i32.store (i32.shl (local.get $i) (i32.const 2))
                 (i32.add (i32.mul (local.get $i) (local.get $i)) (i32.const 3)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $i) (i32.const 16))))
    (call $many_rec (local.get 0)))

  ;; four doubles loaded before the call, read after it
  (func $fmany_rec (param $n i32) (result f64)
    (local $a f64) (local $b f64) (local $c f64) (local $d f64)
    (if (i32.eqz (local.get $n)) (then (return (f64.const 1))))
    (local.set $a (f64.load offset=256 (i32.shl (i32.and (local.get $n) (i32.const 7)) (i32.const 3))))
    (local.set $b (f64.load offset=256 (i32.shl (i32.and (i32.add (local.get $n) (i32.const 1)) (i32.const 7)) (i32.const 3))))
    (local.set $c (f64.load offset=256 (i32.shl (i32.and (i32.add (local.get $n) (i32.const 2)) (i32.const 7)) (i32.const 3))))
    (local.set $d (f64.load offset=256 (i32.shl (i32.and (i32.add (local.get $n) (i32.const 3)) (i32.const 7)) (i32.const 3))))
    (f64.add
      (f64.mul (call $fmany_rec (i32.sub (local.get $n) (i32.const 1))) (f64.const 1.5))
      (f64.add (f64.add (local.get $a) (f64.mul (local.get $b) (f64.const 0.5)))
               (f64.add (f64.mul (local.get $c) (f64.const 0.25)) (local.get $d)))))

  ;; double i of eight at 256 is i + 0.125
  (func $fmany (export "fmany") (param i32) (result f64)
    (local $i i32)
    (loop $fill
      (f64.store offset=256 (i32.shl (local.get $i) (i32.const 3))
                 (f64.add (f64.convert_i32_u (local.get $i)) (f64.const 0.125)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $i) (i32.const 8))))
    (call $fmany_rec (local.get 0)))

  (func $deep (export "deep") (param i32) (result i32)
    (i32.add (call $deep (i32.add (local.get 0) (i32.const 1))) (local.get 0)))
)
