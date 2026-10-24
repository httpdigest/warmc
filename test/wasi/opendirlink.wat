;; Opening a directory through a link to it, following the link, as
;; wasi-libc's opendir does: Linux answers O_NOFOLLOW | O_DIRECTORY on a link
;; with ENOTDIR rather than ELOOP, and the host has to take that for a link
;; to follow too -- while a plain file opened as a directory, link or not,
;; stays ENOTDIR.  Every right but fd_write is asked for, so that a
;; directory opens.  Prints each open's errno and, for one that opened, the
;; file type the descriptor has; diffed against wasmtime.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_create_directory"
    (func $mkdir (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_get" (func $fdstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x400) "d")
  (data (i32.const 0x410) "dl")
  (data (i32.const 0x420) "f")
  (data (i32.const 0x430) "fl")

  (func $num (param $v i32)
    (local $n i32)
    (loop $lp
      (i32.store8 (i32.sub (i32.const 0x1ff) (local.get $n))
                  (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $lp (local.get $v)))
    (i32.store8 (i32.const 0x200) (i32.const 10))
    (i32.store (i32.const 0x300) (i32.sub (i32.const 0x200) (local.get $n)))
    (i32.store (i32.const 0x304) (i32.add (local.get $n) (i32.const 1)))
    (drop (call $write (i32.const 1) (i32.const 0x300) (i32.const 1) (i32.const 0x308))))

  ;; open with these flags, print the errno, and the file type when it opened
  (func $try (param $dirflags i32) (param $p i32) (param $n i32) (param $oflags i32)
    (local $e i32)
    (local.set $e (call $open (i32.const 3) (local.get $dirflags) (local.get $p) (local.get $n)
                              (local.get $oflags) (i64.const 0x1fffffbf) (i64.const 0x1fffffbf)
                              (i32.const 0) (i32.const 0x310)))
    (call $num (local.get $e))
    (if (i32.eqz (local.get $e))
      (then
        (drop (call $fdstat (i32.load (i32.const 0x310)) (i32.const 0x320)))
        (call $num (i32.load8_u (i32.const 0x320)))
        (drop (call $close (i32.load (i32.const 0x310)))))))

  (func (export "_start")
    (call $num (call $mkdir (i32.const 3) (i32.const 0x400) (i32.const 1)))
    (call $num (call $symlink (i32.const 0x400) (i32.const 1) (i32.const 3) (i32.const 0x410) (i32.const 2)))
    ;; a file f, and fl -> f
    (call $try (i32.const 0) (i32.const 0x420) (i32.const 1) (i32.const 1))
    (call $num (call $symlink (i32.const 0x420) (i32.const 1) (i32.const 3) (i32.const 0x430) (i32.const 2)))
    ;; dl as a directory, following it
    (call $try (i32.const 1) (i32.const 0x410) (i32.const 2) (i32.const 2))
    ;; dl, following it, not asking for a directory
    (call $try (i32.const 1) (i32.const 0x410) (i32.const 2) (i32.const 0))
    ;; f and fl as a directory, following
    (call $try (i32.const 1) (i32.const 0x420) (i32.const 1) (i32.const 2))
    (call $try (i32.const 1) (i32.const 0x430) (i32.const 2) (i32.const 2))))
