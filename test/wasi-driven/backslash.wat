;; A path component that is a separator to the host but not to the guest.
;;
;; preview1 paths have one separator, '/', and rt/wasifs.c splits on it and
;; hands each component to openat.  On Windows a backslash is a separator too,
;; so `..\name` is ONE component to wasifs and TWO to Win32 -- and Win32 would
;; resolve the `..` and walk out of the preopen.  rt/winposix.c refuses a leaf
;; carrying one, and answers what a POSIX host answers for a name that is not
;; there: ENOENT.
;;
;; On Linux and macOS a backslash is an ordinary filename character, so the
;; same call is asking for a file nobody created and gets the same 44.  The
;; expected line is therefore the same on every host, which is why it is
;; written out rather than diffed -- wasmtime on Windows refuses it too, but
;; as ENOTCAPABLE (63), so a differential would pin warmc to one host's
;; spelling of the refusal.
;;
;; The second call is the ordinary spelling of the same escape, which every
;; host refuses with 63.  It is here so that a run where the file simply is
;; not where the test thinks it is cannot pass by accident.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "..\5cbackslash.secret")   ;; 19
  (data (i32.const 0x140) "../backslash.secret")     ;; 19

  (func $num (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x290))
    (i32.store8 (local.get $p) (i32.const 32))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (i32.sub (i32.const 0x291) (local.get $p)))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $try (param $p i32) (param $n i32) (result i32)
    (call $open (i32.const 3) (i32.const 1) (local.get $p) (local.get $n)
                (i32.const 0) (i64.const 0x200002) (i64.const 0x200002)
                (i32.const 0) (i32.const 0x280)))

  (func (export "_start")
    (call $num (call $try (i32.const 0x100) (i32.const 19)))
    (call $num (call $try (i32.const 0x140) (i32.const 19))))
)
