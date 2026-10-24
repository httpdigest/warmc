;; A file inside a directory that may be searched but not read (mode 0111):
;; the walk goes through it opened for search, where opening it for reading
;; is refused, so the file opens and reads as it would from a shell.  The
;; runner makes sx/f before and gives sx back its mode after; the exit
;; status is path_open's errno, or 100 when what was read is not "f".
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x100) "sx/f")
  (func (export "_start")
    (local $e i32)
    (local.set $e (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 4) (i32.const 0)
                              (i64.const 0x2) (i64.const 0x2) (i32.const 0) (i32.const 0x200)))
    (if (local.get $e) (then (call $exit (local.get $e))))
    (i32.store (i32.const 0x210) (i32.const 0x300))
    (i32.store (i32.const 0x214) (i32.const 8))
    (drop (call $read (i32.load (i32.const 0x200)) (i32.const 0x210) (i32.const 1) (i32.const 0x218)))
    (if (i32.or (i32.ne (i32.load (i32.const 0x218)) (i32.const 2))
                (i32.ne (i32.load16_u (i32.const 0x300)) (i32.const 0x0a66)))
      (then (call $exit (i32.const 100))))
    (call $exit (i32.const 0))))
