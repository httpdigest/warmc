;; A clock subscription shorter than a millisecond has to wait for about as
;; long as it asked for.
;;
;; poll(2) takes whole milliseconds, so routing a timer through it leaves two
;; wrong answers and no right one: round 100us down and the wait is skipped
;; altogether, round it up and it lasts ten times too long.  Go's scheduler naps
;; in microseconds, and the rounding-up cost 0.35s of wall clock on a two second
;; benchmark -- time spent asleep rather than working, so it never showed in the
;; instruction counts or in `user`.  A wait with no file descriptor in it is now
;; a nanosleep, which says what it means.
;;
;; The test takes the shortest of 300 naps of 100us and of 300 of 1ms.  Where
;; the short nap is honoured it is far shorter than the long one; where it is
;; rounded up to a millisecond the two are the same, so `t1 * 2 < t2` separates
;; them.  The shortest, not the total, because the suite runs this on a machine
;; doing ninety other things: the totals carried every wakeup the scheduler was
;; late for, and past 0.8ms of that a nap misjudged either engine, wasmtime as
;; often as warmc.  Some of 300 naps wake on time however busy the machine is,
;; and no rounded-up nap is ever shorter than a millisecond.  Both engines print
;; `ok`; before this, warmc printed `slow`.

(module
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  (data (i32.const 0x300) "ok\n")
  (data (i32.const 0x310) "slow\n")

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  ;; The monotonic clock, in nanoseconds.
  (func $now (result i64)
    (drop (call $clock (i32.const 1) (i64.const 0) (i32.const 0x1d0)))
    (i64.load (i32.const 0x1d0)))

  ;; One relative clock subscription: id at 16, timeout at 24, flags at 40.
  (func $nap (param $ns i64)
    (i64.store (i32.const 0x100) (i64.const 0))
    (i32.store8 (i32.const 0x108) (i32.const 0))
    (i32.store (i32.const 0x110) (i32.const 1))
    (i64.store (i32.const 0x118) (local.get $ns))
    (i64.store (i32.const 0x120) (i64.const 0))
    (i32.store16 (i32.const 0x128) (i32.const 0))
    (drop (call $poll (i32.const 0x100) (i32.const 0x140) (i32.const 1)
                      (i32.const 0x1c0))))

  ;; The shortest of n naps, in nanoseconds.
  (func $naps (param $n i32) (param $ns i64) (result i64)
    (local $t0 i64) (local $d i64) (local $best i64) (local $i i32)
    (local.set $best (i64.const -1))
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $t0 (call $now))
      (call $nap (local.get $ns))
      (local.set $d (i64.sub (call $now) (local.get $t0)))
      (if (i64.lt_u (local.get $d) (local.get $best))
        (then (local.set $best (local.get $d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $best))

  (func (export "_start")
    (local $t1 i64) (local $t2 i64)
    (local.set $t1 (call $naps (i32.const 300) (i64.const 100000)))
    (local.set $t2 (call $naps (i32.const 300) (i64.const 1000000)))
    (if (i64.lt_u (i64.mul (local.get $t1) (i64.const 2)) (local.get $t2))
      (then (call $puts (i32.const 0x300) (i32.const 3)))
      (else (call $puts (i32.const 0x310) (i32.const 5)))))
)
