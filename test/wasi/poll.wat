;; poll_oneoff, whose event record nothing in the tree had ever looked at.
;;
;; preview1's `event` is userdata at 0, error at 8, type at 10, and then an
;; `fd_readwrite` at 16 holding nbytes at 16 and flags at 24.  warmc wrote the
;; flags at 26, which is tail padding, so a hangup was reported to nobody.
;; The clock subscription is here for the timeout arithmetic beside it: a
;; delay under a millisecond used to truncate to zero and skip the wait.
;;
;; What is printed is the event count and the fields of each event, which both
;; engines have to agree on.

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

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  ;; One clock subscription, relative, half a millisecond.
  (func (export "_start")
    (local $n i32) (local $i i32) (local $e i32)
    (i64.store (i32.const 0x400) (i64.const 7))        ;; userdata
    (i32.store8 (i32.const 0x408) (i32.const 0))       ;; type = clock
    (i32.store (i32.const 0x410) (i32.const 1))        ;; monotonic
    ;; Ten seconds, not half a millisecond: stdin has data waiting, so the
    ;; fd subscription below is ready at once and the clock is only here to
    ;; keep the call from blocking if it is not.  A short timeout makes which
    ;; event is reported a race with the scheduler.
    (i64.store (i32.const 0x418) (i64.const 10000000000))
    (i64.store (i32.const 0x420) (i64.const 0))        ;; precision
    (i32.store16 (i32.const 0x428) (i32.const 0))      ;; relative

    (i64.store (i32.const 0x430) (i64.const 9))        ;; userdata
    (i32.store8 (i32.const 0x438) (i32.const 1))       ;; type = fd_read
    (i32.store (i32.const 0x440) (i32.const 0))        ;; stdin

    (if (call $poll (i32.const 0x400) (i32.const 0x500) (i32.const 2)
                    (i32.const 0x300))
      (then (call $die (i32.const 20))))
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
        (call $putn (i32.wrap_i64
                      (i64.load (i32.add (local.get $e) (i32.const 16)))))
        (call $putn (i32.load16_u (i32.add (local.get $e) (i32.const 24))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $ev (i32.lt_u (local.get $i) (local.get $n)))))))
