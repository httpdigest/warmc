;; The errors of the preopen calls that a program checks for: a buffer too
;; short for a preopen's name is ENAMETOOLONG; the name of a descriptor that
;; is open but no preopen ENOTDIR, of one not open EBADF; and prestat_get of
;; either EBADF.  Each is printed as a decimal errno and diffed against
;; wasmtime.  fd_renumber, which wasmtime does not have, is
;; test/wasi-driven/fdextra.wat's.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_get" (func $pget (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_dir_name" (func $pname (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x400) ".")
  ;; the errno as decimal and a newline
  (func $num (param $v i32)
    (local $n i32) (local $i i32)
    (local.set $n (i32.const 0))
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

  ;; open, and close what it opened
  (func $openp (param $dirflags i32) (param $p i32) (param $n i32) (param $oflags i32) (result i32)
    (local $e i32)
    (local.set $e (call $open (i32.const 3) (local.get $dirflags) (local.get $p) (local.get $n)
                              (local.get $oflags) (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                              (i32.const 0) (i32.const 0x310)))
    (if (i32.eqz (local.get $e)) (then (drop (call $close (i32.load (i32.const 0x310))))))
    (local.get $e))

  (func (export "_start")
    ;; the preopen's name, "/d": a buffer too short, one just long enough,
    ;; and descriptors that are no preopen
    (call $num (call $pname (i32.const 3) (i32.const 0x700) (i32.const 1)))
    (call $num (call $pname (i32.const 3) (i32.const 0x700) (i32.const 2)))
    (call $num (call $pname (i32.const 1) (i32.const 0x700) (i32.const 8)))
    (call $num (call $pget (i32.const 1) (i32.const 0x720)))
    (call $num (call $pget (i32.const 60) (i32.const 0x720)))
    ;; a directory opened from the preopen, which is none itself
    (call $num (call $open (i32.const 3) (i32.const 0) (i32.const 0x400) (i32.const 1) (i32.const 2)
                           (i64.const 0x1fffffbf) (i64.const 0x1fffffbf) (i32.const 0) (i32.const 0x310)))
    (call $num (call $pname (i32.load (i32.const 0x310)) (i32.const 0x700) (i32.const 8)))
    (call $num (call $pget (i32.load (i32.const 0x310)) (i32.const 0x720)))
    (call $num (call $pname (i32.const 60) (i32.const 0x700) (i32.const 8)))))
