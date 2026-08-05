;; Six of the preview1 calls no test entered.
;;
;; A trace of every host call across both preview1 suites -- nineteen modules
;; here and twelve Go programs at three optimisation levels -- enters 37 of the
;; 46 functions the host binds.  fd_tell, fd_datasync, fd_advise,
;; fd_filestat_set_times, sched_yield and proc_raise were among the nine it did
;; not, and two of those had their behaviour changed by the commit that last
;; touched this host: fd_datasync stopped being an alias for fd_sync, and
;; sched_yield was left a no-op deliberately.  Nothing covered either.
;;
;; TinyGo reaches none of them -- Go has no fadvise, its wasip1 os.Chtimes is
;; ENOSYS, and runtime.Gosched compiles to nothing on a single-threaded target
;; -- so a hand-written module is the only way in.
;;
;; The other three, fd_allocate, fd_renumber and an advice outside the enum,
;; are in [[fdextra]] instead: wasmtime 47 does not implement the first two and
;; traps on the third, so they cannot be asked here.
;;
;; Every number printed is an errno, so a mapping that is wrong but not zero
;; comes out as a different number rather than as a silent pass.  In order:
;; open, tell at 0 and the offset it reported, write, tell again and its
;; offset, datasync, the six defined advices, filestat_set_times, filestat_get
;; and the mtime it left in whole seconds, sched_yield, proc_raise, close.
(module
  (import "wasi_snapshot_preview1" "path_open"
          (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_tell" (func $tell (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_datasync" (func $datasync (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_advise" (func $advise (param i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_get" (func $fstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_set_times"
          (func $settimes (param i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sched_yield" (func $yield (result i32)))
  (import "wasi_snapshot_preview1" "proc_raise" (func $raise (param i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "fdops.txt")
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
    (local $fd i32) (local $i i32)

    (call $emiti
      (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                  (i32.const 9) (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                  (i32.const 0) (i32.const 0x300)))
    (local.set $fd (i32.load (i32.const 0x300)))

    (call $emiti (call $tell (local.get $fd) (i32.const 0x310)))
    (call $emit (i64.load (i32.const 0x310)))
    (i32.store (i32.const 0x200) (i32.const 0x110))
    (i32.store (i32.const 0x204) (i32.const 10))
    (call $emiti (call $write (local.get $fd) (i32.const 0x200) (i32.const 1)
                              (i32.const 0x20c)))
    (call $emiti (call $tell (local.get $fd) (i32.const 0x310)))
    (call $emit (i64.load (i32.const 0x310)))

    (call $emiti (call $datasync (local.get $fd)))

    (local.set $i (i32.const 0))
    (loop $a
      (call $emiti (call $advise (local.get $fd) (i64.const 0) (i64.const 4)
                                 (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $a (i32.lt_u (local.get $i) (i32.const 6))))

    ;; Both stamps written explicitly, then read back in whole seconds: a host
    ;; that put the marker for "now" in the wrong field of the timespec would
    ;; answer this call 0 and leave a stamp from 1969.
    (call $emiti (call $settimes (local.get $fd) (i64.const 1000000000)
                                 (i64.const 2000000000) (i32.const 5)))
    (call $emiti (call $fstat (local.get $fd) (i32.const 0x400)))
    (call $emit (i64.div_u (i64.load (i32.const 0x430)) (i64.const 1000000000)))

    (call $emiti (call $yield))
    (call $emiti (call $raise (i32.const 2)))
    (call $emiti (call $close (local.get $fd)))

    (i32.store8 (global.get $out) (i32.const 10))
    (i32.store (i32.const 0x200) (i32.const 0x800))
    (i32.store (i32.const 0x204)
               (i32.sub (i32.add (global.get $out) (i32.const 1))
                        (i32.const 0x800)))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c)))))
