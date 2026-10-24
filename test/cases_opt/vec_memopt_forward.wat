;; A loop of a fixed 16 trips is vectorised and then flattened, so its
;; vector stores stand in straight-line code beside a scalar store and load
;; of the same word: memopt forwarded the scalar store's 77 to the load past
;; the vector stores that overwrote it.  A vector store writes memory as the
;; bulk operations do.
;; run: f => i32:2
(module
  (memory 1)
  (data (i32.const 1024) "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00\07\00\00\00\08\00\00\00")
  ;; dst[0] = 77; for i in 0..16: dst[i] = src[i] + 1; return dst[0]
  ;; expected 2 (src[0] + 1)
  (func (export "f") (result i32)
    (local $i i32)
    (i32.store (i32.const 0) (i32.const 77))
    (loop $l
      (i32.store (i32.shl (local.get $i) (i32.const 2))
        (i32.add (i32.load offset=1024 (i32.shl (local.get $i) (i32.const 2))) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 16))))
    (i32.load (i32.const 0)))
)
