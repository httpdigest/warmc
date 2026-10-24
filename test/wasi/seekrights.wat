;; The rights fd_seek and fd_tell ask for: a seek that moves the offset
;; needs fd_seek, one that leaves it where it is (0 from the current
;; position) fd_seek or fd_tell, and fd_tell fd_tell; a missing one is
;; ENOTCAPABLE.  It checks itself, as test/wasi/caps.wat does, because
;; wasmtime enforces no rights on a descriptor: a runtime that seeks without
;; fd_seek has nothing here to answer for and the case ends; one that
;; refuses must refuse as WASI says and allow what it allows.  The exit
;; status names the first check that failed, 0 when none did.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_seek" (func $seek (param i32 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_tell" (func $tell (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x400) "f")

  ;; f opened with these rights
  (func $openr (param $oflags i32) (param $rights i64) (result i32)
    (if (call $open (i32.const 3) (i32.const 0) (i32.const 0x400) (i32.const 1) (local.get $oflags)
                    (local.get $rights) (local.get $rights) (i32.const 0) (i32.const 0x310))
      (then (call $exit (i32.const 90))))
    (i32.load (i32.const 0x310)))

  (func $want (param $got i32) (param $want i32) (param $check i32)
    (if (i32.ne (local.get $got) (local.get $want)) (then (call $exit (local.get $check)))))

  (func (export "_start")
    (local $r i32) (local $t i32)
    (drop (call $close (call $openr (i32.const 1) (i64.const 0x1fffffff))))
    ;; fd_read alone
    (local.set $r (call $openr (i32.const 0) (i64.const 0x2)))
    (if (i32.eqz (call $seek (local.get $r) (i64.const 3) (i32.const 0) (i32.const 0x320)))
      (then (call $exit (i32.const 0))))
    (call $want (call $seek (local.get $r) (i64.const 3) (i32.const 0) (i32.const 0x320)) (i32.const 76) (i32.const 1))
    (call $want (call $seek (local.get $r) (i64.const 0) (i32.const 1) (i32.const 0x320)) (i32.const 76) (i32.const 2))
    (call $want (call $tell (local.get $r) (i32.const 0x320)) (i32.const 76) (i32.const 3))
    ;; fd_read and fd_tell
    (local.set $t (call $openr (i32.const 0) (i64.const 0x22)))
    (call $want (call $seek (local.get $t) (i64.const 0) (i32.const 1) (i32.const 0x320)) (i32.const 0) (i32.const 4))
    (call $want (call $tell (local.get $t) (i32.const 0x320)) (i32.const 0) (i32.const 5))
    (call $want (call $seek (local.get $t) (i64.const 3) (i32.const 0) (i32.const 0x320)) (i32.const 76) (i32.const 6))
    (call $want (call $seek (local.get $t) (i64.const 1) (i32.const 1) (i32.const 0x320)) (i32.const 76) (i32.const 7))
    (call $exit (i32.const 0))))
