;; Effects of a call_ref on a struct field: the function the reference is
;; to sets the field, so its value is not forwarded across the call from
;; before it (opt.c ref_effects, as callref_effects.wat).
;; run: f 5 => i32:42
(module
  (type $s (struct (field (mut i32))))
  (type $t (func (param (ref $s))))
  (table 1 funcref)
  (elem (i32.const 0) $nop)
  (elem declare func $setter)
  (global $h (mut (ref null $t)) (ref.func $setter))
  (func $nop)
  (func $setter (type $t) (param $o (ref $s))
    (struct.set $s 0 (local.get $o) (i32.const 42)))
  (func (export "f") (param $v i32) (result i32)
    (local $o (ref null $s))
    (local.set $o (struct.new $s (local.get $v)))
    (drop (struct.get $s 0 (local.get $o)))
    (call_ref $t (ref.as_non_null (local.get $o)) (global.get $h))
    (struct.get $s 0 (local.get $o)))
)
