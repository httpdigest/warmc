;; path_link with the follow flag, which must not reach past the preopen.
;;
;; The link's source leaf went to linkat(AT_SYMLINK_FOLLOW) unresolved: the
;; kernel followed a symlink there, so `out -> ../linkescape.secret`, a link
;; the guest may make, became a hard link inside the sandbox to the file
;; outside it, readable and writable.  The flag is refused with EINVAL, as
;; wasmtime refuses it, for a link out of the preopen and for one that stays
;; inside (`in2 -> inside`) alike; without the flag a link to the symlink
;; itself is made.  Everything is printed as a decimal errno or the bytes
;; read, and diffed against wasmtime over identical trees, with a
;; `<name>.secret` file outside the preopen for the escape to aim at.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_link"
    (func $link (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read"
    (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "../linkescape.secret")  ;; 20
  (data (i32.const 0x120) "out")                   ;; 3
  (data (i32.const 0x130) "hard")                  ;; 4
  (data (i32.const 0x140) "inside")                ;; 6
  (data (i32.const 0x150) "in2")                   ;; 3
  (data (i32.const 0x160) "hard2")                 ;; 5
  (data (i32.const 0x170) "INSIDE\0a")             ;; 7

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

  ;; Open `path` without following a final link and print what it reads,
  ;; or the errno.
  (func $show (param $path i32) (param $plen i32)
    (local $e i32) (local $fd i32)
    (local.set $e (call $open (i32.const 3) (i32.const 0) (local.get $path)
                              (local.get $plen) (i32.const 0)
                              (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                              (i32.const 0) (i32.const 0x3a0)))
    (call $num (local.get $e))
    (if (i32.eqz (local.get $e))
      (then
        (local.set $fd (i32.load (i32.const 0x3a0)))
        (i32.store (i32.const 0x380) (i32.const 0x3b0))
        (i32.store (i32.const 0x384) (i32.const 16))
        (drop (call $read (local.get $fd) (i32.const 0x380) (i32.const 1) (i32.const 0x390)))
        (i32.store (i32.const 0x200) (i32.const 0x3b0))
        (i32.store (i32.const 0x204) (i32.load (i32.const 0x390)))
        (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x20c)))
        (drop (call $close (local.get $fd))))))

  (func (export "_start")
    ;; a link out of the preopen, which the guest may make
    (call $num (call $symlink (i32.const 0x100) (i32.const 20) (i32.const 3)
                              (i32.const 0x120) (i32.const 3)))
    ;; hard-linking what it names is refused, and nothing is made
    (call $num (call $link (i32.const 3) (i32.const 1) (i32.const 0x120) (i32.const 3)
                           (i32.const 3) (i32.const 0x130) (i32.const 4)))
    (call $show (i32.const 0x130) (i32.const 4))

    ;; following a link that stays inside is refused all the same
    (call $num (call $put (i32.const 0x140) (i32.const 6) (i32.const 0x170) (i32.const 7)))
    (call $num (call $symlink (i32.const 0x140) (i32.const 6) (i32.const 3)
                              (i32.const 0x150) (i32.const 3)))
    (call $num (call $link (i32.const 3) (i32.const 1) (i32.const 0x150) (i32.const 3)
                           (i32.const 3) (i32.const 0x160) (i32.const 5)))
    (call $show (i32.const 0x160) (i32.const 5)))
)
