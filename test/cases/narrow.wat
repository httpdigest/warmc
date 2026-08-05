(module
  ;; A narrowing store hands its value on only where truncation is a no-op.
  ;;
  ;; `i32.store8 p, v` writes one byte, so `v` is not what a later load of `p`
  ;; sees -- unless the bits it drops were already zero, which is the shape a
  ;; producer emits for `u8 c = buf[i];` and which is worth forwarding.  Every
  ;; export here is a case where the two differ: a value with bits above the
  ;; width, a signed reload of the same byte, and a wider load over a byte
  ;; that was written into it.
  (memory 1)
  (func (export "wide_store8") (result i32)
    (i32.store8 (i32.const 0) (i32.const 0x1FF))
    (i32.load8_u (i32.const 0)))
  (func (export "wide_store16") (result i32)
    (i32.store16 (i32.const 4) (i32.const 0x3FFFF))
    (i32.load16_u (i32.const 4)))
  (func $narrow_ok (export "narrow_ok") (param i32) (result i32)
    (i32.store8 (i32.const 8) (i32.load8_u (local.get 0)))
    (i32.load8_u (i32.const 8)))
  (func (export "signed_reload") (result i32)
    (i32.store8 (i32.const 12) (i32.const 0x80))
    (i32.load8_s (i32.const 12)))
  (func (export "store8_load32") (result i32)
    (i32.store (i32.const 16) (i32.const -1))
    (i32.store8 (i32.const 16) (i32.const 0x7F))
    (i32.load (i32.const 16)))
  (func (export "i64_store32") (result i64)
    (i64.store32 (i32.const 24) (i64.const 0x1FFFFFFFF))
    (i64.load32_u (i32.const 24)))
  ;; narrow_ok takes a parameter, and the oracle here is
  ;; `wasm-interp --run-all-exports`, which skips every export that does --
  ;; so it was compiled and never run.  This calls it.
  (func (export "narrow_ok_t") (result i32)
    (i32.store8 (i32.const 32) (i32.const 0xa5))
    (call $narrow_ok (i32.const 32))))
