;; Inline segments, which run_wat.py holds byte for byte to wasm-tools.
;;
;; A table's (elem ...) is written with the table named, as the
;; abbreviation's expansion is; its offset is a constant of the table's
;; address type, and its items have the table's element type -- function
;; indices too, which become ref.func expressions where that type is not
;; funcref.  An item is (item ...) or one folded instruction, whose operands
;; come first.  A memory's (data ...) has an offset of the memory's address
;; type.  The 64-bit forms, the typed indices and the folded items were all
;; assembled wrong.
(module
  (type $t (func))
  (type $a (array i8))
  (func $f (type $t))
  (table funcref (elem $f $f))
  (table i64 funcref (elem $f))
  (table i64 funcref (elem (ref.func $f) (item ref.null func)))
  (table (ref null $t) (elem $f))
  (table (ref null $a)
    (elem (array.new $a (i32.const 7) (i32.const 3))
          (item (array.new_fixed $a 1 (i32.const 1)))
          (item i32.const 2 i32.const 4 array.new $a)))
  (table funcref (elem))
  (elem $e (ref $a) (array.new $a (i32.const 1) (i32.const 2)))
  (memory (data "a" "b"))
  (memory i64 (data "c")))
