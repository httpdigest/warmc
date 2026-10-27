;; Calls through: no tag, no type of the collector's, frames of its own to be
;; walked through (test/link/exnthru_a.wat)
(module
  (import "A" "thrower" (func $thrower (param i32)))
  (func (export "mid") (param i32) (result i32)
    (call $thrower (i32.mul (local.get 0) (i32.const 2)))
    (i32.const -1)))
