;; fd_write with iovecs that add up to more than 4 GiB: 65537 of the same
;; 64 KiB page, to stdout, which the runner points at /dev/null.  The count
;; comes back in 32 bits, so no more moves than it can say, 2^32 - 1 bytes,
;; where the count wrapped to 65536 once.  Exits 0 when it says that, 1
;; when it says another number, 2 when the call fails.
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 10)
  (func (export "_start")
    (local $i i32)
    ;; the iovecs from 64 KiB on, each the first page
    (loop $l
      (i32.store (i32.add (i32.const 65536) (i32.shl (local.get $i) (i32.const 3))) (i32.const 0))
      (i32.store (i32.add (i32.const 65540) (i32.shl (local.get $i) (i32.const 3))) (i32.const 65536))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 65537))))
    (if (call $write (i32.const 1) (i32.const 65536) (i32.const 65537) (i32.const 8))
      (then (call $exit (i32.const 2))))
    (call $exit (i32.ne (i32.load (i32.const 8)) (i32.const -1)))))
