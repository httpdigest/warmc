;; A WASI command that exercises the filesystem: create a file, write to it,
;; read it back, stat it, list the directory, and remove it.
(module
  (import "wasi_snapshot_preview1" "fd_prestat_get" (func $prestat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_dir_name" (func $prename (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_open" (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_seek" (func $seek (param i32 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get" (func $stat (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_unlink_file" (func $unlink (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "note.txt")
  (data (i32.const 0x110) "hello from wasi\n")

  ;; iovec at 0x200, scratch at 0x400, stat at 0x600
  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x20c))))

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func (export "_start")
    (local $fd i32) (local $n i32)
    ;; the preopen must exist
    (if (call $prestat (i32.const 3) (i32.const 0x300)) (then (call $die (i32.const 10))))
    ;; create + write + truncate, rights: read|write|seek|tell|filestat_get
    (local.set $n (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 8)
                    (i32.const 9)
                    (i64.const 0x20000e6) (i64.const 0x20000e6)
                    (i32.const 0) (i32.const 0x304)))
    (if (local.get $n) (then (call $die (i32.add (i32.const 100) (local.get $n)))))
    (local.set $fd (i32.load (i32.const 0x304)))
    (i32.store (i32.const 0x200) (i32.const 0x110))
    (i32.store (i32.const 0x204) (i32.const 16))
    (if (call $write (local.get $fd) (i32.const 0x200) (i32.const 1) (i32.const 0x308))
        (then (call $die (i32.const 12))))
    ;; back to the start and read it again
    (if (call $seek (local.get $fd) (i64.const 0) (i32.const 0) (i32.const 0x310))
        (then (call $die (i32.const 13))))
    (i32.store (i32.const 0x200) (i32.const 0x400))
    (i32.store (i32.const 0x204) (i32.const 64))
    (if (call $read (local.get $fd) (i32.const 0x200) (i32.const 1) (i32.const 0x30c))
        (then (call $die (i32.const 14))))
    (call $puts (i32.const 0x400) (i32.load (i32.const 0x30c)))
    (drop (call $close (local.get $fd)))
    ;; its size, which must be what was written
    (if (call $stat (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 8) (i32.const 0x600))
        (then (call $die (i32.const 15))))
    (if (i64.ne (i64.load (i32.const 0x620)) (i64.const 16)) (then (call $die (i32.const 16))))
    (if (i32.ne (i32.load8_u (i32.const 0x610)) (i32.const 4)) (then (call $die (i32.const 17))))
    ;; and gone again
    (if (call $unlink (i32.const 3) (i32.const 0x100) (i32.const 8))
        (then (call $die (i32.const 18))))
    (if (i32.eqz (call $stat (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 8) (i32.const 0x600)))
        (then (call $die (i32.const 19))))
    (call $exit (i32.const 0)))
)
