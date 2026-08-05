;; What a symlink is allowed to reach, and where a relative one starts from.
;;
;; walk() resolves every path component itself and hands back a leaf it has
;; deliberately not resolved, so that the caller can decide.  Every caller
;; re-resolves that leaf in userspace -- path_open re-walks it, unlinkat and
;; friends pass O_NOFOLLOW -- except the two filestat entry points, which
;; passed the leaf to fstatat and utimensat with the follow flag the guest
;; asked for.  That hands the last component to the kernel with no sandbox
;; check at all: a link inside the preopen pointing anywhere the host process
;; can read became a size, an inode and three timestamps, and
;; path_filestat_set_times became a write.
;;
;; The second half is about where a followed link resolves *from*.  When
;; path_open found the leaf was a link and the guest wanted it followed, it
;; overwrote the whole path with the link's text and restarted from the
;; preopen root -- but a relative target is relative to the directory holding
;; the link.  So `sub/link -> inner` opened the root's `inner` rather than
;; `sub/inner`: a different file, and with O_CREAT|O_TRUNC a different file
;; destroyed.  walk() gets this right for the components it resolves itself;
;; only the leaf retry did not.
;;
;; Everything is printed as a decimal errno, or as the bytes read, and diffed
;; against wasmtime -- the harness runs both engines over identical trees.  A
;; `<name>.secret` file sits outside the preopen for the first half to aim at.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get"
    (func $statat (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_create_directory"
    (func $mkdir (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read"
    (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "../symlink.secret")  ;; 17
  (data (i32.const 0x120) "up")                 ;; 2
  (data (i32.const 0x130) "sub")                ;; 3
  (data (i32.const 0x140) "inner")              ;; 5
  (data (i32.const 0x150) "sub/inner")          ;; 9
  (data (i32.const 0x160) "sub/link")           ;; 8
  (data (i32.const 0x170) "OUTER\0a")           ;; 6
  (data (i32.const 0x180) "INNER\0a")           ;; 6

  (func $num (param $v i32)
    (local $p i32) (local $n i32)
    (local.set $p (i32.const 0x1a0))
    (if (i32.eqz (local.get $v))
      (then (i32.store8 (local.get $p) (i32.const 48)) (local.set $n (i32.const 1)))
      (else
        (block $done (loop $lp
          (br_if $done (i32.eqz (local.get $v)))
          (i32.store8 (i32.add (local.get $p) (local.get $n))
                      (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
          (local.set $n (i32.add (local.get $n) (i32.const 1)))
          (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
          (br $lp)))
        (local.set $v (i32.const 0))
        (block $d2 (loop $l2
          (br_if $d2 (i32.ge_u (local.get $v) (i32.div_u (local.get $n) (i32.const 2))))
          (local.set $p (i32.load8_u (i32.add (i32.const 0x1a0) (local.get $v))))
          (i32.store8 (i32.add (i32.const 0x1a0) (local.get $v))
            (i32.load8_u (i32.add (i32.const 0x1a0)
              (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $v)))))
          (i32.store8 (i32.add (i32.const 0x1a0)
            (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $v))) (local.get $p))
          (local.set $v (i32.add (local.get $v) (i32.const 1)))
          (br $l2)))))
    (i32.store8 (i32.add (i32.const 0x1a0) (local.get $n)) (i32.const 10))
    (i32.store (i32.const 0x200) (i32.const 0x1a0))
    (i32.store (i32.const 0x204) (i32.add (local.get $n) (i32.const 1)))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x20c))))

  ;; Create a file under the preopen and put `len` bytes of `src` in it.
  (func $put (param $path i32) (param $plen i32) (param $src i32) (param $len i32)
             (result i32)
    (local $e i32)
    (local.set $e
      (call $open (i32.const 3) (i32.const 0) (local.get $path) (local.get $plen)
                  (i32.const 1)                      ;; O_CREAT
                  (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                  (i32.const 0) (i32.const 0x3a0)))
    (if (local.get $e) (then (return (local.get $e))))
    (i32.store (i32.const 0x380) (local.get $src))
    (i32.store (i32.const 0x384) (local.get $len))
    (drop (call $write (i32.load (i32.const 0x3a0)) (i32.const 0x380)
                       (i32.const 1) (i32.const 0x390)))
    (drop (call $close (i32.load (i32.const 0x3a0))))
    (i32.const 0))

  (func (export "_start")
    (local $e i32) (local $fd i32) (local $n i32)

    ;; --- a link that leaves the preopen -------------------------------------
    ;; Making it is allowed; what it reaches is decided on resolution.
    (call $num (call $symlink (i32.const 0x100) (i32.const 17) (i32.const 3)
                              (i32.const 0x120) (i32.const 2)))

    ;; Following it is not.  Both the verdict and, when there is one, the size
    ;; it reported -- the size is what says whether the kernel resolved it.
    (local.set $e (call $statat (i32.const 3) (i32.const 1)
                                (i32.const 0x120) (i32.const 2) (i32.const 0x300)))
    (call $num (local.get $e))
    (call $num (if (result i32) (local.get $e)
                 (then (i32.const 0))
                 (else (i32.load (i32.const 0x320)))))

    ;; Not following it is fine, and reports the link itself.
    (call $num (call $statat (i32.const 3) (i32.const 0)
                             (i32.const 0x120) (i32.const 2) (i32.const 0x300)))

    ;; --- where a relative link resolves from --------------------------------
    (call $num (call $mkdir (i32.const 3) (i32.const 0x130) (i32.const 3)))
    (call $num (call $put (i32.const 0x140) (i32.const 5) (i32.const 0x170) (i32.const 6)))
    (call $num (call $put (i32.const 0x150) (i32.const 9) (i32.const 0x180) (i32.const 6)))
    (call $num (call $symlink (i32.const 0x140) (i32.const 5) (i32.const 3)
                              (i32.const 0x160) (i32.const 8)))

    ;; sub/link -> inner, so this is sub/inner: INNER, not OUTER.
    (local.set $e (call $open (i32.const 3) (i32.const 1) (i32.const 0x160)
                              (i32.const 8) (i32.const 0)
                              (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                              (i32.const 0) (i32.const 0x3a0)))
    (call $num (local.get $e))
    (if (i32.eqz (local.get $e))
      (then
        (local.set $fd (i32.load (i32.const 0x3a0)))
        (i32.store (i32.const 0x380) (i32.const 0x3b0))
        (i32.store (i32.const 0x384) (i32.const 6))
        (drop (call $read (local.get $fd) (i32.const 0x380)
                          (i32.const 1) (i32.const 0x390)))
        (i32.store (i32.const 0x200) (i32.const 0x3b0))
        (i32.store (i32.const 0x204) (i32.load (i32.const 0x390)))
        (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x20c)))
        (drop (call $close (local.get $fd)))))

    (call $exit (i32.const 0)))
)
