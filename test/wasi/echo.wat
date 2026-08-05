;; fd_read from stdin, then write back exactly what arrived.
;;
;; A short read is end of input rather than an error, and the host has to
;; report the count it actually got: reporting the iovec's length instead
;; would echo whatever was left in the buffer.
(module
  (import "wasi_snapshot_preview1" "fd_read"
    (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "_start") (local $n i32)
    (i32.store (i32.const 0) (i32.const 256))    ;; iovec.base
    (i32.store (i32.const 4) (i32.const 512))    ;; iovec.len
    (drop (call $fd_read (i32.const 0) (i32.const 0) (i32.const 1) (i32.const 8)))
    (local.set $n (i32.load (i32.const 8)))
    (i32.store (i32.const 4) (local.get $n))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 12)))))
