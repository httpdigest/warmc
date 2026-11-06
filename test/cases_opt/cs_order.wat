;; Callee-saved registers from the two ends (regalloc.inc, cs_top): a value
;; live across a call made in a loop takes x28 down, any other x22 up.  The
;; loop in $run keeps its counter, its sum and its bound across each call of
;; $tri, which keeps its argument across its own call, at no loop: from the
;; same end, the two would share x22, and $tri's save and restore of it would
;; put $run's counter through memory on every call.  ($tri's own call takes
;; its base case in -- partial inlining of a function's own start -- so its
;; argument is saved around that call alone, in no callee-saved register.)
;; Not a constant, whose callee's restore is off any chain the loop waits
;; on: $run2's four constants of two moves each, made at its start, take
;; x22 up, and its counter and sum x28 down -- $mix saves x22 and x23,
;; which with the constants on top had the sum.
;;
;; shape: run \b[xw]2[2-6]\b=0
;; shape: run stp\tx28=1
;; shape: tri \b[xw]2[3-8]\b=0
;; shape: run2 eor\s+w2[23],=0
;; shape: run2 add\s+w28, w28, #0x1=1
;;
;; run: run 10 => i32:85
;; run: run 0 => i32:0
;; run: tri 4 => i32:10
;; run: run2 0 => i32:0
;; run: run2 1 => i32:1842037837
;; run: run2 10 => i32:922163532
;; run: run2 1000 => i32:-1377825956
;; run: mix 100 => i32:1025
(module
  (func $tri (export "tri") (param $x i32) (result i32)
    (if (result i32) (i32.eqz (local.get $x))
      (then (i32.const 0))
      (else (i32.add (local.get $x) (call $tri (i32.sub (local.get $x) (i32.const 1)))))))
  (func $run (export "run") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (call $tri (i32.and (local.get $i) (i32.const 7)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; keeps three values across its own call: x22 up
  (func $mix (export "mix") (param $x i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (if (result i32) (i32.lt_u (local.get $x) (i32.const 2))
      (then (local.get $x))
      (else
        (local.set $a (i32.mul (local.get $x) (i32.const 3)))
        (local.set $b (i32.xor (local.get $x) (i32.const 5)))
        (local.set $c (i32.add (local.get $x) (i32.const 7)))
        (i32.add (i32.add (local.get $a) (local.get $b))
                 (i32.add (local.get $c) (call $mix (i32.shr_u (local.get $x) (i32.const 1))))))))
  (func $run2 (export "run2") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $t i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $t (call $mix (i32.and (local.get $i) (i32.const 1023))))
        (local.set $s (i32.add (local.get $s)
          (i32.xor (i32.mul (local.get $t) (i32.const 0x10624dd3))
                   (i32.add (i32.mul (local.get $i) (i32.const 0x55555556))
                            (i32.xor (local.get $t) (i32.const 0x7fff1234))))))
        (local.set $s (i32.xor (local.get $s) (i32.const 0x12345679)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
)
