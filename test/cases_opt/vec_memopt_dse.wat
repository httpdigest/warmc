;; As vec_memopt_forward.wat, with a vector load between two scalar stores:
;; memopt took the first store for dead, overwritten before anything read
;; it, though the vector load read it.
;; run: f => i32:78
(module
  (memory 1)
  ;; dst[0] = 77; for i in 0..16: dst2[i] = dst[i] + 1; dst[0] = 5; return dst2[0]  (expected 78)
  (func (export "f") (result i32)
    (local $i i32)
    (i32.store (i32.const 0) (i32.const 77))
    (loop $l
      (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2))
        (i32.add (i32.load (i32.shl (local.get $i) (i32.const 2))) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 16))))
    (i32.store (i32.const 0) (i32.const 5))
    (i32.load (i32.const 1024)))
)
