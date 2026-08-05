;; fd_write to both standard descriptors.
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 16) "out\n")
  (data (i32.const 32) "err\n")
  (func $say (param $ptr i32) (param $len i32) (param $fd i32)
    (i32.store (i32.const 0) (local.get $ptr))
    (i32.store (i32.const 4) (local.get $len))
    (drop (call $fd_write (local.get $fd) (i32.const 0) (i32.const 1) (i32.const 8))))
  (func (export "_start")
    (call $say (i32.const 16) (i32.const 4) (i32.const 1))
    (call $say (i32.const 32) (i32.const 4) (i32.const 2))))
