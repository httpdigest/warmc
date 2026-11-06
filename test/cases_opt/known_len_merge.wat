;; An array merged from arrays of different lengths (opt.c array_known_len,
;; array_exact_len): through a block parameter, the least of the lengths
;; its ways in give is a lower bound -- what a proof that an index is in
;; bounds may take -- and not the length: array.len stays a read of it
;; (lenmerge: x of 4 or of 8 elements), and a read at k traps past x's own
;; length, not past the least (getmerge).
;; run: lenmerge 1 => i32:4
;; run: lenmerge 0 => i32:8
;; run: getmerge 1 3 => i32:3
;; run: getmerge 1 4 => trap: out of bounds array access
;; run: getmerge 0 7 => i32:5
;; run: getmerge 0 8 => trap: out of bounds array access
(module
  (type $a (array (mut i32)))
  (func (export "lenmerge") (param $c i32) (result i32)
    (local $x (ref null $a))
    (if (local.get $c)
      (then (local.set $x (array.new_default $a (i32.const 4))))
      (else (local.set $x (array.new_default $a (i32.const 8)))))
    (array.len (ref.as_non_null (local.get $x))))
  (func (export "getmerge") (param $c i32) (param $k i32) (result i32)
    (local $x (ref null $a))
    (if (local.get $c)
      (then (local.set $x (array.new $a (i32.const 3) (i32.const 4))))
      (else (local.set $x (array.new $a (i32.const 5) (i32.const 8)))))
    (array.get $a (ref.as_non_null (local.get $x)) (local.get $k)))
)
