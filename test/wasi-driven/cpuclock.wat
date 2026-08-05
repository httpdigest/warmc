;; clock_time_get and clock_res_get for the two CPU-time clocks.
;;
;; preview1 defines four clock ids -- realtime, monotonic, process_cputime_id
;; and thread_cputime_id -- and the host answered `id > 1` with EINVAL, so two
;; of the four were simply absent.  clock_res_get also never asked the host
;; anything: it wrote a hardcoded 1ns for whichever clock it was given.
;;
;; There is no wasmtime side to diff against here either, but for the opposite
;; reason to [[echoserver]]: wasmtime answers clock ids 2 and 3 with EBADF, so
;; a differential case would pin warmc to the reference's gap rather than to
;; preview1.  What is printed is the rc of each of the eight calls -- four
;; clocks, res and time -- which must all be 0, and then whether the process
;; clock advanced across a spin, which must be 1.

(module
  (import "wasi_snapshot_preview1" "clock_res_get"
    (func $res (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $time (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

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
    (local $i i32) (local $spin i32) (local $before i64)

    (local.set $i (i32.const 0))
    (loop $c
      (call $putn (call $res (local.get $i) (i32.const 0x400)))
      (call $putn (call $time (local.get $i) (i64.const 0) (i32.const 0x408)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 4))))

    ;; process_cputime_id has to move while the process burns CPU.
    (drop (call $time (i32.const 2) (i64.const 0) (i32.const 0x410)))
    (local.set $before (i64.load (i32.const 0x410)))
    (local.set $spin (i32.const 0))
    (loop $burn
      (local.set $spin (i32.add (local.get $spin) (i32.const 1)))
      (br_if $burn (i32.lt_u (local.get $spin) (i32.const 20000000))))
    (drop (call $time (i32.const 2) (i64.const 0) (i32.const 0x418)))
    (call $putn (i64.gt_u (i64.load (i32.const 0x418)) (local.get $before)))))
