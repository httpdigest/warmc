;; The instance and memory 0's base pinned in x19 and x20 on arm64 (isel.c
;; codegen_pinned): a function is entered with them there and leaves them
;; there, so it neither copies x0 in nor saves the two, and a call or tail
;; call of one of the module's own functions passes no instance in x0; a call
;; of a C helper still does.  A function that calls one which reads memory
;; keeps x20 for it, however short of registers ($hot).  An executable's
;; module is the only instance there is, so none of its functions has the
;; check of another instance's call ahead of it (emit_entry_check, which
;; test/run_link.sh sees in objects).
;;
;; stats -O2 a64: entry_checks=0
;;
;; shape: fib mov\tx19, x0=0
;; shape: fib mov\tx0, x19=0
;; shape: fib x19, x|x\d+, x19=0
;; shape: fib \bx20\b=0
;; shape: peek \[x20\b=1
;; shape: peek mov\tx(9|10|19|20), x0=0
;; shape: peek ldr\tx\d+, \[x19=0
;; shape: grow mov\tx0, x19=1
;; shape: tailsum mov\tx0, x19=0
;; shape: tailsum b\t=1
;; shape: hot \b[xw]20\b=0
;;
;; run: fib 10 => i32:55
;; run: fib 1 => i32:1
;; run: peek 8 => i32:258
;; run: grow 1 => i32:1
;; run: viatable 4 => i32:5
;; run: total 100 => i32:5050
;; run: tailsum 10 => i32:55
;; run: hot 50 => i32:262145
;; run: hot 7 => i32:3457
(module
  (memory 1 4)
  (data (i32.const 8) "\02\01")
  (table 1 funcref)
  (elem (i32.const 0) $tabled)
  (func $fib (export "fib") (param $n i32) (result i32)
    (if (result i32) (i32.lt_u (local.get $n) (i32.const 2))
      (then (local.get $n))
      (else (i32.add (call $fib (i32.sub (local.get $n) (i32.const 1)))
                     (call $fib (i32.sub (local.get $n) (i32.const 2)))))))
  ;; a leaf reading memory: x20 itself, no copy in x10
  (func $peek (export "peek") (param $p i32) (result i32)
    (i32.load16_u (local.get $p)))
  ;; a C helper's call, which takes the instance in x0
  (func $grow (export "grow") (param $n i32) (result i32)
    (memory.grow (local.get $n)))
  ;; named by a segment only
  (func $tabled (param $x i32) (result i32)
    (i32.add (local.get $x) (i32.const 1)))
  (func $viatable (export "viatable") (param $x i32) (result i32)
    (call_indirect (param i32) (result i32) (local.get $x) (i32.const 0)))
  ;; recursive, so that it stays a function of its own
  (func $sum (param $n i32) (result i32)
    (if (result i32) (i32.eqz (local.get $n))
      (then (i32.const 0))
      (else (i32.add (local.get $n) (call $sum (i32.sub (local.get $n) (i32.const 1)))))))
  (func $total (export "total") (param $n i32) (result i32)
    (call $sum (local.get $n)))
  (func $tailsum (export "tailsum") (param $n i32) (result i32)
    (return_call $sum (local.get $n)))
  ;; More values carried round a loop than registers, the loop calling a
  ;; function that reads memory: the loop's function reads none itself, but
  ;; x20 is its callee's, and lent to one of the values it would break that.
  (func $rd (param $i i32) (result i32)
    (if (result i32) (i32.gt_u (local.get $i) (i32.const 1))
      (then (call $rd (i32.sub (local.get $i) (i32.const 2))))
      (else (i32.load8_u offset=8 (local.get $i)))))
  (func $hot (export "hot") (param $n i32) (result i32)
    (local $t i32) (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $v16 i32) (local $v17 i32) (local $v18 i32) (local $v19 i32) (local $v20 i32) (local $v21 i32) (local $v22 i32) (local $v23 i32) (local $v24 i32) (local $v25 i32)
    (loop $l
      (local.set $t (call $rd (i32.and (local.get $n) (i32.const 3))))
      (local.set $v0 (i32.add (i32.rotl (local.get $v0) (i32.const 1)) (i32.add (local.get $t) (i32.const 0))))
      (local.set $v1 (i32.add (i32.rotl (local.get $v1) (i32.const 1)) (i32.add (local.get $t) (i32.const 1))))
      (local.set $v2 (i32.add (i32.rotl (local.get $v2) (i32.const 1)) (i32.add (local.get $t) (i32.const 2))))
      (local.set $v3 (i32.add (i32.rotl (local.get $v3) (i32.const 1)) (i32.add (local.get $t) (i32.const 3))))
      (local.set $v4 (i32.add (i32.rotl (local.get $v4) (i32.const 1)) (i32.add (local.get $t) (i32.const 4))))
      (local.set $v5 (i32.add (i32.rotl (local.get $v5) (i32.const 1)) (i32.add (local.get $t) (i32.const 5))))
      (local.set $v6 (i32.add (i32.rotl (local.get $v6) (i32.const 1)) (i32.add (local.get $t) (i32.const 6))))
      (local.set $v7 (i32.add (i32.rotl (local.get $v7) (i32.const 1)) (i32.add (local.get $t) (i32.const 7))))
      (local.set $v8 (i32.add (i32.rotl (local.get $v8) (i32.const 1)) (i32.add (local.get $t) (i32.const 8))))
      (local.set $v9 (i32.add (i32.rotl (local.get $v9) (i32.const 1)) (i32.add (local.get $t) (i32.const 9))))
      (local.set $v10 (i32.add (i32.rotl (local.get $v10) (i32.const 1)) (i32.add (local.get $t) (i32.const 10))))
      (local.set $v11 (i32.add (i32.rotl (local.get $v11) (i32.const 1)) (i32.add (local.get $t) (i32.const 11))))
      (local.set $v12 (i32.add (i32.rotl (local.get $v12) (i32.const 1)) (i32.add (local.get $t) (i32.const 12))))
      (local.set $v13 (i32.add (i32.rotl (local.get $v13) (i32.const 1)) (i32.add (local.get $t) (i32.const 13))))
      (local.set $v14 (i32.add (i32.rotl (local.get $v14) (i32.const 1)) (i32.add (local.get $t) (i32.const 14))))
      (local.set $v15 (i32.add (i32.rotl (local.get $v15) (i32.const 1)) (i32.add (local.get $t) (i32.const 15))))
      (local.set $v16 (i32.add (i32.rotl (local.get $v16) (i32.const 1)) (i32.add (local.get $t) (i32.const 16))))
      (local.set $v17 (i32.add (i32.rotl (local.get $v17) (i32.const 1)) (i32.add (local.get $t) (i32.const 17))))
      (local.set $v18 (i32.add (i32.rotl (local.get $v18) (i32.const 1)) (i32.add (local.get $t) (i32.const 18))))
      (local.set $v19 (i32.add (i32.rotl (local.get $v19) (i32.const 1)) (i32.add (local.get $t) (i32.const 19))))
      (local.set $v20 (i32.add (i32.rotl (local.get $v20) (i32.const 1)) (i32.add (local.get $t) (i32.const 20))))
      (local.set $v21 (i32.add (i32.rotl (local.get $v21) (i32.const 1)) (i32.add (local.get $t) (i32.const 21))))
      (local.set $v22 (i32.add (i32.rotl (local.get $v22) (i32.const 1)) (i32.add (local.get $t) (i32.const 22))))
      (local.set $v23 (i32.add (i32.rotl (local.get $v23) (i32.const 1)) (i32.add (local.get $t) (i32.const 23))))
      (local.set $v24 (i32.add (i32.rotl (local.get $v24) (i32.const 1)) (i32.add (local.get $t) (i32.const 24))))
      (local.set $v25 (i32.add (i32.rotl (local.get $v25) (i32.const 1)) (i32.add (local.get $t) (i32.const 25))))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v0) (local.get $v1)) (i32.xor (local.get $v2) (local.get $v3))) (i32.xor (i32.xor (local.get $v4) (local.get $v5)) (i32.xor (local.get $v6) (local.get $v7)))) (i32.xor (i32.xor (i32.xor (local.get $v8) (local.get $v9)) (i32.xor (local.get $v10) (local.get $v11))) (i32.xor (i32.xor (local.get $v12) (local.get $v13)) (i32.xor (local.get $v14) (local.get $v15))))) (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v16) (local.get $v17)) (i32.xor (local.get $v18) (local.get $v19))) (i32.xor (i32.xor (local.get $v20) (local.get $v21)) (i32.xor (local.get $v22) (local.get $v23)))) (i32.xor (local.get $v24) (local.get $v25)))))
)
