;; A function whose only use of the instance is the inline type test of a
;; cast (I_REF_SLOT): the slot base the test reads is the instance's, so the
;; function needs the instance register set and kept -- neither lent to a
;; value when registers run short (isel.c isel_spare_gprs) nor left holding
;; what its caller had.  analyse_function once missed the test, and such a
;; function read some other pointer for the base: one with more values live
;; round a loop than there are registers (pressure_cast), whose instance
;; register went to one of them, and one an export tail-calls (tail_cast),
;; entered after the epilogue had put back the register of the export's
;; caller -- the host's, before the runtime set it to the instance there.
;;
;; pressure_cast checks its own answer and traps when it is wrong; tail_cast's
;; answer comes back to test/run_gc.sh to check.  Assembled with wasm-tools.

(module
  (type $a (sub (struct (field i32))))
  (type $b (sub $a (struct (field i32) (field i32))))

  (func $make (param $yes i32) (result (ref null any))
    (if (result (ref null any)) (local.get $yes)
      (then (struct.new $b (i32.const 1) (i32.const 2)))
      (else (struct.new $a (i32.const 1)))))

  ;; n tests of r, counted
  (func $count (param $r (ref null any)) (param $n i32) (result i32)
    (local $k i32)
    (loop $l
      (local.set $k (i32.add (local.get $k) (ref.test (ref $b) (local.get $r))))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (local.get $k))

  (func (export "tail_cast") (param $yes i32) (result i32)
    (return_call $count (call $make (local.get $yes)) (i32.const 3)))

  ;; 26 values carried round a loop that tests r each time, and the same
  ;; with the test's answer given
  (func $pressure (param $r (ref null any)) (param $q (ref null any)) (param $n i32) (result i32)
    (local $t i32) (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $v16 i32) (local $v17 i32) (local $v18 i32) (local $v19 i32) (local $v20 i32) (local $v21 i32) (local $v22 i32) (local $v23 i32) (local $v24 i32) (local $v25 i32)
    (loop $l
      (local.set $t (ref.test (ref $b)
        (select (result (ref null any)) (local.get $r) (local.get $q)
          (i32.and (local.get $n) (i32.const 1)))))
      (local.set $v0 (i32.add (local.get $v0) (i32.add (local.get $t) (i32.const 0))))
      (local.set $v1 (i32.add (local.get $v1) (i32.add (local.get $t) (i32.const 1))))
      (local.set $v2 (i32.add (local.get $v2) (i32.add (local.get $t) (i32.const 2))))
      (local.set $v3 (i32.add (local.get $v3) (i32.add (local.get $t) (i32.const 3))))
      (local.set $v4 (i32.add (local.get $v4) (i32.add (local.get $t) (i32.const 4))))
      (local.set $v5 (i32.add (local.get $v5) (i32.add (local.get $t) (i32.const 5))))
      (local.set $v6 (i32.add (local.get $v6) (i32.add (local.get $t) (i32.const 6))))
      (local.set $v7 (i32.add (local.get $v7) (i32.add (local.get $t) (i32.const 7))))
      (local.set $v8 (i32.add (local.get $v8) (i32.add (local.get $t) (i32.const 8))))
      (local.set $v9 (i32.add (local.get $v9) (i32.add (local.get $t) (i32.const 9))))
      (local.set $v10 (i32.add (local.get $v10) (i32.add (local.get $t) (i32.const 10))))
      (local.set $v11 (i32.add (local.get $v11) (i32.add (local.get $t) (i32.const 11))))
      (local.set $v12 (i32.add (local.get $v12) (i32.add (local.get $t) (i32.const 12))))
      (local.set $v13 (i32.add (local.get $v13) (i32.add (local.get $t) (i32.const 13))))
      (local.set $v14 (i32.add (local.get $v14) (i32.add (local.get $t) (i32.const 14))))
      (local.set $v15 (i32.add (local.get $v15) (i32.add (local.get $t) (i32.const 15))))
      (local.set $v16 (i32.add (local.get $v16) (i32.add (local.get $t) (i32.const 16))))
      (local.set $v17 (i32.add (local.get $v17) (i32.add (local.get $t) (i32.const 17))))
      (local.set $v18 (i32.add (local.get $v18) (i32.add (local.get $t) (i32.const 18))))
      (local.set $v19 (i32.add (local.get $v19) (i32.add (local.get $t) (i32.const 19))))
      (local.set $v20 (i32.add (local.get $v20) (i32.add (local.get $t) (i32.const 20))))
      (local.set $v21 (i32.add (local.get $v21) (i32.add (local.get $t) (i32.const 21))))
      (local.set $v22 (i32.add (local.get $v22) (i32.add (local.get $t) (i32.const 22))))
      (local.set $v23 (i32.add (local.get $v23) (i32.add (local.get $t) (i32.const 23))))
      (local.set $v24 (i32.add (local.get $v24) (i32.add (local.get $t) (i32.const 24))))
      (local.set $v25 (i32.add (local.get $v25) (i32.add (local.get $t) (i32.const 25))))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v0) (local.get $v1)) (i32.xor (local.get $v2) (local.get $v3))) (i32.xor (i32.xor (local.get $v4) (local.get $v5)) (i32.xor (local.get $v6) (local.get $v7)))) (i32.xor (i32.xor (i32.xor (local.get $v8) (local.get $v9)) (i32.xor (local.get $v10) (local.get $v11))) (i32.xor (i32.xor (local.get $v12) (local.get $v13)) (i32.xor (local.get $v14) (local.get $v15))))) (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v16) (local.get $v17)) (i32.xor (local.get $v18) (local.get $v19))) (i32.xor (i32.xor (local.get $v20) (local.get $v21)) (i32.xor (local.get $v22) (local.get $v23)))) (i32.xor (local.get $v24) (local.get $v25)))))
  (func $pressure_want (param $r i32) (param $q i32) (param $n i32) (result i32)
    (local $t i32) (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $v16 i32) (local $v17 i32) (local $v18 i32) (local $v19 i32) (local $v20 i32) (local $v21 i32) (local $v22 i32) (local $v23 i32) (local $v24 i32) (local $v25 i32)
    (loop $l
      (local.set $t (select (local.get $r) (local.get $q) (i32.and (local.get $n) (i32.const 1))))
      (local.set $v0 (i32.add (local.get $v0) (i32.add (local.get $t) (i32.const 0))))
      (local.set $v1 (i32.add (local.get $v1) (i32.add (local.get $t) (i32.const 1))))
      (local.set $v2 (i32.add (local.get $v2) (i32.add (local.get $t) (i32.const 2))))
      (local.set $v3 (i32.add (local.get $v3) (i32.add (local.get $t) (i32.const 3))))
      (local.set $v4 (i32.add (local.get $v4) (i32.add (local.get $t) (i32.const 4))))
      (local.set $v5 (i32.add (local.get $v5) (i32.add (local.get $t) (i32.const 5))))
      (local.set $v6 (i32.add (local.get $v6) (i32.add (local.get $t) (i32.const 6))))
      (local.set $v7 (i32.add (local.get $v7) (i32.add (local.get $t) (i32.const 7))))
      (local.set $v8 (i32.add (local.get $v8) (i32.add (local.get $t) (i32.const 8))))
      (local.set $v9 (i32.add (local.get $v9) (i32.add (local.get $t) (i32.const 9))))
      (local.set $v10 (i32.add (local.get $v10) (i32.add (local.get $t) (i32.const 10))))
      (local.set $v11 (i32.add (local.get $v11) (i32.add (local.get $t) (i32.const 11))))
      (local.set $v12 (i32.add (local.get $v12) (i32.add (local.get $t) (i32.const 12))))
      (local.set $v13 (i32.add (local.get $v13) (i32.add (local.get $t) (i32.const 13))))
      (local.set $v14 (i32.add (local.get $v14) (i32.add (local.get $t) (i32.const 14))))
      (local.set $v15 (i32.add (local.get $v15) (i32.add (local.get $t) (i32.const 15))))
      (local.set $v16 (i32.add (local.get $v16) (i32.add (local.get $t) (i32.const 16))))
      (local.set $v17 (i32.add (local.get $v17) (i32.add (local.get $t) (i32.const 17))))
      (local.set $v18 (i32.add (local.get $v18) (i32.add (local.get $t) (i32.const 18))))
      (local.set $v19 (i32.add (local.get $v19) (i32.add (local.get $t) (i32.const 19))))
      (local.set $v20 (i32.add (local.get $v20) (i32.add (local.get $t) (i32.const 20))))
      (local.set $v21 (i32.add (local.get $v21) (i32.add (local.get $t) (i32.const 21))))
      (local.set $v22 (i32.add (local.get $v22) (i32.add (local.get $t) (i32.const 22))))
      (local.set $v23 (i32.add (local.get $v23) (i32.add (local.get $t) (i32.const 23))))
      (local.set $v24 (i32.add (local.get $v24) (i32.add (local.get $t) (i32.const 24))))
      (local.set $v25 (i32.add (local.get $v25) (i32.add (local.get $t) (i32.const 25))))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v0) (local.get $v1)) (i32.xor (local.get $v2) (local.get $v3))) (i32.xor (i32.xor (local.get $v4) (local.get $v5)) (i32.xor (local.get $v6) (local.get $v7)))) (i32.xor (i32.xor (i32.xor (local.get $v8) (local.get $v9)) (i32.xor (local.get $v10) (local.get $v11))) (i32.xor (i32.xor (local.get $v12) (local.get $v13)) (i32.xor (local.get $v14) (local.get $v15))))) (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v16) (local.get $v17)) (i32.xor (local.get $v18) (local.get $v19))) (i32.xor (i32.xor (local.get $v20) (local.get $v21)) (i32.xor (local.get $v22) (local.get $v23)))) (i32.xor (local.get $v24) (local.get $v25)))))

  (func (export "pressure_cast") (param $yes i32)
    (if (i32.ne (call $pressure (call $make (local.get $yes)) (call $make (i32.const 1))
                               (i32.const 50))
                (call $pressure_want (local.get $yes) (i32.const 1) (i32.const 50)))
      (then (unreachable))))
)
