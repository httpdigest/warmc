;; Callee-saved registers from the two ends (regalloc.inc, cs_top): a value
;; live across a call made in a loop takes x28 down, any other x22 up.  The
;; loop in $run keeps its counter, its sum and its bound across each call of
;; $tri, which keeps its argument across its own call, at no loop: from the
;; same end, the two would share x22, and $tri's save and restore of it would
;; put $run's counter through memory on every call.
;;
;; shape: run \b[xw]2[2-6]\b=0
;; shape: run stp\tx28=1
;; shape: tri \b[xw]2[3-8]\b=0
;; shape: tri stp\tx22=1
;;
;; run: run 10 => i32:85
;; run: run 0 => i32:0
;; run: tri 4 => i32:10
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
)
