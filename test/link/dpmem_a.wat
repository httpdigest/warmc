;; A memory that is not zero when another module's data segments are written
;; into it: its start function fills the first page with 0xa5
;; (test/link/dpmemdriver.c).
(module
  (memory (export "m") 1)
  (func $fill (memory.fill (i32.const 0) (i32.const 0xa5) (i32.const 65536)))
  (start $fill))
