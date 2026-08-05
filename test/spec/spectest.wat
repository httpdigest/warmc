;; The host module the upstream suite imports from.  In a JS embedding these
;; are host functions; here the module that provides them can simply be wasm,
;; because nothing in the suite observes what `print` does -- only that it can
;; be imported and called with the right signature.
(module
  (func (export "print"))
  (func (export "print_i32") (param i32))
  (func (export "print_i64") (param i64))
  (func (export "print_f32") (param f32))
  (func (export "print_f64") (param f64))
  (func (export "print_i32_f32") (param i32 f32))
  (func (export "print_f64_f64") (param f64 f64))

  ;; The values the reference interpreter's `spectest` module defines, and the
  ;; float ones are 666.6 rather than 666: imports.wast reads them back and
  ;; checks the bits.  Nothing noticed while that file failed to build at all.
  (global (export "global_i32") i32 (i32.const 666))
  (global (export "global_i64") i64 (i64.const 666))
  (global (export "global_f32") f32 (f32.const 666.6))
  (global (export "global_f64") f64 (f64.const 666.6))

  (table (export "table") 10 20 funcref)
  (memory (export "memory") 1 2)

  ;; The 64-bit-index pair.  `table64.wast` imports `table64` and fails to
  ;; instantiate without it; the memory is here for symmetry with the
  ;; reference interpreter's own spectest, which defines both.
  (table (export "table64") i64 10 20 funcref)
  (memory (export "memory64") i64 1 2)
)
