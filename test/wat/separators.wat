;; '_' between the digits of every part a number has, and in a \u{...}
;; escape; an annotation in front of the module, which leaves the custom
;; one inside it a module field.
(@ignored "before the module")
(module
  (@custom "inside" "\u{1_F600}")
  (memory 1)
  (data (i32.const 0) "\u{1_F600}\u{4_1}")
  (func (export "f") (result f64)
    (drop (i32.const 0x1_0))
    (drop (i32.const 1_000_000))
    (drop (i64.const -0xFFFF_FFFF))
    (drop (f32.const nan:0x1_0))
    (drop (f32.const 0x1_f.8_0p1_0))
    (f64.const 1_0.2_5e-1_0)))
