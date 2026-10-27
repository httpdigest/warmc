;; Catches what test/link/exnthru_a.wat throws, through exnthru_b.wat
(module
  (import "A" "t" (tag $t (param i32)))
  (import "B" "mid" (func $mid (param i32) (result i32)))
  (func (export "catch") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $t $h) (drop (call $mid (local.get 0))))
      (i32.const -2))))
