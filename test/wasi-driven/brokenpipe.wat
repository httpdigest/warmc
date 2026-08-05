;; fd_write against a reader that has gone away.
;;
;; sys_write returned void on both the Darwin and the freestanding-Linux
;; branch, so warmc_rt_write handed back the byte count it was asked for
;; whatever the kernel said.  EPIPE, ENOSPC, EBADF and every short write were
;; invisible: `prog | head -1` wrote into a closed pipe a hundred thousand
;; times and exited 0.  SIGPIPE would have killed it first, except that the
;; driver now ignores SIGPIPE the way wasmtime does, so the errno is what the
;; guest sees.
;;
;; The loop exits with whatever errno fd_write first reports, so a healthy
;; engine exits 64 (EPIPE) and the old one exited 0.  The plain case runner
;; cannot express this because it redirects stdout to a file, where the write
;; never fails; run_wasi.sh pipes this one into `head -1` and compares the
;; exit status against wasmtime.

(module
  (import "wasi_snapshot_preview1" "fd_write"
          (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 100) "hello from a doomed pipe\n")

  (func (export "_start")
    (local $i i32) (local $rc i32)
    (i32.store (i32.const 0) (i32.const 100))
    (i32.store (i32.const 4) (i32.const 25))
    (block $done
      (loop $again
        (local.set $rc (call $fd_write (i32.const 1) (i32.const 0)
                                       (i32.const 1) (i32.const 8)))
        (br_if $done (i32.ne (local.get $rc) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $again (i32.lt_u (local.get $i) (i32.const 100000)))))
    (call $exit (local.get $rc))))
