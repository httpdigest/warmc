;; stdin and stdout blocking or not as fd_fdstat_set_flags leaves them, over
;; host files that are not the guest's alone: the pipes, or the terminal,
;; are the parent's too, and keep their flags after the program.  Go's
;; runtime makes all three non-blocking as it starts; the host set O_NONBLOCK
;; on the files themselves, and a shell's terminal stayed non-blocking when
;; the program ended, so that the next program to read it failed EAGAIN.
;; Now the flags are the guest's alone, carried out by poll before each read
;; or write.  Driven by test/wasi-driven/stdioflags.py, which checks the
;; pipes' own flags; the exit status names the first step that went wrong.
;;
;; With no argument (the pipes blocking):
;;   stdin and stdout set non-blocking, and fd_fdstat_get says so;
;;   a read of stdin, which nothing has been written to, says EAGAIN;
;;   "a" on stdout, and poll_oneoff waits for stdin, then reads "x";
;;   stdout written 512 bytes at a time until EAGAIN, as the driver reads
;;   none of it yet, then poll_oneoff waits for it, then "c".
;; With one (stdin's pipe made non-blocking by the driver):
;;   stdin is blocking to the guest, which says "b" and reads: the read
;;   waits for the "y" the driver sends a while later, not EAGAIN.
;; With two (stdout a file the driver wrote ten bytes to and went back to the
;; start of):
;;   stdout set APPEND, and "x" written, which the driver finds after the
;;   ten, with the file's own flags no O_APPEND; a sync flag refused,
;;   ENOTSUP, and the flags left as they were.

(module
  (import "wasi_snapshot_preview1" "fd_fdstat_get"
    (func $fdstat (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_set_flags"
    (func $setfl (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read"
    (func $read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_sizes_get"
    (func $argsizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "a\n")
  (data (i32.const 0x104) "b\n")
  (data (i32.const 0x108) "\nc\n")
  (data (i32.const 0x10c) "x")

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func $flags (param $fd i32) (result i32)
    (drop (call $fdstat (local.get $fd) (i32.const 0x400)))
    (i32.load16_u (i32.const 0x402)))

  ;; Whether fd is ready for an event of type 1 (read) or 2 (write): one
  ;; subscription, no clock, so this waits for it.
  (func $ready (param $fd i32) (param $type i32) (result i32)
    (i64.store (i32.const 0x700) (i64.const 5))
    (i32.store8 (i32.const 0x708) (local.get $type))
    (i32.store (i32.const 0x710) (local.get $fd))
    (if (call $poll (i32.const 0x700) (i32.const 0x740) (i32.const 1) (i32.const 0x760))
      (then (return (i32.const 0))))
    (i32.and (i32.eq (i32.load (i32.const 0x760)) (i32.const 1))
             (i32.and (i32.eqz (i32.load16_u (i32.const 0x748)))
                      (i32.eq (i32.load8_u (i32.const 0x74a)) (local.get $type)))))

  (func $out (param $p i32) (param $len i32) (result i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $len))
    (call $write (i32.const 1) (i32.const 0x200) (i32.const 1) (i32.const 0x30c)))

  ;; One read of stdin into 0x500: its rc, the count at 0x308.
  (func $in (result i32)
    (i32.store (i32.const 0x210) (i32.const 0x500))
    (i32.store (i32.const 0x214) (i32.const 256))
    (call $read (i32.const 0) (i32.const 0x210) (i32.const 1) (i32.const 0x308)))

  ;; Whether the read just made took the two bytes c, "\n".
  (func $took (param $c i32) (result i32)
    (i32.and (i32.eq (i32.load (i32.const 0x308)) (i32.const 2))
             (i32.eq (i32.load16_u (i32.const 0x500))
                     (i32.or (local.get $c) (i32.const 0xa00)))))

  (func (export "_start")
    (local $rc i32) (local $n i32)
    (drop (call $argsizes (i32.const 0x800) (i32.const 0x804)))
    (if (i32.eq (i32.load (i32.const 0x800)) (i32.const 3))
      (then
        (if (call $setfl (i32.const 1) (i32.const 1)) (then (call $die (i32.const 50))))
        (if (i32.ne (call $flags (i32.const 1)) (i32.const 1)) (then (call $die (i32.const 51))))
        (if (call $out (i32.const 0x10c) (i32.const 1)) (then (call $die (i32.const 52))))
        (if (i32.ne (call $setfl (i32.const 1) (i32.const 0x11)) (i32.const 58))
          (then (call $die (i32.const 53))))
        (if (i32.ne (call $flags (i32.const 1)) (i32.const 1)) (then (call $die (i32.const 54))))
        (return)))
    (if (i32.eq (i32.load (i32.const 0x800)) (i32.const 2))
      (then
        (if (call $flags (i32.const 0)) (then (call $die (i32.const 40))))
        (if (call $out (i32.const 0x104) (i32.const 2)) (then (call $die (i32.const 41))))
        (if (call $in) (then (call $die (i32.const 42))))
        (if (i32.eqz (call $took (i32.const 0x79))) (then (call $die (i32.const 43))))
        (return)))

    (if (call $setfl (i32.const 0) (i32.const 4)) (then (call $die (i32.const 20))))
    (if (i32.ne (call $flags (i32.const 0)) (i32.const 4)) (then (call $die (i32.const 21))))
    (if (call $setfl (i32.const 1) (i32.const 4)) (then (call $die (i32.const 22))))
    (if (i32.ne (call $flags (i32.const 1)) (i32.const 4)) (then (call $die (i32.const 23))))

    (if (i32.ne (call $in) (i32.const 6)) (then (call $die (i32.const 24))))
    (if (call $out (i32.const 0x100) (i32.const 2)) (then (call $die (i32.const 25))))
    (if (i32.eqz (call $ready (i32.const 0) (i32.const 1))) (then (call $die (i32.const 26))))
    (if (call $in) (then (call $die (i32.const 27))))
    (if (i32.eqz (call $took (i32.const 0x78))) (then (call $die (i32.const 28))))

    ;; 512 bytes, PIPE_BUF on macOS: a write poll calls ready takes them all
    (memory.fill (i32.const 0x1000) (i32.const 0x66) (i32.const 512))
    (block $full
      (loop $fill
        (local.set $rc (call $out (i32.const 0x1000) (i32.const 512)))
        (br_if $full (i32.eq (local.get $rc) (i32.const 6)))
        (if (local.get $rc) (then (call $die (i32.const 29))))
        (local.set $n (i32.add (local.get $n) (i32.const 1)))
        (br_if $fill (i32.lt_u (local.get $n) (i32.const 8192))))
      (call $die (i32.const 30)))
    (if (i32.eqz (call $ready (i32.const 1) (i32.const 2))) (then (call $die (i32.const 31))))
    (if (call $out (i32.const 0x108) (i32.const 3)) (then (call $die (i32.const 32))))))
