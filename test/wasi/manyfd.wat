;; The two fixed tables a real program can walk off the end of.
;;
;; The fd table was `Fd g_fds[256]`, so the 253rd concurrently-open file got
;; EMFILE, and poll_oneoff refused any call with more than MAXSUBS (64)
;; subscriptions with EINVAL before it looked at even one of them.  Neither
;; limit is in preview1; both were the size of an array.
;;
;; 300 files open at once, then a poll over 100 clock subscriptions.  What is
;; printed is the number of opens that succeeded, the rc of the poll and the
;; number of events it produced -- 300, 0, 100 on an engine with no ceiling
;; there.  The subscriptions are all relative zero, so all 100 are due at once
;; and the call does not wait.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 4)
  (data (i32.const 0x100) "seed.txt")

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $putn (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x290))
    (i32.store8 (local.get $p) (i32.const 10))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (call $puts (local.get $p) (i32.sub (i32.const 0x291) (local.get $p))))

  (func (export "_start")
    (local $i i32) (local $ok i32) (local $rc i32) (local $sub i32)

    ;; 300 concurrent opens of the same file, none of them closed.
    (local.set $i (i32.const 0))
    (loop $more
      (if (i32.eqz (call $open (i32.const 3) (i32.const 0) (i32.const 0x100)
                                (i32.const 8) (i32.const 0) (i64.const 78)
                                (i64.const 78) (i32.const 0) (i32.const 0x300)))
        (then (local.set $ok (i32.add (local.get $ok) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $more (i32.lt_u (local.get $i) (i32.const 300))))
    (call $putn (local.get $ok))

    ;; 100 clock subscriptions, every one of them already due.
    (local.set $i (i32.const 0))
    (loop $subs
      (local.set $sub (i32.add (i32.const 0x10000)
                               (i32.mul (local.get $i) (i32.const 48))))
      (i64.store   (local.get $sub) (i64.extend_i32_u (local.get $i)))
      (i32.store8  (i32.add (local.get $sub) (i32.const 8)) (i32.const 0))
      (i32.store   (i32.add (local.get $sub) (i32.const 16)) (i32.const 1))
      (i64.store   (i32.add (local.get $sub) (i32.const 24)) (i64.const 0))
      (i64.store   (i32.add (local.get $sub) (i32.const 32)) (i64.const 0))
      (i32.store16 (i32.add (local.get $sub) (i32.const 40)) (i32.const 0))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $subs (i32.lt_u (local.get $i) (i32.const 100))))

    (local.set $rc (call $poll (i32.const 0x10000) (i32.const 0x20000)
                               (i32.const 100) (i32.const 0x304)))
    (call $putn (local.get $rc))
    (call $putn (i32.load (i32.const 0x304)))))
