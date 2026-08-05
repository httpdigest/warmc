;; args_sizes_get and environ_sizes_get, printed as two digits.
;;
;; The *counts* rather than the contents: argv[0] is the program's own path
;; and differs between the two engines by construction.  test/run_wasi.sh
;; controls both so that the numbers are comparable -- a fixed argument list,
;; and an environment holding exactly one variable that neither engine is
;; supposed to hand on, since a sandbox does not inherit what it was not
;; asked to.
(module
  (import "wasi_snapshot_preview1" "args_sizes_get"
    (func $args_sizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_sizes_get"
    (func $env_sizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    (drop (call $args_sizes (i32.const 64) (i32.const 68)))
    (drop (call $env_sizes (i32.const 72) (i32.const 76)))
    (i32.store8 (i32.const 16)
      (i32.add (i32.const 48) (i32.load (i32.const 64))))
    (i32.store8 (i32.const 17) (i32.const 32))
    (i32.store8 (i32.const 18)
      (i32.add (i32.const 48) (i32.load (i32.const 72))))
    (i32.store8 (i32.const 19) (i32.const 10))
    (i32.store (i32.const 0) (i32.const 16))
    (i32.store (i32.const 4) (i32.const 4))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8)))))
