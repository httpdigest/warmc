;; poll_oneoff with two clock subscriptions whose deadlines are far apart.
;;
;; The tail loop that emitted clock events ran `for (i = 0; i < n && !ready; i++)`
;; and fired *every* clock subscription, whatever its deadline.  With no fd
;; subscription in the call `ready` is never assigned, so both of the two timers
;; below were reported ready after the shorter one elapsed -- the five-second
;; timer firing about 4.99 seconds early.  A guest that multiplexes timers sees
;; a wakeup for work that is not due yet.
;;
;; One event, userdata 1, is the whole answer.  Printing the count and the
;; userdata of each event is enough to pin it, and the case finishes in the
;; time of the *short* timer, so it costs the suite ~2ms rather than 5s.

(module
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $putn (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x260))
    (i32.store8 (local.get $p) (i32.const 10))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (call $puts (local.get $p)
                (i32.sub (i32.const 0x261) (local.get $p))))

  (func (export "_start")
    (local $n i32) (local $i i32) (local $e i32)

    ;; Subscription 0: monotonic, relative, two milliseconds.  This is the
    ;; one that is allowed to fire.
    (i64.store   (i32.const 0x400) (i64.const 1))          ;; userdata
    (i32.store8  (i32.const 0x408) (i32.const 0))          ;; type = clock
    (i32.store   (i32.const 0x410) (i32.const 1))          ;; monotonic
    (i64.store   (i32.const 0x418) (i64.const 2000000))    ;; 2ms
    (i64.store   (i32.const 0x420) (i64.const 0))          ;; precision
    (i32.store16 (i32.const 0x428) (i32.const 0))          ;; relative

    ;; Subscription 1: monotonic, relative, five seconds.  Nothing may report
    ;; this one; if the suite ever hangs for five seconds here, the wait is
    ;; picking the wrong deadline instead.
    (i64.store   (i32.const 0x430) (i64.const 2))          ;; userdata
    (i32.store8  (i32.const 0x438) (i32.const 0))          ;; type = clock
    (i32.store   (i32.const 0x440) (i32.const 1))          ;; monotonic
    (i64.store   (i32.const 0x448) (i64.const 5000000000)) ;; 5s
    (i64.store   (i32.const 0x450) (i64.const 0))          ;; precision
    (i32.store16 (i32.const 0x458) (i32.const 0))          ;; relative

    (if (call $poll (i32.const 0x400) (i32.const 0x500) (i32.const 2)
                    (i32.const 0x300))
      (then (call $exit (i32.const 20)) (unreachable)))

    (local.set $n (i32.load (i32.const 0x300)))
    (call $putn (local.get $n))
    (local.set $i (i32.const 0))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $ev
        (local.set $e (i32.add (i32.const 0x500)
                               (i32.mul (local.get $i) (i32.const 32))))
        (call $putn (i32.wrap_i64 (i64.load (local.get $e))))
        (call $putn (i32.load16_u (i32.add (local.get $e) (i32.const 8))))
        (call $putn (i32.load8_u (i32.add (local.get $e) (i32.const 10))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $ev (i32.lt_u (local.get $i) (local.get $n)))))))
