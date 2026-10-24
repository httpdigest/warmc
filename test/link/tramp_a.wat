;; The module test/link/tramp_b.wat imports its global from: what peek
;; reads is the global as B's trampolined functions last wrote it.
(module
  (global $g (export "g") (mut i32) (i32.const 0))
  (func (export "peek") (result i32) (global.get $g)))
