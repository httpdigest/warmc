;; proc_exit with a status the process cannot exit with as it is: 256, which
;; 8 bits would carry as 0, success, with no arguments; 126, the first a
;; shell takes for its own, with one; 125, the last that passes, with two.
(module
  (import "wasi_snapshot_preview1" "args_sizes_get" (func $sizes (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    (drop (call $sizes (i32.const 0) (i32.const 4)))
    (call $exit
      (select (i32.const 256)
        (select (i32.const 126) (i32.const 125) (i32.eq (i32.load (i32.const 0)) (i32.const 2)))
        (i32.eq (i32.load (i32.const 0)) (i32.const 1))))))
