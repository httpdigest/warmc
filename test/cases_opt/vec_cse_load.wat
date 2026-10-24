;; Two flattened vector loops, an in-place increment and then a copy: CSE
;; took the copy's vector loads for the increment's, which ran before its
;; vector stores.  A vector load has no effect, but its value is memory's,
;; not its operands' alone (ir_is_pure, ir_has_no_effect).
;; run: f => i32:302
(module
  (memory 1)
  (data (i32.const 1024) "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00\07\00\00\00\08\00\00\00")
  ;; src[i] += 1 for 16 elements, then dst[i] = src[i] for 16 elements; return dst[0]
  (func (export "f") (result i32)
    (local $i i32)
    (loop $l
      (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2))
        (i32.add (i32.load offset=1024 (i32.shl (local.get $i) (i32.const 2))) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 16))))
    (local.set $i (i32.const 0))
    (loop $m
      (i32.store (i32.shl (local.get $i) (i32.const 2))
        (i32.load offset=1024 (i32.shl (local.get $i) (i32.const 2))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $m (i32.lt_u (local.get $i) (i32.const 16))))
    (i32.add (i32.load (i32.const 0)) (i32.mul (i32.load (i32.const 4)) (i32.const 100))))
)
