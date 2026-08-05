;; Opening and closing far more files than the host will lend at once.
;;
;; fd_close protected the initial stdio descriptors by refusing to close
;; slots 0, 1 and 2 -- keying on the slot number rather than on what the slot
;; holds.  fd_alloc hands out the lowest free slot, so once the guest closes
;; stdin the next path_open lands in slot 0 and closing it leaks the host
;; descriptor.
;;
;; What this case can show is that the round works at all in both engines; it
;; cannot show the exhaustion, because the descriptor limit here is a million
;; and the leak is one per round.  It is the path that matters: with the leak
;; in place every round below leaves a descriptor behind.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close"
    (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x100) "seed.txt")

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func (export "_start")
    (local $i i32) (local $e i32)
    ;; Give up stdin, so the next descriptor opened takes its slot.
    (drop (call $close (i32.const 0)))
    (loop $round
      (local.set $e
        (call $open (i32.const 3) (i32.const 0) (i32.const 0x100) (i32.const 8)
                    (i32.const 0)
                    (i64.const 0x2000042) (i64.const 0x2000042)
                    (i32.const 0) (i32.const 0x304)))
      (if (local.get $e) (then (call $die (i32.const 20))))
      (if (call $close (i32.load (i32.const 0x304)))
        (then (call $die (i32.const 21))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $round (i32.lt_u (local.get $i) (i32.const 300))))))
