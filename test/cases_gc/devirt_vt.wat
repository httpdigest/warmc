;; The vtable rung of the devirtualizer, tested from both sides.
;;
;; A call_ref through a struct field becomes a direct call only when every
;; creation site in the module stores the same function in that field, unioned
;; over the receiver type's whole subtype cone.  Each export that must
;; devirtualize has a twin the pass must refuse: a slot two functions reach, a
;; mutable slot flipped after creation, a pair of structurally identical types
;; whose instances the type system cannot tell apart, and a defaulted instance
;; whose null must still trap.  The answers are asserted, so a wrong target is
;; a failure and not a faster wrong program.
;;
;; Every struct type here carries a distinguishing dummy field: structurally
;; identical types are the same wasm type, the analysis poisons them, and only
;; $dupa/$dupb are meant to collide.
(module
  (type $mth (func (param i32) (result i32)))
  (type $vt  (sub (struct (field (ref null $mth)))))
  (type $vt2 (sub $vt (struct (field (ref null $mth)) (field (ref null $mth)))))
  (type $obj (struct (field (ref $vt)) (field i32)))

  (type $pv (struct (field (ref null $mth)) (field i32)))
  (type $nv (struct (field (ref null $mth)) (field i64)))
  (type $mv (struct (field (mut (ref null $mth))) (field f32)))

  (type $dupa (struct (field (ref null $mth)) (field f64)))
  (type $dupb (struct (field (ref null $mth)) (field f64)))

  (func $double (type $mth) (i32.mul (local.get 0) (i32.const 2)))
  (func $triple (type $mth) (i32.mul (local.get 0) (i32.const 3)))
  (elem declare func $double $triple)

  ;; every $vt-cone instance holds $double in field 0: monomorphic
  (global $v1 (ref $vt)  (struct.new $vt  (ref.func $double)))
  (global $v2 (ref $vt2) (struct.new $vt2 (ref.func $double) (ref.func $triple)))
  (global $o1 (ref $obj) (struct.new $obj (global.get $v1) (i32.const 21)))
  (global $o2 (ref $obj) (struct.new $obj (global.get $v2) (i32.const 14)))

  ;; two functions reach $pv's slot: polymorphic
  (global $p1 (ref $pv) (struct.new $pv (ref.func $double) (i32.const 0)))
  (global $p2 (ref $pv) (struct.new $pv (ref.func $triple) (i32.const 0)))
  (global $sel (mut i32) (i32.const 1))

  ;; one function and one default: monomorphic but nullable
  (global $n1 (ref $nv) (struct.new $nv (ref.func $double) (i64.const 0)))
  (global $n2 (ref $nv) (struct.new_default $nv))

  ;; same shape, different index, different function: the same wasm type
  (global $da (ref $dupa) (struct.new $dupa (ref.func $double) (f64.const 0)))
  (global $db (ref $dupb) (struct.new $dupb (ref.func $triple) (f64.const 0)))

  (func $check (param i32 i32)
    (if (i32.ne (local.get 0) (local.get 1)) (then unreachable)))

  (func (export "vt_mono")
    (call $check
      (call_ref $mth (struct.get $obj 1 (global.get $o1))
                     (struct.get $vt 0 (struct.get $obj 0 (global.get $o1))))
      (i32.const 42)))

  (func (export "vt_sub")
    (call $check
      (call_ref $mth (struct.get $obj 1 (global.get $o2))
        (struct.get $vt2 1
          (ref.cast (ref $vt2) (struct.get $obj 0 (global.get $o2)))))
      (i32.const 42)))

  (func (export "vt_poly")
    (call $check
      (call_ref $mth (i32.const 10)
        (struct.get $pv 0
          (if (result (ref $pv)) (global.get $sel)
            (then (global.get $p2)) (else (global.get $p1)))))
      (i32.const 30)))

  (func (export "vt_nullslot_ok")
    (call $check
      (call_ref $mth (i32.const 8) (struct.get $nv 0 (global.get $n1)))
      (i32.const 16)))

  (func (export "vt_nulltrap")
    (drop (call_ref $mth (i32.const 8) (struct.get $nv 0 (global.get $n2)))))

  (func (export "vt_mut")
    (local $m (ref $mv))
    (local.set $m (struct.new $mv (ref.func $double) (f32.const 0)))
    (struct.set $mv 0 (local.get $m) (ref.func $triple))
    (call $check
      (call_ref $mth (i32.const 5) (struct.get $mv 0 (local.get $m)))
      (i32.const 15)))

  ;; a $dupb instance seen through a (ref $dupa)-typed read: only the
  ;; duplicate-type poisoning keeps this call honest
  (func (export "vt_dup")
    (local $x (ref $dupa))
    (local.set $x (global.get $db))
    (call $check
      (call_ref $mth (i32.const 7) (struct.get $dupa 0 (local.get $x)))
      (i32.const 21)))
)
