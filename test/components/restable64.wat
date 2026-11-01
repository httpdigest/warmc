;; The three canonical built-ins over a resource whose representation is an
;; i64 (the component model's 64-bit feature, cm64), run.
;;
;; [[restable]] next door does the same with i32 representations, which is
;; what every other fixture has, so a table that kept 32 bits of one passed
;; all of them.  These representations need every bit: one has bit 32 set,
;; one the whole upper half, one only the top bit.  Each way of losing the
;; upper half comes out as a different digit:
;;
;;   resource.new truncating     -- rep(b) reads back 9, the hundreds go
;;   resource.rep truncating     -- the same, and so do the 100000s
;;   the destructor getting 32
;;   bits, or the handle         -- seen is not 0x100000007, the 10000s go
;;   the handles themselves      -- a and c are both 1, and b 2, as with i32
;;
;; So 111111 is the only answer that means all of it is right, and wasmtime
;; computes it from the same bytes.

(component
  (core module $store
    (global $seen (mut i64) (i64.const 0))
    (func (export "dtor") (param $rep i64)
      (global.set $seen (i64.add (global.get $seen) (local.get $rep))))
    (func (export "seen") (result i64) (global.get $seen)))
  (core instance $s (instantiate $store))

  (type $r (resource (rep i64) (dtor (core func $s "dtor"))))
  (core func $new (canon resource.new $r))
  (core func $drop (canon resource.drop $r))
  (core func $rep (canon resource.rep $r))

  (core module $use
    (import "res" "new" (func $new (param i64) (result i32)))
    (import "res" "rep" (func $rep (param i32) (result i64)))
    (import "res" "drop" (func $drop (param i32)))
    (import "res" "seen" (func $seen (result i64)))
    (func (export "run") (result i32)
      (local $a i32) (local $b i32) (local $c i32)
      (local.set $a (call $new (i64.const 0x100000007)))
      (local.set $b (call $new (i64.const 0xffffffff00000009)))
      (call $drop (local.get $a))
      (local.set $c (call $new (i64.const 0x800000000000000b)))
      (i32.add
        (i32.add
          (i32.eq (local.get $a) (i32.const 1))
          (i32.mul (i32.eq (local.get $b) (i32.const 2)) (i32.const 10)))
        (i32.add
          (i32.add
            (i32.mul (i64.eq (call $rep (local.get $b))
                             (i64.const 0xffffffff00000009))
                     (i32.const 100))
            (i32.mul (i32.eq (local.get $c) (i32.const 1)) (i32.const 1000)))
          (i32.add
            (i32.mul (i64.eq (call $seen) (i64.const 0x100000007))
                     (i32.const 10000))
            (i32.mul (i64.eq (call $rep (local.get $c))
                             (i64.const 0x800000000000000b))
                     (i32.const 100000)))))))

  (core instance $u (instantiate $use
    (with "res" (instance
      (export "new" (func $new))
      (export "rep" (func $rep))
      (export "drop" (func $drop))
      (export "seen" (func $s "seen"))))))

  (func (export "run") (result u32) (canon lift (core func $u "run")))
)
