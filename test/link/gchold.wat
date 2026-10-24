;; A reference the embedder holds across a call that traps
;; (test/link/gcholddriver.c): make hands out a struct, boom traps,
;; churn allocates enough to collect, and read reads the struct back.
(module
  (type $s (struct (field (mut i64))))
  (type $junk (array (mut i64)))
  (func (export "make") (param i64) (result (ref null $s)) (struct.new $s (local.get 0)))
  (func (export "boom") unreachable)
  (func (export "churn") (param $n i32)
    (local $i i32)
    (loop $l
      (drop (array.new_default $junk (i32.const 64)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
  (func (export "read") (param (ref null $s)) (result i64) (struct.get $s 0 (local.get 0))))
