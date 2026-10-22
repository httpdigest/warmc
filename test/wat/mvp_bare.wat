;; The MVP's bare segment indices, which wabt, wasm-tools and older test suites
;; still read: a data segment names its memory and an element segment its table
;; by index alone, with no (memory ...) or (table ...) round it.  The fuzzer's
;; two-memory modules are written this way, and warmc refused them.
;;
;; The two oracles part over the element segment: wasm-tools keeps the table
;; index explicit (flags 2) and wabt drops it for table 0 (flags 0).  Both are
;; the same segment; warmc writes what wasm-tools does.

(module
  (memory 1)
  (memory $m2 1)
  (table 2 funcref)
  (data 0 (i32.const 8) "\2a\00\00\00")
  (data 1 (i32.const 16) "\07\00\00\00")
  (elem 0 (i32.const 1) $f)
  (func $f (result i32) (i32.const 11))
  (func (export "first") (result i32) (i32.load (i32.const 8)))
  (func (export "second") (result i32) (i32.load $m2 (i32.const 16)))
  (func (export "indirect") (result i32)
    (call_indirect (result i32) (i32.const 1))))
