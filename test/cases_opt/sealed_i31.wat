;; A module that lets no reference cross its boundary and makes an i31 only
;; in a global's initialiser: reach finds it there (compile.c Reach.i31), so
;; the slot test of a cast to a struct type still asks whether the reference
;; is an i31 before it reads a header (isel.c emit_slot_test), and the i31
;; fails the cast instead of being read as an address.
;; stats: slot_tests=2
;; run: cast_s 0 => trap: cast failure
;; run: cast_s 1 => i32:5
;; run: test_s 0 => i32:0
;; run: test_s 1 => i32:1
(module
  (type $s (struct (field i32)))
  (global $i anyref (ref.i31 (i32.const 7)))
  (global $o (mut anyref) (ref.null any))
  (func $pick (param $k i32) (result anyref)
    (if (result anyref) (local.get $k)
      (then
        (global.set $o (struct.new $s (i32.const 5)))
        (global.get $o))
      (else (global.get $i))))
  (func (export "cast_s") (param $k i32) (result i32)
    (struct.get $s 0 (ref.cast (ref $s) (call $pick (local.get $k)))))
  (func (export "test_s") (param $k i32) (result i32)
    (ref.test (ref $s) (call $pick (local.get $k))))
)
