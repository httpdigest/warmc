;; A module in Go's shape whose stack pointer gosp.c passes to its Go
;; function, run by the start function, so that nothing exported reaches the
;; pointer.  The Go function hands the import a reference to $peek, which
;; reads the global, and the import calls it: a function the host could be
;; handed a reference to counts as one an export reaches, so the call to the
;; import stores the pointer into the global first, and $peek sees the Go
;; function's frame.
(module
  (@custom "go:buildid" "warmc link test")
  (type $go (func (param i32) (result i32)))
  (type $t (func (result i32)))
  (import "A" "call_it" (func $call_it (param funcref) (result i32)))
  (memory 1)
  (global $sp (mut i32) (i32.const 8192))
  (table 2 funcref)
  (elem (i32.const 1) $g)
  (elem declare func $peek)
  (func $peek (type $t) (global.get $sp))
  (func $g (type $go)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (i32.store (i32.const 0) (call $call_it (ref.func $peek)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 32)))
    (i32.const 0))
  (func $init (drop (call_indirect (type $go) (i32.const 0) (i32.const 1))))
  (start $init)
  (func (export "seen") (result i32) (i32.load (i32.const 0))))
