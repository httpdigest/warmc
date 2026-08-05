;; The core module a guest toolchain would produce for the world beside it:
;; the mangled export names, cabi_realloc, and the post-return for the export
;; that returns a string.
(module
  (import "example:calc/log@0.1.0" "write" (func $write (param i32 i32)))
  (memory (export "memory") 1)
  (func (export "example:calc/ops@0.1.0#add") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.add)
  (func (export "example:calc/ops@0.1.0#area") (param i32 i32 i32) (result i32)
    local.get 0)
  (func (export "example:calc/ops@0.1.0#name") (result i32) i32.const 0)
  (func (export "cabi_post_example:calc/ops@0.1.0#name") (param i32))
  (func (export "cabi_realloc") (param i32 i32 i32 i32) (result i32)
    i32.const 8))
