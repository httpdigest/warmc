;; A create or truncate whose leaf is a symlink.  path_open's last open never
;; follows a link: it answers ELOOP, and the host follows the link itself
;; when asked to.  On Windows that open was a CreateFileW of the link itself,
;; whose CREATE_ALWAYS for O_CREAT|O_TRUNC replaced the link by an empty
;; file before the ELOOP test could see it: the open succeeded, the link was
;; gone, and what it led to was left as it was.  Here `lt -> t`, with `t`
;; holding "data"; each step prints its errno, 0 for success, the link is
;; read back at the end, and the whole is diffed against wasmtime, the tree
;; left behind included.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_readlink"
    (func $readlinkp (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 1024) "t")
  (data (i32.const 1040) "lt")
  (data (i32.const 1056) "data")
  (data (i32.const 1072) "x")

  ;; the errno as decimal and a newline
  (func $num (param $v i32)
    (local $n i32)
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

  ;; open with these lookup and open flags; on success write the n bytes at
  ;; s through it, then close it
  (func $openw (param $dirflags i32) (param $p i32) (param $n i32) (param $oflags i32)
               (param $s i32) (param $sn i32) (result i32)
    (local $e i32) (local $fd i32)
    (local.set $e (call $open (i32.const 3) (local.get $dirflags) (local.get $p) (local.get $n)
                              (local.get $oflags) (i64.const 0x1fffffff) (i64.const 0x1fffffff)
                              (i32.const 0) (i32.const 0x310)))
    (if (i32.eqz (local.get $e))
      (then
        (local.set $fd (i32.load (i32.const 0x310)))
        (if (local.get $sn)
          (then
            (i32.store (i32.const 0x340) (local.get $s))
            (i32.store (i32.const 0x344) (local.get $sn))
            (drop (call $write (local.get $fd) (i32.const 0x340) (i32.const 1) (i32.const 0x348)))))
        (drop (call $close (local.get $fd)))))
    (local.get $e))

  (func (export "_start")
    ;; t, holding "data"
    (call $num (call $openw (i32.const 0) (i32.const 1024) (i32.const 1) (i32.const 1)
                            (i32.const 1056) (i32.const 4)))
    ;; lt -> t
    (call $num (call $symlink (i32.const 1024) (i32.const 1) (i32.const 3) (i32.const 1040) (i32.const 2)))
    ;; not following lt: a create and truncate, a truncate, a create, and an
    ;; exclusive create, none of which may touch the link
    (call $num (call $openw (i32.const 0) (i32.const 1040) (i32.const 2) (i32.const 9)
                            (i32.const 0) (i32.const 0)))
    (call $num (call $openw (i32.const 0) (i32.const 1040) (i32.const 2) (i32.const 8)
                            (i32.const 0) (i32.const 0)))
    (call $num (call $openw (i32.const 0) (i32.const 1040) (i32.const 2) (i32.const 1)
                            (i32.const 0) (i32.const 0)))
    (call $num (call $openw (i32.const 0) (i32.const 1040) (i32.const 2) (i32.const 5)
                            (i32.const 0) (i32.const 0)))
    ;; following lt, a create and truncate empties t, which then gets "x"
    (call $num (call $openw (i32.const 1) (i32.const 1040) (i32.const 2) (i32.const 9)
                            (i32.const 1072) (i32.const 1)))
    ;; lt is still the link to t
    (call $num (call $readlinkp (i32.const 3) (i32.const 1040) (i32.const 2) (i32.const 0x380)
                                (i32.const 64) (i32.const 0x370)))
    (i32.store (i32.const 0x340) (i32.const 0x380))
    (i32.store (i32.const 0x344) (i32.load (i32.const 0x370)))
    (drop (call $write (i32.const 1) (i32.const 0x340) (i32.const 1) (i32.const 0x348)))
    (call $num (i32.const 0))))
