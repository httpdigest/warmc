;; The WASI functions a TinyGo module may import, every body an
;; `unreachable`.  This module exists to be *linked against*, not run: it is
;; registered as "wasi_snapshot_preview1" so instantiation can check the
;; import types -- both sides compiled by warmc, so the structural ids agree
;; by construction -- and the driver then rebinds the imports it implements
;; to host C functions with `warmc_bind_import`.  A function a program
;; imports but the driver never bound traps here, loudly, which is exactly
;; what an unimplemented syscall should do.
;;
;; The signatures are wasi_snapshot_preview1's, wider than today's programs
;; need on purpose: a future Go program that pulls in fd_close or
;; clock_time_get should fail at the unbound call, not at the link.
(module
  (func (export "fd_write") (param i32 i32 i32 i32) (result i32) unreachable)
  (func (export "fd_read") (param i32 i32 i32 i32) (result i32) unreachable)
  (func (export "fd_close") (param i32) (result i32) unreachable)
  (func (export "fd_seek") (param i32 i64 i32 i32) (result i32) unreachable)
  (func (export "fd_fdstat_get") (param i32 i32) (result i32) unreachable)
  (func (export "fd_prestat_get") (param i32 i32) (result i32) unreachable)
  (func (export "fd_prestat_dir_name") (param i32 i32 i32) (result i32) unreachable)
  (func (export "proc_exit") (param i32) unreachable)
  (func (export "args_sizes_get") (param i32 i32) (result i32) unreachable)
  (func (export "args_get") (param i32 i32) (result i32) unreachable)
  (func (export "environ_sizes_get") (param i32 i32) (result i32) unreachable)
  (func (export "environ_get") (param i32 i32) (result i32) unreachable)
  (func (export "clock_time_get") (param i32 i64 i32) (result i32) unreachable)
  (func (export "random_get") (param i32 i32) (result i32) unreachable)
  (func (export "sched_yield") (result i32) unreachable)
  (func (export "poll_oneoff") (param i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_open")
        (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32) unreachable)
)
