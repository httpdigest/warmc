;; A nullable cast and the read of a field of what it casts, in a module
;; that lets no reference cross its boundary: the cast traps on null as the
;; read would (codegen.c seal_tests), so the code tests the object for null
;; once, and the read does not again -- nor a ref.as_non_null between them.
;; shape: get cbz\s+x=1
;; shape: get_nn cbz\s+x=1
;; and the cast branches to its trap itself, making no 0 or 1 to test
;; shape: get cbz\s+w=0
;; run: get 1 => i32:5
;; run: get 0 => trap: null reference
;; run: get 2 => trap: cast failure
;; run: get_nn 1 => i32:6
;; run: get_nn 0 => trap: null reference
;; run: get_nn 2 => trap: cast failure
(module
  (type $s (struct (field i32)))
  (type $t (struct (field f64)))
  (global $g (mut anyref) (ref.null any))
  (func $put (param $k i32)
    (if (i32.eq (local.get $k) (i32.const 1))
      (then (global.set $g (struct.new $s (i32.const 5)))))
    (if (i32.eq (local.get $k) (i32.const 2))
      (then (global.set $g (struct.new $t (f64.const 1))))))
  (func $get (export "get") (param $k i32) (result i32)
    (call $put (local.get $k))
    (struct.get $s 0 (ref.cast (ref null $s) (global.get $g))))
  (func $get_nn (export "get_nn") (param $k i32) (result i32)
    (call $put (local.get $k))
    (i32.add (i32.const 1)
      (struct.get $s 0 (ref.as_non_null (ref.cast (ref null $s) (global.get $g))))))
)
