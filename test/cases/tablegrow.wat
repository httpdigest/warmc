;; A table with no maximum grows as far as it is asked: 3000 entries past a
;; minimum of 1, where the runtime's mapping once stopped at 1024 past it,
;; and an entry set far out is called through.
(module
  (table $t 1 funcref)
  (func $f (result i32) (i32.const 7))
  (elem declare func $f)
  (func (export "grow_far") (result i32) (table.grow $t (ref.null func) (i32.const 3000)))
  (func (export "size") (result i32) (table.size $t))
  (func (export "set_call") (result i32)
    (table.set $t (i32.const 2500) (ref.func $f))
    (call_indirect $t (result i32) (i32.const 2500))))
