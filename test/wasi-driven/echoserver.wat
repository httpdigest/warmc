;; sock_accept, sock_recv, sock_send and sock_shutdown over a preopened
;; listening socket.
;;
;; All four used to return ENOTSOCK unconditionally, which was honest only
;; because no descriptor could ever be a socket: there was no way to hand the
;; guest one.  --tcplisten=ADDR:PORT seeds the fd table with a listener, so the
;; four calls have something to work on.
;;
;; This module is driven by run_wasi.sh rather than by the plain case runner:
;; it needs someone to connect to it.  The runner passes --tcplisten before
;; --dir, and the listener is still fd 4, after the directory at 3: wasi-libc
;; and Go look for preopens from 3 up and stop at the first descriptor that is
;; none.  The listener is a stream socket to fd_filestat_get as well as to
;; fd_fdstat_get, and non-blocking, as Go's runtime needs to poll it rather
;; than wait in sock_accept with every goroutine stopped.
;;
;; Before anyone can connect -- the driver waits for "up" on stdout -- a
;; sock_accept says EAGAIN.  Then two connections, each answered with
;; "ready", then the bytes the peer sends
;; back with an "echo: " prefix, then the write side shut down:
;;   1. waited for in poll_oneoff, accepted blocking: the recv waits for the
;;      peer, which sends only a while after "ready" -- on macOS a connection
;;      took its listener's O_NONBLOCK, and the recv failed EAGAIN;
;;   2. accepted non-blocking from a listener made blocking again: the recv
;;      before "ready" finds nothing and says EAGAIN, then poll_oneoff waits
;;      for the peer.
;; Then it says on stdout how many bytes came each time, after the "up".  The exit status
;; names the first step that went wrong.
;;
;; There is no wasmtime side to diff against.  wasmtime 47 answers
;; `-S tcplisten=` with "components do not support --tcplisten", and the
;; `-Spreview2=n` that used to select the wasip1 host it did support is gone,
;; so the expected reply is written out in the runner rather than taken from
;; a second engine.

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
  (import "wasi_snapshot_preview1" "fd_fdstat_set_flags"
    (func $setfl (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_get"
    (func $filestat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_get"
    (func $prestat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close"
    (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "echo: ")
  (data (i32.const 0x108) "ready")
  (data (i32.const 0x10e) "up\n")

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  ;; fs_flags out of an fd_fdstat_t, which lives at offset 2.
  (func $flags (param $fd i32) (result i32)
    (if (call $fdstat (local.get $fd) (i32.const 0x400))
      (then (call $die (i32.const 49))))
    (i32.load16_u (i32.const 0x402)))

  ;; Whether fd is readable: one subscription, no clock, so this waits for it.
  (func $readable (param $fd i32) (result i32)
    (i64.store (i32.const 0x700) (i64.const 5))
    (i32.store8 (i32.const 0x708) (i32.const 1))
    (i32.store (i32.const 0x710) (local.get $fd))
    (if (call $poll (i32.const 0x700) (i32.const 0x740) (i32.const 1) (i32.const 0x760))
      (then (return (i32.const 0))))
    (i32.and
      (i32.and (i32.eq (i32.load (i32.const 0x760)) (i32.const 1))
               (i64.eq (i64.load (i32.const 0x740)) (i64.const 5)))
      (i32.and (i32.eqz (i32.load16_u (i32.const 0x748)))
               (i32.eq (i32.load8_u (i32.const 0x74a)) (i32.const 1)))))

  ;; One sock_send of len bytes at p.
  (func $say (param $conn i32) (param $p i32) (param $len i32) (result i32)
    (i32.store (i32.const 0x230) (local.get $p))
    (i32.store (i32.const 0x234) (local.get $len))
    (call $send (local.get $conn) (i32.const 0x230) (i32.const 1)
                (i32.const 0) (i32.const 0x310)))

  ;; One sock_recv into the buffer at 0x500: its rc, the count at 0x308.
  (func $hear (param $conn i32) (result i32)
    (i32.store (i32.const 0x200) (i32.const 0x500))
    (i32.store (i32.const 0x204) (i32.const 256))
    (call $recv (local.get $conn) (i32.const 0x200) (i32.const 1)
                (i32.const 0) (i32.const 0x308) (i32.const 0x30c)))

  ;; The prefix and the got bytes at 0x500 back as two iovecs in one call,
  ;; then the write side shut down so the peer sees end-of-stream.
  (func $answer (param $conn i32) (param $got i32) (param $code i32)
    (i32.store (i32.const 0x210) (i32.const 0x100))
    (i32.store (i32.const 0x214) (i32.const 6))
    (i32.store (i32.const 0x218) (i32.const 0x500))
    (i32.store (i32.const 0x21c) (local.get $got))
    (if (call $send (local.get $conn) (i32.const 0x210) (i32.const 2)
                    (i32.const 0) (i32.const 0x310))
      (then (call $die (local.get $code))))
    (if (call $shutdown (local.get $conn) (i32.const 2))
      (then (call $die (i32.add (local.get $code) (i32.const 1)))))
    (drop (call $close (local.get $conn))))

  (func (export "_start")
    (local $conn i32) (local $got1 i32) (local $got2 i32)

    ;; The directory is 3, the listener 4 and no preopen.
    (if (call $prestat (i32.const 3) (i32.const 0x480))
      (then (call $die (i32.const 35))))
    (if (i32.ne (call $prestat (i32.const 4) (i32.const 0x480)) (i32.const 8))
      (then (call $die (i32.const 36))))

    ;; A stream socket, filetype 6, to both stat calls; non-blocking.
    (drop (call $filestat (i32.const 4) (i32.const 0x440)))
    (if (i32.ne (i32.load8_u (i32.const 0x450)) (i32.const 6))
      (then (call $die (i32.const 37))))
    (if (i32.ne (call $flags (i32.const 4)) (i32.const 4))
      (then (call $die (i32.const 38))))
    (if (i32.ne (i32.load8_u (i32.const 0x400)) (i32.const 6))
      (then (call $die (i32.const 30))))

    ;; Nobody to accept yet, which the driver is then told.
    (if (i32.ne (call $accept (i32.const 4) (i32.const 0) (i32.const 0x300)) (i32.const 6))
      (then (call $die (i32.const 55))))
    (i32.store (i32.const 0x220) (i32.const 0x10e))
    (i32.store (i32.const 0x224) (i32.const 3))
    (drop (call $write (i32.const 1) (i32.const 0x220) (i32.const 1)
                       (i32.const 0x314)))

    ;; 1. Waited for, and accepted blocking.
    (if (i32.eqz (call $readable (i32.const 4)))
      (then (call $die (i32.const 39))))
    (if (call $accept (i32.const 4) (i32.const 0) (i32.const 0x300))
      (then (call $die (i32.const 31))))
    (local.set $conn (i32.load (i32.const 0x300)))
    (if (call $flags (local.get $conn))
      (then (call $die (i32.const 40))))
    (if (call $say (local.get $conn) (i32.const 0x108) (i32.const 5))
      (then (call $die (i32.const 50))))
    (if (call $hear (local.get $conn))
      (then (call $die (i32.const 32))))
    (local.set $got1 (i32.load (i32.const 0x308)))
    (call $answer (local.get $conn) (local.get $got1) (i32.const 33))

    ;; 2. The listener blocking again, a connection accepted non-blocking.
    (if (call $setfl (i32.const 4) (i32.const 0))
      (then (call $die (i32.const 41))))
    (if (call $flags (i32.const 4))
      (then (call $die (i32.const 42))))
    (if (call $accept (i32.const 4) (i32.const 4) (i32.const 0x300))
      (then (call $die (i32.const 43))))
    (local.set $conn (i32.load (i32.const 0x300)))
    (if (i32.ne (call $flags (local.get $conn)) (i32.const 4))
      (then (call $die (i32.const 44))))
    (if (i32.ne (call $hear (local.get $conn)) (i32.const 6))
      (then (call $die (i32.const 45))))
    (if (call $say (local.get $conn) (i32.const 0x108) (i32.const 5))
      (then (call $die (i32.const 46))))
    (if (i32.eqz (call $readable (local.get $conn)))
      (then (call $die (i32.const 47))))
    (if (call $hear (local.get $conn))
      (then (call $die (i32.const 48))))
    (local.set $got2 (i32.load (i32.const 0x308)))
    (call $answer (local.get $conn) (local.get $got2) (i32.const 51))

    ;; And say on stdout how many bytes came each time.
    (i32.store (i32.const 0x220) (i32.const 0x600))
    (i32.store (i32.const 0x224) (i32.const 3))
    (i32.store8 (i32.const 0x600) (i32.add (i32.const 48) (local.get $got1)))
    (i32.store8 (i32.const 0x601) (i32.add (i32.const 48) (local.get $got2)))
    (i32.store8 (i32.const 0x602) (i32.const 10))
    (drop (call $write (i32.const 1) (i32.const 0x220) (i32.const 1)
                       (i32.const 0x314)))))
