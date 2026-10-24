;; A library in the shape of Go's wasm port, whose stack pointer gosp.c
;; passes to its functions in place of global 0, and whose import the host
;; (test/shared/gosphost.c) answers by calling back into it: go_sp reads
;; the global while the Go function $g is inside the import with its frame
;; pushed, and go_move moves it, which $g then pops from.  An import counts
;; as reaching the global because an export does: $g stores its pointer
;; into the global before the call and loads it again after.
(module
  (@custom "go:buildid" "warmc shared test")
  (type $go (func (param i32) (result i32)))
  (import "env" "back" (func $back (param i32) (result i64)))
  (global $sp (mut i32) (i32.const 8192))
  (global $got (mut i64) (i64.const 0))
  (memory 1)

  ;; a Go function: a frame of 32, the import called with the pointer
  (func $g (type $go)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (i32.store (global.get $sp) (i32.const 5))
    (global.set $got (call $back (global.get $sp)))
    (global.set $got (i64.add (global.get $got) (i64.extend_i32_u (i32.load (global.get $sp)))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 40)))
    (i32.const 0))

  (func (export "go_sp") (result i32) (global.get $sp))
  (func (export "go_move") (param i32)
    (global.set $sp (i32.add (global.get $sp) (local.get 0))))

  ;; $got above, where the pointer ended below
  (func (export "go_run") (result i64)
    (global.set $sp (i32.const 8192))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 1))
    (if (call $g (i32.const 0)) (then unreachable))
    (i64.or (i64.shl (global.get $got) (i64.const 32)) (i64.extend_i32_u (global.get $sp)))))
