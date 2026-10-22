;; A loop whose header tests a value the loop never changes and leaves for a
;; block that reports the loop's counter and traps -- the shape of Rust's
;; bounds-check panic, which prints the index.  The test can be hoisted in
;; front of the loop, because if it ever holds it holds on the first
;; iteration, but then the report must say the counter's first value: the
;; counter itself does not exist before the loop.  The value tested comes
;; from args_sizes_get, so nothing folds it; the report ends in proc_exit(3),
;; with the trap after it never reached.
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_sizes_get"
    (func $args_sizes_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  ;; write "index <n>\n" to stdout, as one buffer
  (func $report (param $n i32)
    (local $p i32)
    (local.set $p (i32.const 80))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p) (i32.add (i32.const 48) (i32.rem_u (local.get $n) (i32.const 10))))
      (local.set $n (i32.div_u (local.get $n) (i32.const 10)))
      (br_if $d (local.get $n)))
    (i32.store8 (i32.const 80) (i32.const 10))
    (local.set $p (i32.sub (local.get $p) (i32.const 6)))
    (i32.store (local.get $p) (i32.const 0x65646e69))             ;; "inde"
    (i32.store16 offset=4 (local.get $p) (i32.const 0x2078))      ;; "x "
    (i32.store (i32.const 0) (local.get $p))
    (i32.store (i32.const 4) (i32.sub (i32.const 81) (local.get $p)))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 32))))

  (func $walk (param $n i32) (param $start i32) (result i32)
    (local $i i32) (local $s i32)
    (local.set $i (local.get $start))
    (loop $l
      (if (i32.ge_u (local.get $n) (i32.const 41))
        (then (call $report (local.get $i)) (call $exit (i32.const 3)) (unreachable)))
      (local.set $s (i32.add (local.get $s) (i32.load (i32.shl (local.get $i) (i32.const 2)))))
      (i32.store (i32.shl (local.get $i) (i32.const 2)) (local.get $s))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 900))))
    (local.get $s))

  (func (export "_start")
    (local $argc i32)
    (drop (call $args_sizes_get (i32.const 96) (i32.const 100)))
    (local.set $argc (i32.load (i32.const 96)))
    ;; argc is 1: the first walk passes its test, the second does not
    (drop (call $walk (i32.add (i32.const 30) (local.get $argc)) (i32.const 120)))
    (drop (call $walk (i32.add (i32.const 49) (local.get $argc)) (i32.add (i32.const 135) (local.get $argc))))))
