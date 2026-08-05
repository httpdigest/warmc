(module
  ;; The devirtualizer's answer must never be observable: every export here
  ;; has a twin the pass cannot touch, and wasm-interp is the referee for
  ;; both.  Table 0 is frozen (module-private, never written), slot 3 is
  ;; deliberately null so the contents fact has a hole to record.
  (table 4 funcref)
  (elem (i32.const 0) $add $mul $sub)

  (type $bin (func (param i32 i32) (result i32)))
  (func $add (type $bin) (i32.add (local.get 0) (local.get 1)))
  (func $mul (type $bin) (i32.mul (local.get 0) (local.get 1)))
  (func $sub (type $bin) (i32.sub (local.get 0) (local.get 1)))
  (elem declare func $add $mul $sub)

  ;; a mutable i32 global keeps the "dynamic" twins dynamic
  (global $gidx (mut i32) (i32.const 1))

  ;; constant index into the frozen table: all three checks discharge
  (func (export "ind_const") (result i32)
    (call_indirect (type $bin) (i32.const 40) (i32.const 2) (i32.const 1)))

  ;; runtime index: bounds, null and signature checks all stay
  (func (export "ind_dyn") (result i32)
    (call_indirect (type $bin) (i32.const 10) (i32.const 3) (global.get $gidx)))

  ;; call_ref on a literal ref.func
  (func (export "ref_direct") (result i32)
    (call_ref $bin (i32.const 7) (i32.const 5) (ref.func $sub)))

  ;; the same, laundered through a local: SSA hands the pass the same def
  (func (export "ref_local") (result i32)
    (local $r (ref null $bin))
    (local.set $r (ref.func $mul))
    (call_ref $bin (i32.const 6) (i32.const 7) (local.get $r)))

  ;; call_ref through an immutable global holding a bare ref.func
  (global $fsub (ref $bin) (ref.func $sub))
  (func (export "ref_glob") (result i32)
    (call_ref $bin (i32.const 9) (i32.const 4) (global.get $fsub)))

  ;; a mutable funcref global must not devirtualize: the answer flips at
  ;; runtime, and only the real lookup sees the flip
  (global $fmut (mut (ref null $bin)) (ref.func $add))
  (func (export "ref_mut") (result i32)
    (global.set $fmut (ref.func $mul))
    (call_ref $bin (i32.const 6) (i32.const 7) (global.get $fmut)))

  ;; a null slot reached with a runtime index still traps like it should
  ;; (kept dynamic so this file stays trap-free at index 0..2)
  (func (export "ind_dyn_add") (result i32)
    (call_indirect (type $bin) (i32.const 20) (i32.const 22)
                   (i32.sub (global.get $gidx) (i32.const 1))))
)
