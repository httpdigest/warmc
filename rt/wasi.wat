;; The import surface of wasi_snapshot_preview1, every body `unreachable`.
;;
;; This module exists to be *linked against*, not run.  Instantiation resolves
;; a function import by finding an export of that name on a registered
;; instance and checking the two signatures agree, so something has to be
;; there for a WASI program to link to at all -- and it has to be a wasm
;; module rather than a table of C pointers, because what the check compares
;; is the *structural type id* the compiler assigns.  Both sides are compiled
;; by warmc, so those agree by construction; a hand-built descriptor would
;; have to reproduce the hashing to agree by luck.
;;
;; rt/wasi.c registers this under the WASI name and then rebinds the calls it
;; implements to C with `warmc_bind_import`.  **A function a program imports
;; but the host does not implement still traps here**, which is what an
;; unimplemented syscall should do: returning ENOSYS instead would let a
;; program carry on believing a file was opened.  The trap names the module
;; and there is no guessing about which call was missing.
;;
;; The signatures are preview1's, and the whole surface is declared rather
;; than only the implemented part.  Declaring less would move the failure from
;; the call to the *link*, which is worse: a program that imports `path_open`
;; and never reaches it would refuse to start.
(module
  (func (export "args_get") (param i32 i32) (result i32) unreachable)
  (func (export "args_sizes_get") (param i32 i32) (result i32) unreachable)
  (func (export "environ_get") (param i32 i32) (result i32) unreachable)
  (func (export "environ_sizes_get") (param i32 i32) (result i32) unreachable)

  (func (export "clock_res_get") (param i32 i32) (result i32) unreachable)
  (func (export "clock_time_get") (param i32 i64 i32) (result i32) unreachable)

  (func (export "fd_advise") (param i32 i64 i64 i32) (result i32) unreachable)
  (func (export "fd_allocate") (param i32 i64 i64) (result i32) unreachable)
  (func (export "fd_close") (param i32) (result i32) unreachable)
  (func (export "fd_datasync") (param i32) (result i32) unreachable)
  (func (export "fd_fdstat_get") (param i32 i32) (result i32) unreachable)
  (func (export "fd_fdstat_set_flags") (param i32 i32) (result i32) unreachable)
  (func (export "fd_fdstat_set_rights") (param i32 i64 i64) (result i32) unreachable)
  (func (export "fd_filestat_get") (param i32 i32) (result i32) unreachable)
  (func (export "fd_filestat_set_size") (param i32 i64) (result i32) unreachable)
  (func (export "fd_filestat_set_times") (param i32 i64 i64 i32) (result i32) unreachable)
  (func (export "fd_pread") (param i32 i32 i32 i64 i32) (result i32) unreachable)
  (func (export "fd_prestat_get") (param i32 i32) (result i32) unreachable)
  (func (export "fd_prestat_dir_name") (param i32 i32 i32) (result i32) unreachable)
  (func (export "fd_pwrite") (param i32 i32 i32 i64 i32) (result i32) unreachable)
  (func (export "fd_read") (param i32 i32 i32 i32) (result i32) unreachable)
  (func (export "fd_readdir") (param i32 i32 i32 i64 i32) (result i32) unreachable)
  (func (export "fd_renumber") (param i32 i32) (result i32) unreachable)
  (func (export "fd_seek") (param i32 i64 i32 i32) (result i32) unreachable)
  (func (export "fd_sync") (param i32) (result i32) unreachable)
  (func (export "fd_tell") (param i32 i32) (result i32) unreachable)
  (func (export "fd_write") (param i32 i32 i32 i32) (result i32) unreachable)

  (func (export "path_create_directory") (param i32 i32 i32) (result i32) unreachable)
  (func (export "path_filestat_get") (param i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_filestat_set_times")
        (param i32 i32 i32 i32 i64 i64 i32) (result i32) unreachable)
  (func (export "path_link") (param i32 i32 i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_open")
        (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32) unreachable)
  (func (export "path_readlink") (param i32 i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_remove_directory") (param i32 i32 i32) (result i32) unreachable)
  (func (export "path_rename") (param i32 i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_symlink") (param i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "path_unlink_file") (param i32 i32 i32) (result i32) unreachable)

  (func (export "poll_oneoff") (param i32 i32 i32 i32) (result i32) unreachable)
  (func (export "proc_exit") (param i32) unreachable)
  (func (export "proc_raise") (param i32) (result i32) unreachable)
  (func (export "random_get") (param i32 i32) (result i32) unreachable)
  (func (export "sched_yield") (result i32) unreachable)

  (func (export "sock_accept") (param i32 i32 i32) (result i32) unreachable)
  (func (export "sock_recv") (param i32 i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "sock_send") (param i32 i32 i32 i32 i32) (result i32) unreachable)
  (func (export "sock_shutdown") (param i32 i32) (result i32) unreachable)
)
