;; A start function that calls WASI: the host binds its functions as the
;; module is made, not after, so the call that runs before _start reaches
;; fd_write rather than a stub.  Its output comes first, then _start's.
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $w (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "from start\0a")
  (data (i32.const 80) "from _start\0a")
  (func $say (param $at i32) (param $n i32)
    (i32.store (i32.const 0) (local.get $at))
    (i32.store (i32.const 4) (local.get $n))
    (drop (call $w (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8))))
  (func $init (call $say (i32.const 64) (i32.const 11)))
  (start $init)
  (func (export "_start") (call $say (i32.const 80) (i32.const 12))))
