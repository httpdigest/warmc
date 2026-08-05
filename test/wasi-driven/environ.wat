;; environ_get and args_get: the contents, not the counts.
;;
;; [[counts]] asks the two _sizes_get calls how many there are, and does it
;; with an empty environment on purpose -- the point there is that a variable
;; set in the process is not handed to the guest unless it was asked for.  So
;; nothing anywhere entered environ_get itself, which is the call that has to
;; lay the strings out: an array of pointers into a buffer, each pointer aimed
;; at a NUL-terminated string inside it.  Getting the pointers right and the
;; buffer wrong, or writing one more or one fewer NUL than there are strings,
;; is invisible to a count.
;;
;; This one needs its own driver only because it has to be given an
;; environment, and the plain runner deliberately gives none.  It is still
;; differential: both engines are handed the same two variables and the same
;; arguments, and both have to lay them out the same way.  argv[0] is dropped
;; because it is the program's own path and differs by construction.
(module
  (import "wasi_snapshot_preview1" "environ_sizes_get"
    (func $env_sizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_get"
    (func $env_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_sizes_get"
    (func $args_sizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_get"
    (func $args_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  (global $out (mut i32) (i32.const 0x1000))

  (func $put (param $c i32)
    (i32.store8 (global.get $out) (local.get $c))
    (global.set $out (i32.add (global.get $out) (i32.const 1))))

  (func $emit (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x900))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (loop $c
      (call $put (i32.load8_u (local.get $p)))
      (local.set $p (i32.add (local.get $p) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $p) (i32.const 0x900)))))

  ;; The string a pointer aims at, through to its NUL, which is the part a
  ;; count cannot see.
  (func $puts_at (param $p i32)
    (loop $c
      (br_if $c (block (result i32)
        (call $put (i32.load8_u (local.get $p)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (i32.load8_u (local.get $p))))))

  (func (export "_start")
    (local $n i32) (local $i i32)

    (call $emit (call $env_sizes (i32.const 0x100) (i32.const 0x104)))
    (call $put (i32.const 32))
    (call $emit (i32.load (i32.const 0x100)))
    (call $put (i32.const 32))
    (call $emit (i32.load (i32.const 0x104)))
    (call $put (i32.const 10))

    (call $emit (call $env_get (i32.const 0x200) (i32.const 0x400)))
    (call $put (i32.const 10))
    (local.set $n (i32.load (i32.const 0x100)))
    (local.set $i (i32.const 0))
    (block $done
      (loop $e
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (call $puts_at
          (i32.load (i32.add (i32.const 0x200)
                             (i32.mul (local.get $i) (i32.const 4)))))
        (call $put (i32.const 10))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $e)))

    (call $emit (call $args_sizes (i32.const 0x108) (i32.const 0x10c)))
    (call $put (i32.const 32))
    (call $emit (i32.load (i32.const 0x108)))
    (call $put (i32.const 10))

    (call $emit (call $args_get (i32.const 0x300) (i32.const 0x600)))
    (call $put (i32.const 10))
    (local.set $n (i32.load (i32.const 0x108)))
    (local.set $i (i32.const 1))
    (block $adone
      (loop $a
        (br_if $adone (i32.ge_u (local.get $i) (local.get $n)))
        (call $puts_at
          (i32.load (i32.add (i32.const 0x300)
                             (i32.mul (local.get $i) (i32.const 4)))))
        (call $put (i32.const 10))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $a)))

    (i32.store (i32.const 0x00) (i32.const 0x1000))
    (i32.store (i32.const 0x04)
               (i32.sub (global.get $out) (i32.const 0x1000)))
    (drop (call $write (i32.const 1) (i32.const 0x00) (i32.const 1)
                       (i32.const 0x0c)))))
