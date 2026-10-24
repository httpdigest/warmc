;; As licm_cast_arraylen.wat, for struct.get: the field of a cast's result
;; is read only where the cast's test passed.
;; run: f 0 0 10 => i32:45
;; run: f 0 3 10 => trap: cast failure
(module
  (type $st (struct (field i32) (field i64)))
  (func (export "f") (param $p i32) (param $k i32) (param $n i32) (result i32)
    (local $x (ref any)) (local $i i32) (local $s i32)
    (local.set $x (ref.i31 (local.get $p)))
    (loop $l
      (if (i32.eq (local.get $i) (i32.sub (local.get $k) (i32.const 1)))
        (then (local.set $s (i32.add (local.get $s)
                (struct.get $st 0 (ref.cast (ref $st) (local.get $x)))))))
      (local.set $s (i32.add (local.get $s) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
