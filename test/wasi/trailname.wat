;; Names that end in a dot or a space are names like any other: `x.` is not
;; the file `x`, and `.. ` is not the parent.  Win32 strips a component's
;; trailing dots and spaces from a path without the \\?\ prefix, so there
;; `y.` made the file `y`, and a walk that counted `.. ` as an ordinary name
;; while Win32 went up by it reached past the preopen: `.. /trailname.secret`
;; read the file beside it.  Each step prints its errno, 0 for success, and
;; the whole is diffed against wasmtime, the tree left behind included.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_create_directory"
    (func $mkdir (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get"
    (func $filestat (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read"
    (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 1024) "x")
  (data (i32.const 1040) "x.")
  (data (i32.const 1056) "x ")
  (data (i32.const 1072) "y.")
  (data (i32.const 1088) "y")
  (data (i32.const 1104) "z ")
  (data (i32.const 1120) "z")
  (data (i32.const 1136) "sub")
  (data (i32.const 1152) "sub/.. ")
  (data (i32.const 1168) "sub/...")
  (data (i32.const 1184) ".. /trailname.secret")
  (data (i32.const 1216) "sub/.. /.. /trailname.secret")
  (data (i32.const 1248) ". ")
  (data (i32.const 1264) "sub/. ./x")

  ;; the errno as decimal and a newline
  (func $num (param $v i32)
    (local $n i32)
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
  (func $openp (param $p i32) (param $n i32) (param $oflags i32) (result i32)
    (local $e i32)
    (local.set $e (call $open (i32.const 3) (i32.const 0) (local.get $p) (local.get $n)
                              (local.get $oflags) (i64.const 0x1fffffbf) (i64.const 0x1fffffbf)
                              (i32.const 0) (i32.const 0x310)))
    (if (i32.eqz (local.get $e)) (then (drop (call $close (i32.load (i32.const 0x310))))))
    (local.get $e))

  ;; a file made by path_open's O_CREAT
  (func $put (param $p i32) (param $n i32) (result i32)
    (call $openp (local.get $p) (local.get $n) (i32.const 1)))

  (func $stat (param $p i32) (param $n i32) (result i32)
    (call $filestat (i32.const 3) (i32.const 0) (local.get $p) (local.get $n) (i32.const 0x320)))

  ;; open for reading and print the errno, then what was read, if anything
  (func $steal (param $p i32) (param $n i32)
    (local $e i32) (local $fd i32)
    (local.set $e (call $open (i32.const 3) (i32.const 0) (local.get $p) (local.get $n)
                              (i32.const 0) (i64.const 2) (i64.const 0)
                              (i32.const 0) (i32.const 0x310)))
    (call $num (local.get $e))
    (if (i32.eqz (local.get $e))
      (then
        (local.set $fd (i32.load (i32.const 0x310)))
        (i32.store (i32.const 0x340) (i32.const 0x400))
        (i32.store (i32.const 0x344) (i32.const 64))
        (drop (call $read (local.get $fd) (i32.const 0x340) (i32.const 1) (i32.const 0x348)))
        (i32.store (i32.const 0x344) (i32.load (i32.const 0x348)))
        (drop (call $write (i32.const 1) (i32.const 0x340) (i32.const 1) (i32.const 0x348)))
        (drop (call $close (local.get $fd))))))

  (func (export "_start")
    ;; a file x
    (call $num (call $put (i32.const 1024) (i32.const 1)))
    ;; x. and x  are not x
    (call $num (call $stat (i32.const 1040) (i32.const 2)))
    (call $num (call $stat (i32.const 1056) (i32.const 2)))
    ;; y. is made as y., and y is not there
    (call $num (call $put (i32.const 1072) (i32.const 2)))
    (call $num (call $stat (i32.const 1088) (i32.const 1)))
    (call $num (call $stat (i32.const 1072) (i32.const 2)))
    ;; z  likewise
    (call $num (call $put (i32.const 1104) (i32.const 2)))
    (call $num (call $stat (i32.const 1120) (i32.const 1)))
    ;; a directory, and the names in it that only look like its parent
    (call $num (call $mkdir (i32.const 3) (i32.const 1136) (i32.const 3)))
    (call $num (call $stat (i32.const 1152) (i32.const 7)))
    (call $num (call $stat (i32.const 1168) (i32.const 7)))
    (call $num (call $stat (i32.const 1248) (i32.const 2)))
    (call $num (call $stat (i32.const 1264) (i32.const 9)))
    ;; the file beside the preopen, by .. with a space after it
    (call $steal (i32.const 1184) (i32.const 20))
    (call $steal (i32.const 1216) (i32.const 28))))
