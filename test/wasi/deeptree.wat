;; Deep trees and long paths: 200 directories one inside the other, a path
;; that goes down five and back up by "..", and one of 3959 bytes.  The
;; walk keeps only the directory it is in open, and opens again from the
;; preopen down the names it came by to go back up, so depth costs no
;; descriptors; a path may have 4096 bytes, as on Linux.  Each step prints
;; its errno, and the tree left behind is compared with wasmtime's too.
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
  (memory (export "memory") 2)

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


  ;; "a/a/.../a", $n components of names $w bytes long, at 0x10000: each
  ;; prefix made a directory, then the whole stated; the errno, or for a
  ;; failure on the way 1000 times the component's index plus it
  (func $chain (param $n i32) (param $w i32) (result i32)
    (local $i i32) (local $p i32) (local $k i32) (local $e i32)
    (local.set $p (i32.const 0x10000))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (local.get $i) (then (i32.store8 (local.get $p) (i32.const 47)) (local.set $p (i32.add (local.get $p) (i32.const 1)))))
        (local.set $k (i32.const 0))
        (loop $c
          (i32.store8 (local.get $p) (i32.const 97))
          (local.set $p (i32.add (local.get $p) (i32.const 1)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $c (i32.lt_u (local.get $k) (local.get $w))))
        (local.set $e (call $mkdir (i32.const 3) (i32.const 0x10000) (i32.sub (local.get $p) (i32.const 0x10000))))
        (if (local.get $e) (then (return (i32.add (i32.mul (local.get $i) (i32.const 1000)) (local.get $e)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    ;; and stat the whole
    (call $stat (i32.const 0) (i32.const 0x10000) (i32.sub (local.get $p) (i32.const 0x10000))))
  (data (i32.const 0x500) "a/a/a/a/a/../../../../b")
  (func (export "_start")
    ;; 200 deep, names of 1 byte: a directory open at each level once
    ;; was a descriptor held by the walk
    (call $num (call $chain (i32.const 200) (i32.const 1)))
    ;; back up from five down, by ".." through the names gone down by
    (call $num (call $mkdir (i32.const 3) (i32.const 0x500) (i32.const 23)))
    ;; 180 components of 21 bytes: 3959 bytes, below the 4096 a path may have
    (call $num (call $chain (i32.const 180) (i32.const 21)))))
