;; What must not be reachable.
;;
;; preview1 is capability-based: a program can only name a path under a
;; directory it was given.  Three ways out, and the answer each gets is
;; compared against wasmtime rather than asserted here -- the point is that
;; both engines refuse, and refuse the same way.
;;
;;   * `..` above the preopen
;;   * an absolute path
;;   * a symlink inside the preopen whose target is outside it
;;
;; The third is the one a lexical check misses: the path is unremarkable and
;; it is the kernel that would follow it out.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "../escape.secret")   ;; 16
  (data (i32.const 0x120) "/etc/hosts")         ;; 10
  (data (i32.const 0x140) "out")                ;; 3, the link
  (data (i32.const 0x150) "../escape.secret")   ;; 16, its target
  (data (i32.const 0x170) " \n")

  ;; One decimal number, then a space.
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
        ;; written backwards; reverse it in place
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

  (func $try (param $p i32) (param $n i32) (result i32)
    (call $open (i32.const 3) (i32.const 1) (local.get $p) (local.get $n)
                (i32.const 0) (i64.const 0x200002) (i64.const 0x200002)
                (i32.const 0) (i32.const 0x280)))

  (func (export "_start")
    (call $num (call $try (i32.const 0x100) (i32.const 16)))
    (call $num (call $try (i32.const 0x120) (i32.const 10)))
    ;; A link is allowed to be made; what it reaches is decided when it is
    ;; resolved, which is the case this is really about.
    (call $num (call $symlink (i32.const 0x150) (i32.const 16) (i32.const 3)
                              (i32.const 0x140) (i32.const 3)))
    (call $num (call $try (i32.const 0x140) (i32.const 3)))
    (call $exit (i32.const 0)))
)
