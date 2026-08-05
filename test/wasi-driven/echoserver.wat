;; sock_accept, sock_recv, sock_send and sock_shutdown over a preopened
;; listening socket.
;;
;; All four used to return ENOTSOCK unconditionally, which was honest only
;; because no descriptor could ever be a socket: there was no way to hand the
;; guest one.  --tcplisten=ADDR:PORT seeds the fd table with a listener, so the
;; four calls have something to work on.
;;
;; This module is driven by run_wasi.sh rather than by the plain case runner:
;; it needs someone to connect to it.  It accepts one connection, echoes the
;; bytes back with an "echo: " prefix, shuts the write side down and exits.
;; The listener is fd 3 because no directory is preopened alongside it.
;;
;; There is no wasmtime side to diff against.  wasmtime 47 answers
;; `-S tcplisten=` with "components do not support --tcplisten", and the
;; `-Spreview2=n` that used to select the wasip1 host it did support is gone,
;; so the expected reply below is written out rather than taken from a second
;; engine.

(module
  (import "wasi_snapshot_preview1" "sock_accept"
    (func $accept (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_recv"
    (func $recv (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send"
    (func $send (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_shutdown"
    (func $shutdown (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_get"
    (func $fdstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "echo: ")

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func (export "_start")
    (local $conn i32) (local $got i32)

    ;; The listener has to report itself as a stream socket, filetype 6.
    (drop (call $fdstat (i32.const 3) (i32.const 0x400)))
    (if (i32.ne (i32.load8_u (i32.const 0x400)) (i32.const 6))
      (then (call $die (i32.const 30))))

    (if (call $accept (i32.const 3) (i32.const 0) (i32.const 0x300))
      (then (call $die (i32.const 31))))
    (local.set $conn (i32.load (i32.const 0x300)))

    ;; One recv into a single iovec at 0x500.
    (i32.store (i32.const 0x200) (i32.const 0x500))
    (i32.store (i32.const 0x204) (i32.const 256))
    (if (call $recv (local.get $conn) (i32.const 0x200) (i32.const 1)
                    (i32.const 0) (i32.const 0x308) (i32.const 0x30c))
      (then (call $die (i32.const 32))))
    (local.set $got (i32.load (i32.const 0x308)))

    ;; Send the prefix and the payload back as two iovecs in one call.
    (i32.store (i32.const 0x210) (i32.const 0x100))
    (i32.store (i32.const 0x214) (i32.const 6))
    (i32.store (i32.const 0x218) (i32.const 0x500))
    (i32.store (i32.const 0x21c) (local.get $got))
    (if (call $send (local.get $conn) (i32.const 0x210) (i32.const 2)
                    (i32.const 0) (i32.const 0x310))
      (then (call $die (i32.const 33))))

    ;; Shut the write side down so the peer sees end-of-stream.
    (if (call $shutdown (local.get $conn) (i32.const 2))
      (then (call $die (i32.const 34))))

    ;; And say on stdout how many bytes went each way.
    (i32.store (i32.const 0x220) (i32.const 0x600))
    (i32.store (i32.const 0x224) (i32.const 2))
    (i32.store8 (i32.const 0x600)
                (i32.add (i32.const 48) (local.get $got)))
    (i32.store8 (i32.const 0x601) (i32.const 10))
    (drop (call $write (i32.const 1) (i32.const 0x220) (i32.const 1)
                       (i32.const 0x314)))))
