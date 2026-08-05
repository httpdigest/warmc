;; Reading a directory across several calls.
;;
;; fd_readdir takes a cookie and is expected to resume from it, so the cookie
;; logic counts entries from the beginning of the stream every time.  warmc
;; opens the stream with fdopendir on a dup of the guest's descriptor, which
;; shares the open file description and therefore the offset -- and libc pulls
;; a whole block ahead, so by the time the stream is closed the shared offset
;; sits far past the last entry reported.  The next call then starts wherever
;; that left it and the count comes out short.  A directory small enough to
;; fit one buffer never resumes, which is why every other case here passes.
;;
;; The buffer is deliberately smaller than the directory so the walk takes
;; several calls, and what is printed is a count and a total rather than the
;; names, because two engines reading two different directories need not
;; return them in the same order.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_readdir"
    (func $readdir (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close"
    (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "rd0")

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $putn (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x260))
    (i32.store8 (local.get $p) (i32.const 10))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (call $puts (local.get $p)
                (i32.sub (i32.const 0x261) (local.get $p))))

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func (export "_start")
    (local $i i32) (local $e i32)
    (local $cookie i64) (local $got i32) (local $at i32)
    (local $namlen i32) (local $entries i32) (local $bytes i32)

    ;; Eight more names than the directory came with, so no buffer below
    ;; holds them all.
    (local.set $i (i32.const 0))
    (loop $mk
      (i32.store8 (i32.const 0x102)
                  (i32.add (i32.const 48) (local.get $i)))
      (local.set $e (call $open (i32.const 3) (i32.const 0) (i32.const 0x100)
                          (i32.const 3) (i32.const 1)
                          (i64.const 0x2000042) (i64.const 0x2000042)
                          (i32.const 0) (i32.const 0x304)))
      (if (local.get $e) (then (call $die (i32.const 20))))
      (drop (call $close (i32.load (i32.const 0x304))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $i) (i32.const 8))))

    ;; Walk the whole directory in 64-byte bites.
    (block $done
      (loop $call
        (if (call $readdir (i32.const 3) (i32.const 0x800) (i32.const 64)
                  (local.get $cookie) (i32.const 0x300))
          (then (call $die (i32.const 21))))
        (local.set $got (i32.load (i32.const 0x300)))
        (br_if $done (i32.lt_u (local.get $got) (i32.const 24)))
        (local.set $at (i32.const 0))
        (block $chunk
          (loop $walk
            (br_if $chunk
              (i32.gt_u (i32.add (local.get $at) (i32.const 24))
                        (local.get $got)))
            (local.set $namlen
              (i32.load (i32.add (i32.const 0x800)
                                 (i32.add (local.get $at) (i32.const 16)))))
            (br_if $chunk
              (i32.gt_u (i32.add (local.get $at)
                                 (i32.add (i32.const 24) (local.get $namlen)))
                        (local.get $got)))
            (local.set $cookie
              (i64.load (i32.add (i32.const 0x800) (local.get $at))))
            (local.set $entries (i32.add (local.get $entries) (i32.const 1)))
            (local.set $bytes (i32.add (local.get $bytes) (local.get $namlen)))
            (local.set $at
              (i32.add (local.get $at)
                       (i32.add (i32.const 24) (local.get $namlen))))
            (br $walk)))
        ;; Nothing whole came back, so the buffer cannot hold even one entry.
        (br_if $done (i32.eqz (local.get $at)))
        (br $call)))

    (call $putn (local.get $entries))
    (call $putn (local.get $bytes))))
