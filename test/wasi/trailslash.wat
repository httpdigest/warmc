;; Every path operation of WASI preview 1 on a path that ends in a slash: the
;; leaf before it must be a directory, or a link to one where the operation
;; follows it, and the slash must not make the host follow a link past the
;; preopen.  The setup makes a directory d, a file f, links ld -> d, lf -> f
;; and lx -> x (not there), and a file d/x; each operation then prints its
;; errno, 0 for success, and the whole is diffed against wasmtime, the tree
;; left behind included -- on Windows against what wasmtime gives on Linux,
;; trailslash.linux.out and trailslash.linux.tree, since rt/winposix.c keeps
;; Linux's answers where wasmtime's Windows host has rules of its own.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_create_directory"
    (func $mkdir (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_remove_directory"
    (func $rmdir (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_unlink_file"
    (func $unlink (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_link"
    (func $link (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_rename"
    (func $rename (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get"
    (func $filestat (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_readlink"
    (func $readlinkp (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 1024) "d")
  (data (i32.const 1040) "f")
  (data (i32.const 1056) "ld")
  (data (i32.const 1072) "lf")
  (data (i32.const 1088) "x")
  (data (i32.const 1104) "lx")
  (data (i32.const 1120) "n/")
  (data (i32.const 1136) "n2//")
  (data (i32.const 1152) "d/")
  (data (i32.const 1168) "f/")
  (data (i32.const 1184) "ld/")
  (data (i32.const 1200) "lx/")
  (data (i32.const 1216) "lf/")
  (data (i32.const 1232) "new/")
  (data (i32.const 1248) "s/")
  (data (i32.const 1264) "h/")
  (data (i32.const 1280) "h")
  (data (i32.const 1296) "g")
  (data (i32.const 1312) "g/")
  (data (i32.const 1328) "m")
  (data (i32.const 1344) "k/")
  (data (i32.const 1360) "le")
  (data (i32.const 1376) "d/x/")
  (data (i32.const 1392) "d/x")

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

  ;; a file made by path_open's O_CREAT
  (func $put (param $p i32) (param $n i32) (result i32)
    (call $openp (i32.const 0) (local.get $p) (local.get $n) (i32.const 1)))

  (func $stat (param $flags i32) (param $p i32) (param $n i32) (result i32)
    (call $filestat (i32.const 3) (local.get $flags) (local.get $p) (local.get $n) (i32.const 0x320)))

  (func $readlink (param $p i32) (param $n i32) (result i32)
    (call $readlinkp (i32.const 3) (local.get $p) (local.get $n) (i32.const 0x380) (i32.const 64)
                     (i32.const 0x370)))

  (func (export "_start")
    ;; a directory d
    (call $num (call $mkdir (i32.const 3) (i32.const 1024) (i32.const 1)))
    ;; a file f
    (call $num (call $put (i32.const 1040) (i32.const 1)))
    ;; ld -> d
    (call $num (call $symlink (i32.const 1024) (i32.const 1) (i32.const 3) (i32.const 1056) (i32.const 2)))
    ;; lf -> f
    (call $num (call $symlink (i32.const 1040) (i32.const 1) (i32.const 3) (i32.const 1072) (i32.const 2)))
    ;; lx -> x, which is not there
    (call $num (call $symlink (i32.const 1088) (i32.const 1) (i32.const 3) (i32.const 1104) (i32.const 2)))
    ;; a file under d
    (call $num (call $put (i32.const 1392) (i32.const 3)))
    ;; create n/
    (call $num (call $mkdir (i32.const 3) (i32.const 1120) (i32.const 2)))
    ;; create n2//
    (call $num (call $mkdir (i32.const 3) (i32.const 1136) (i32.const 4)))
    ;; create d/, which is there
    (call $num (call $mkdir (i32.const 3) (i32.const 1152) (i32.const 2)))
    ;; create f/, a file
    (call $num (call $mkdir (i32.const 3) (i32.const 1168) (i32.const 2)))
    ;; create ld/, a link to a directory
    (call $num (call $mkdir (i32.const 3) (i32.const 1184) (i32.const 3)))
    ;; create lx/, a dangling link
    (call $num (call $mkdir (i32.const 3) (i32.const 1200) (i32.const 3)))
    ;; open f/
    (call $num (call $openp (i32.const 0) (i32.const 1168) (i32.const 2) (i32.const 0)))
    ;; open d/
    (call $num (call $openp (i32.const 0) (i32.const 1152) (i32.const 2) (i32.const 0)))
    ;; open ld/
    (call $num (call $openp (i32.const 0) (i32.const 1184) (i32.const 3) (i32.const 0)))
    ;; open ld/ following
    (call $num (call $openp (i32.const 1) (i32.const 1184) (i32.const 3) (i32.const 0)))
    ;; open lf/
    (call $num (call $openp (i32.const 0) (i32.const 1216) (i32.const 3) (i32.const 0)))
    ;; open f/ as a directory
    (call $num (call $openp (i32.const 0) (i32.const 1168) (i32.const 2) (i32.const 2)))
    ;; open new/ creating it
    (call $num (call $openp (i32.const 0) (i32.const 1232) (i32.const 4) (i32.const 1)))
    ;; open d/ creating it
    (call $num (call $openp (i32.const 0) (i32.const 1152) (i32.const 2) (i32.const 1)))
    ;; open lx/ creating it
    (call $num (call $openp (i32.const 0) (i32.const 1200) (i32.const 3) (i32.const 1)))
    ;; stat f/
    (call $num (call $stat (i32.const 0) (i32.const 1168) (i32.const 2)))
    ;; stat d/
    (call $num (call $stat (i32.const 0) (i32.const 1152) (i32.const 2)))
    ;; stat ld/
    (call $num (call $stat (i32.const 0) (i32.const 1184) (i32.const 3)))
    ;; stat lf/
    (call $num (call $stat (i32.const 0) (i32.const 1216) (i32.const 3)))
    ;; stat ld/ following
    (call $num (call $stat (i32.const 1) (i32.const 1184) (i32.const 3)))
    ;; readlink ld/
    (call $num (call $readlink (i32.const 1184) (i32.const 3)))
    ;; readlink lf/
    (call $num (call $readlink (i32.const 1216) (i32.const 3)))
    ;; unlink f/
    (call $num (call $unlink (i32.const 3) (i32.const 1168) (i32.const 2)))
    ;; unlink lf/
    (call $num (call $unlink (i32.const 3) (i32.const 1216) (i32.const 3)))
    ;; unlink ld/
    (call $num (call $unlink (i32.const 3) (i32.const 1184) (i32.const 3)))
    ;; unlink d/
    (call $num (call $unlink (i32.const 3) (i32.const 1152) (i32.const 2)))
    ;; symlink f to s/
    (call $num (call $symlink (i32.const 1040) (i32.const 1) (i32.const 3) (i32.const 1248) (i32.const 2)))
    ;; link f to h/
    (call $num (call $link (i32.const 3) (i32.const 0) (i32.const 1040) (i32.const 1) (i32.const 3) (i32.const 1264) (i32.const 2)))
    ;; link f/ to h
    (call $num (call $link (i32.const 3) (i32.const 0) (i32.const 1168) (i32.const 2) (i32.const 3) (i32.const 1280) (i32.const 1)))
    ;; rename f/ to g
    (call $num (call $rename (i32.const 3) (i32.const 1168) (i32.const 2) (i32.const 3) (i32.const 1296) (i32.const 1)))
    ;; rename f to g/
    (call $num (call $rename (i32.const 3) (i32.const 1040) (i32.const 1) (i32.const 3) (i32.const 1312) (i32.const 2)))
    ;; rename n/ to m
    (call $num (call $rename (i32.const 3) (i32.const 1120) (i32.const 2) (i32.const 3) (i32.const 1328) (i32.const 1)))
    ;; rename m to k/
    (call $num (call $rename (i32.const 3) (i32.const 1328) (i32.const 1) (i32.const 3) (i32.const 1344) (i32.const 2)))
    ;; rename ld/ to le
    (call $num (call $rename (i32.const 3) (i32.const 1184) (i32.const 3) (i32.const 3) (i32.const 1360) (i32.const 2)))
    ;; remove f/, a file
    (call $num (call $rmdir (i32.const 3) (i32.const 1168) (i32.const 2)))
    ;; remove ld/, a link to a directory
    (call $num (call $rmdir (i32.const 3) (i32.const 1184) (i32.const 3)))
    ;; remove k/
    (call $num (call $rmdir (i32.const 3) (i32.const 1344) (i32.const 2)))
    ;; remove n2//
    (call $num (call $rmdir (i32.const 3) (i32.const 1136) (i32.const 4)))
    ;; remove d/, not empty
    (call $num (call $rmdir (i32.const 3) (i32.const 1152) (i32.const 2)))
    ;; stat d/x/, a file under d
    (call $num (call $stat (i32.const 0) (i32.const 1376) (i32.const 4)))))
