;; fd_fdstat_set_flags, which used to accept every flag and keep only one.
;;
;; The host stored a single `append` bool, so APPEND round-tripped and the
;; other four preview1 fdflags were read, returned ESUCCESS, and dropped on the
;; floor: nothing reached the host fd and fd_fdstat_get had nothing to report
;; back.  A guest that sets NONBLOCK and then spins on fd_read blocked forever,
;; having been told the fd was non-blocking.
;;
;; Printed: the fdflags a fresh open has, then the rc and the fdflags after
;; setting APPEND, then the same after setting NONBLOCK.  Both engines have to
;; agree, and the last number is the one that used to be zero.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_get"
    (func $fdstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_set_flags"
    (func $setfl (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x100) "seed.txt")

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $putn (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x290))
    (i32.store8 (local.get $p) (i32.const 10))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (call $puts (local.get $p) (i32.sub (i32.const 0x291) (local.get $p))))

  ;; fs_flags out of an fd_fdstat_t, which lives at offset 2.
  (func $flags (param $fd i32) (result i32)
    (drop (call $fdstat (local.get $fd) (i32.const 0x400)))
    (i32.load16_u (i32.const 0x402)))

  (func (export "_start")
    (local $fd i32)
    ;; fd_read | fd_seek | fd_fdstat_set_flags | fd_write
    (if (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 8)
                    (i32.const 0) (i64.const 78) (i64.const 78)
                    (i32.const 0) (i32.const 0x300))
      (then (call $exit (i32.const 21)) (unreachable)))
    (local.set $fd (i32.load (i32.const 0x300)))

    (call $putn (call $flags (local.get $fd)))
    (call $putn (call $setfl (local.get $fd) (i32.const 1)))   ;; APPEND
    (call $putn (call $flags (local.get $fd)))
    (call $putn (call $setfl (local.get $fd) (i32.const 4)))   ;; NONBLOCK
    (call $putn (call $flags (local.get $fd)))))
