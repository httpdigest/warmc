;; fd_allocate, fd_renumber, and an advice outside the enum.
;;
;; The other six calls nothing had entered are in [[fdops]], where wasmtime
;; answers them and the case is differential.  These three it does not:
;;
;;   fd_allocate  -- wasmtime 47 answers ENOTSUP(58) and leaves the file the
;;                   length it was.  warmc emulates it with ftruncate, so the
;;                   file grows.  preview1 defines the call; the reference
;;                   serves preview1 through the preview2 adapter now, and
;;                   wasi:filesystem has nothing to build it out of.
;;   fd_renumber  -- wasmtime 47 answers EBADF(8) for the same reason: there is
;;                   no renumber in preview2 to adapt.  warmc moves the
;;                   descriptor and closes the old number, which is what
;;                   preview1 says.
;;   fd_advise    -- with an advice outside the six the enum defines, wasmtime
;;                   traps while converting the argument.  warmc answers
;;                   EINVAL(28).  A guest that sends a seventh advice is
;;                   broken either way; returning an errno lets it find out.
;;
;; So this is golden, for the same reason [[cpuclock]] is: a differential case
;; would pin warmc to the reference's gaps rather than to preview1.  What is
;; printed, in order: allocate, filestat_get, the size it left, advise with a
;; seventh value, renumber, a read through the new number, its length, the
;; first byte it read, tell on the old number, renumber past the host's
;; ceiling, and close.
(module
  (import "wasi_snapshot_preview1" "path_open"
          (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_seek" (func $seek (param i32 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_tell" (func $tell (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_advise" (func $advise (param i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_allocate" (func $allocate (param i32 i64 i64) (result i32)))
  (import "wasi_snapshot_preview1" "fd_renumber" (func $renumber (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_get" (func $fstat (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "fdextra.txt")
  (data (i32.const 0x110) "0123456789")

  (global $out (mut i32) (i32.const 0x800))

  (func $emit (param $v i64)
    (local $p i32)
    (local.set $p (i32.const 0x7f0))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48)
                 (i32.wrap_i64 (i64.rem_u (local.get $v) (i64.const 10)))))
      (local.set $v (i64.div_u (local.get $v) (i64.const 10)))
      (br_if $d (i64.ne (local.get $v) (i64.const 0))))
    (loop $c
      (i32.store8 (global.get $out) (i32.load8_u (local.get $p)))
      (global.set $out (i32.add (global.get $out) (i32.const 1)))
      (local.set $p (i32.add (local.get $p) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $p) (i32.const 0x7f0))))
    (i32.store8 (global.get $out) (i32.const 32))
    (global.set $out (i32.add (global.get $out) (i32.const 1))))

  (func $emiti (param $v i32) (call $emit (i64.extend_i32_u (local.get $v))))

  (func (export "_start")
    (local $fd i32)

    (drop (call $open (i32.const 3) (i32.const 0) (i32.const 0x100)
                      (i32.const 11) (i32.const 9) (i64.const 0x1fffffff)
                      (i64.const 0x1fffffff) (i32.const 0) (i32.const 0x300)))
    (local.set $fd (i32.load (i32.const 0x300)))
    (i32.store (i32.const 0x200) (i32.const 0x110))
    (i32.store (i32.const 0x204) (i32.const 10))
    (drop (call $write (local.get $fd) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c)))

    (call $emiti (call $allocate (local.get $fd) (i64.const 10) (i64.const 22)))
    (call $emiti (call $fstat (local.get $fd) (i32.const 0x400)))
    (call $emit (i64.load (i32.const 0x420)))

    (call $emiti (call $advise (local.get $fd) (i64.const 0) (i64.const 4)
                               (i32.const 6)))

    (call $emiti (call $renumber (local.get $fd) (i32.const 9)))
    (drop (call $seek (i32.const 9) (i64.const 0) (i32.const 0) (i32.const 0x310)))
    (i32.store (i32.const 0x200) (i32.const 0x500))
    (i32.store (i32.const 0x204) (i32.const 4))
    (call $emiti (call $read (i32.const 9) (i32.const 0x200) (i32.const 1)
                             (i32.const 0x20c)))
    (call $emiti (i32.load (i32.const 0x20c)))
    (call $emiti (i32.load8_u (i32.const 0x500)))
    (call $emiti (call $tell (local.get $fd) (i32.const 0x310)))
    (call $emiti (call $renumber (i32.const 9) (i32.const 1000000)))
    (call $emiti (call $close (i32.const 9)))

    (i32.store8 (global.get $out) (i32.const 10))
    (i32.store (i32.const 0x200) (i32.const 0x800))
    (i32.store (i32.const 0x204)
               (i32.sub (i32.add (global.get $out) (i32.const 1))
                        (i32.const 0x800)))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c)))))
