;; As licm_cast_arraylen.wat, for br_on_cast_fail: the length is read in
;; the arm where the operand is the array, and not before the branch that
;; says so.
;; run: f 0 10 => i32:45
;; run: f 200 10 => i32:75
(module
  (type $arr (array (mut i32)))
  (func (export "f") (param $p i32) (param $n i32) (result i32)
    (local $x (ref any)) (local $i i32) (local $s i32)
    (local.set $x (if (result (ref any)) (i32.gt_u (local.get $p) (i32.const 100)) (then (array.new $arr (i32.const 1) (i32.const 3))) (else (ref.i31 (local.get $p)))))
    (loop $l
      (block $no (result (ref any))
        (local.set $s (i32.add (local.get $s)
          (array.len (br_on_cast_fail $no (ref any) (ref $arr) (local.get $x)))))
        (ref.i31 (i32.const 0)))
      (drop)
      (local.set $s (i32.add (local.get $s) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
