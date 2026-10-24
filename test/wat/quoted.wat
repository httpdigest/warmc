;; Identifiers and an annotation's id written as strings: $"a b" names what
;; no plain identifier can, $"plain" is $plain, and (@"custom" ...) is a
;; custom section as (@custom ...) is.
(module
  (@"custom" "quoted" "x")
  (func $"a b" (result i32) (i32.const 7))
  (func $plain (result i32) (call $"a b"))
  (func (export "f") (result i32) (call $"plain")))
