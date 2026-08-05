;; What a narrowed directory handle can still do.
;;
;; A guest may drop rights from a descriptor with fd_fdstat_set_rights and
;; then hand it on, which is the whole point of the capability model: the
;; holder must not be able to do what the rights say it cannot.  Two of the
;; rights warmc advertises in a directory's set were never consulted, so a
;; handle with path_create_file dropped could still create files and one with
;; path_filestat_set_size dropped could still truncate an existing file to
;; nothing.
;;
;; This case checks itself rather than printing, because it cannot be a
;; differential one: current wasmtime has no rights to narrow and refuses
;; fd_fdstat_set_rights outright.  A runtime that refuses it has nothing to
;; answer for and the case ends; one that accepts it must then enforce what
;; it accepted.  Either way there is no output and the exit status is zero,
;; which is what the harness compares.

(module
  (import "wasi_snapshot_preview1" "fd_fdstat_get"
    (func $fdstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_set_rights"
    (func $setrights (param i32 i64 i64) (result i32)))
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close"
    (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "seed.txt")
  (data (i32.const 0x110) "made.txt")

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  ;; open(path, len, oflags) on the preopen, returning the errno
  (func $try (param $path i32) (param $len i32) (param $oflags i32)
             (result i32)
    (local $e i32)
    (local.set $e
      (call $open (i32.const 3) (i32.const 0) (local.get $path) (local.get $len)
                  (local.get $oflags)
                  (i64.const 0x2000042) (i64.const 0x2000042)
                  (i32.const 0) (i32.const 0x304)))
    (if (i32.eqz (local.get $e))
      (then (drop (call $close (i32.load (i32.const 0x304))))))
    (local.get $e))

  (func (export "_start")
    ;; Whatever the preopen came with, less path_create_file (bit 10) and
    ;; path_filestat_set_size (bit 19).  Reading the set back first keeps this
    ;; independent of which rights the host handed out: set_rights may only
    ;; narrow, and a mask holding a bit the handle does not have is refused.
    (if (call $fdstat (i32.const 3) (i32.const 0x400))
      (then (call $die (i32.const 10))))
    (if (call $setrights (i32.const 3)
          (i64.and (i64.load (i32.const 0x408))
                   (i64.const 0xfffffffffff7fbff))
          (i64.load (i32.const 0x410)))
      (then (return)))

    ;; Creating a file needs path_create_file, which is gone.
    (if (i32.eqz (call $try (i32.const 0x110) (i32.const 8) (i32.const 1)))
      (then (call $die (i32.const 30))))
    ;; Truncating one needs path_filestat_set_size, which is gone.
    (if (i32.eqz (call $try (i32.const 0x100) (i32.const 8) (i32.const 8)))
      (then (call $die (i32.const 31))))
    ;; Opening one is still allowed, so this is not passing by refusing
    ;; everything.
    (if (call $try (i32.const 0x100) (i32.const 8) (i32.const 0))
      (then (call $die (i32.const 32))))))
