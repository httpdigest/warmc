;; poll_oneoff with a descriptor that cannot be polled beside a clock of five
;; seconds: the bad one's event, EBADF, is there before any waiting, so the
;; call returns it at once and the clock does not fire.  Exits with the
;; number of events times ten plus the first one's error, 18, and the
;; runner times it.  wasmtime fails the whole call with EBADF instead, where
;; the event's own error field is the place WASI gives it.
(module
  (import "wasi_snapshot_preview1" "poll_oneoff" (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    ;; fd_read on descriptor 99, userdata 7
    (i64.store (i32.const 0) (i64.const 7))
    (i32.store8 (i32.const 8) (i32.const 1))
    (i32.store (i32.const 16) (i32.const 99))
    ;; the monotonic clock, five seconds from now, userdata 9
    (i64.store (i32.const 48) (i64.const 9))
    (i32.store8 (i32.const 56) (i32.const 0))
    (i32.store (i32.const 64) (i32.const 1))
    (i64.store (i32.const 72) (i64.const 5000000000))
    (if (call $poll (i32.const 0) (i32.const 200) (i32.const 2) (i32.const 400))
      (then (call $exit (i32.const 99))))
    (call $exit (i32.add (i32.mul (i32.load (i32.const 400)) (i32.const 10))
                         (i32.load16_u (i32.const 208))))))
