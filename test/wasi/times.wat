;; Timestamps: set both, omit one, and set one to now.
;;
;; `path_filestat_set_times` takes the two stamps and a flag word saying which
;; of them to write, and two of the four flags have no value behind them -- one
;; means "now" and the absence of a pair means "leave that one alone".  The host
;; passes both of those to the kernel as a marker in a timespec's *nanoseconds*
;; field, with the seconds ignored.  Put the marker in the seconds field
;; instead and nothing fails: the call returns 0, the stamp is simply set to
;; 1969, and a stamp the program asked not to touch is overwritten.  So the
;; only way to see it is to read the stamp back, which is what this does.
;;
;; Nothing in TinyGo reaches here -- its wasip1 `os.Chtimes` is ENOSYS -- so
;; test/gowasi cannot cover this and a hand-written module has to.
(module
  (import "wasi_snapshot_preview1" "fd_prestat_get" (func $prestat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_open" (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get" (func $stat (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_set_times"
          (func $settimes (param i32 i32 i32 i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_unlink_file" (func $unlink (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "stamp.txt")
  (data (i32.const 0x120) "set ok\n")
  (data (i32.const 0x130) "set BAD\n")
  (data (i32.const 0x140) "omit ok\n")
  (data (i32.const 0x150) "omit BAD\n")
  (data (i32.const 0x160) "now ok\n")
  (data (i32.const 0x170) "now BAD\n")

  ;; iovec at 0x200, scratch at 0x300, filestat at 0x600: atim +40, mtim +48.
  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x20c))))

  (func $say (param $ok i32) (param $good i32) (param $gn i32) (param $bad i32) (param $bn i32)
    (if (local.get $ok)
      (then (call $puts (local.get $good) (local.get $gn)))
      (else (call $puts (local.get $bad) (local.get $bn)))))

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  ;; The stamp the file carries now, in whole seconds.  Whole seconds because a
  ;; filesystem may keep fewer digits than were asked for, and that is not what
  ;; is being tested -- a marker written to the wrong field lands nowhere near.
  (func $mtime_sec (result i64)
    (if (call $stat (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                    (i32.const 0x600))
        (then (call $die (i32.const 20))))
    (i64.div_u (i64.load (i32.const 0x630)) (i64.const 1000000000)))

  (func $atime_sec (result i64)
    (if (call $stat (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                    (i32.const 0x600))
        (then (call $die (i32.const 21))))
    (i64.div_u (i64.load (i32.const 0x628)) (i64.const 1000000000)))

  (func (export "_start")
    (local $fd i32) (local $rc i32)
    (if (call $prestat (i32.const 3) (i32.const 0x300)) (then (call $die (i32.const 10))))

    ;; An empty file to hang the stamps on: create|trunc, the same rights fs.wat
    ;; asks for.
    (local.set $rc (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                    (i32.const 9)
                    (i64.const 0x20000e6) (i64.const 0x20000e6)
                    (i32.const 0) (i32.const 0x304)))
    (if (local.get $rc) (then (call $die (i32.add (i32.const 100) (local.get $rc)))))
    (local.set $fd (i32.load (i32.const 0x304)))
    (drop (call $close (local.get $fd)))

    ;; Both stamps to values with a nonzero nanosecond part: ATIM|MTIM = 5.
    (if (call $settimes (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                        (i64.const 1000000000500000000)
                        (i64.const 1234567890123456789)
                        (i32.const 5))
        (then (call $die (i32.const 11))))
    (call $say (i32.and (i64.eq (call $mtime_sec) (i64.const 1234567890))
                        (i64.eq (call $atime_sec) (i64.const 1000000000)))
               (i32.const 0x120) (i32.const 7) (i32.const 0x130) (i32.const 8))

    ;; Only the access time this time: ATIM = 1, so the modification time is the
    ;; one with no pair, and has to come back unchanged.
    (if (call $settimes (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                        (i64.const 1500000000000000000) (i64.const 0)
                        (i32.const 1))
        (then (call $die (i32.const 12))))
    (call $say (i32.and (i64.eq (call $mtime_sec) (i64.const 1234567890))
                        (i64.eq (call $atime_sec) (i64.const 1500000000)))
               (i32.const 0x140) (i32.const 8) (i32.const 0x150) (i32.const 9))

    ;; And the modification time to now: MTIM_NOW = 8, which has to read back
    ;; later than 2009 without the module knowing what the clock says -- and
    ;; before 2100, because a stamp set to a second *before* the epoch comes
    ;; back as a nanosecond count that wrapped, and "very large" would otherwise
    ;; read as "later".
    (if (call $settimes (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 9)
                        (i64.const 0) (i64.const 0)
                        (i32.const 8))
        (then (call $die (i32.const 13))))
    (call $say (i32.and (i64.gt_u (call $mtime_sec) (i64.const 1234567890))
                        (i64.lt_u (call $mtime_sec) (i64.const 4102444800)))
               (i32.const 0x160) (i32.const 7) (i32.const 0x170) (i32.const 8))

    (if (call $unlink (i32.const 3) (i32.const 0x100) (i32.const 9))
        (then (call $die (i32.const 14))))
    (call $exit (i32.const 0)))
)
