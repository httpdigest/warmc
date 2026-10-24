;; A GC array's bounds check taken off by a counted loop's test (opt.c
;; gc_counted_cond): x walks 0, 1, 2, ... from the header, and x <u b holds
;; on the side of x != b only when the side of x == b leaves the loop.  In
;; f both sides go on round it, so x passes b and the read past the end
;; must trap; g is the form the rule is for.
;; run: f 0 => i32:0
;; run: f 1 => i32:3
;; run: f 4 => trap: out of bounds array access
;; run: f 100 => trap: out of bounds array access
;; run: g 0 => i32:0
;; run: g 5 => i32:15
(module
  (type $arr (array (mut i32)))
  ;; a has length $b; the loop runs x = 0 .. 2b-1 and reads a[x] whenever
  ;; x != b, so x = b + 1 is out of bounds and must trap
  (func (export "f") (param $b i32) (result i32)
    (local $a (ref null $arr)) (local $x i32) (local $s i32)
    (local.set $a (array.new $arr (i32.const 3) (local.get $b)))
    (loop $l
      (if (i32.ne (local.get $x) (local.get $b))
        (then (local.set $s (i32.add (local.get $s)
                (array.get $arr (local.get $a) (local.get $x))))))
      (local.set $x (i32.add (local.get $x) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $x) (i32.mul (local.get $b) (i32.const 2)))))
    (local.get $s))
  ;; the form the rule is for: x == b leaves the loop, so x stays below b
  ;; and the read needs no bounds check -- a[x] = 3 for x below b
  (func (export "g") (param $b i32) (result i32)
    (local $a (ref null $arr)) (local $x i32) (local $s i32)
    (local.set $a (array.new $arr (i32.const 3) (local.get $b)))
    (block $out
      (loop $l
        (br_if $out (i32.eq (local.get $x) (local.get $b)))
        (local.set $s (i32.add (local.get $s) (array.get $arr (local.get $a) (local.get $x))))
        (local.set $x (i32.add (local.get $x) (i32.const 1)))
        (br $l)))
    (local.get $s))
)
