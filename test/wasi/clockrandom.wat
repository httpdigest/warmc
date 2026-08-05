;; The two calls whose *values* cannot be compared between engines, checked by
;; the properties they promise instead: the monotonic clock does not go
;; backwards, and random_get fills the buffer it was given rather than leaving
;; it as it found it.
;;
;; The random check is probabilistic and deliberately weak -- sixteen zero
;; bytes is 2^-128 from a working generator and certain from a broken one, so
;; it catches "did nothing" without pretending to judge the distribution.
(module
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "random_get"
    (func $random (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func $byte (param $c i32)
    (i32.store8 (i32.const 16) (local.get $c))
    (i32.store8 (i32.const 17) (i32.const 10))
    (i32.store (i32.const 0) (i32.const 16))
    (i32.store (i32.const 4) (i32.const 2))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8))))
  (func (export "_start") (local $any i32)
    ;; clock id 1 is monotonic; read it twice and check it did not go back.
    (drop (call $clock (i32.const 1) (i64.const 1000) (i32.const 64)))
    (drop (call $clock (i32.const 1) (i64.const 1000) (i32.const 72)))
    (call $byte (select (i32.const 121) (i32.const 110)
                        (i64.ge_u (i64.load (i32.const 72))
                                  (i64.load (i32.const 64)))))
    ;; Sixteen zero bytes, then ask for entropy and see if any changed.
    (i64.store (i32.const 128) (i64.const 0))
    (i64.store (i32.const 136) (i64.const 0))
    (drop (call $random (i32.const 128) (i32.const 16)))
    (local.set $any
      (i32.or (i64.ne (i64.load (i32.const 128)) (i64.const 0))
              (i64.ne (i64.load (i32.const 136)) (i64.const 0))))
    (call $byte (select (i32.const 121) (i32.const 110) (local.get $any)))))
