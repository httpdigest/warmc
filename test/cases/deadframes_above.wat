;; -fdead-frames on a stack that is not C's: a caller leaves a callee's
;; arguments at its own stack pointer and the callee reads them, and writes
;; its result, above its own, as Go's and GHC's code do.  A store into a
;; frame is then read by a callee that was given no pointer to it, so the
;; pass leaves the whole module as it is.

(module
  (memory 1)
  (global $sp (mut i32) (i32.const 65536))
  ;; never set: each callee tail-calls itself under it, which keeps it out of
  ;; line, so that its reads stay its own and not its caller's
  (global $deep (mut i32) (i32.const 0))

  (func $add_args
    (local $s i32)
    (local.set $s (global.get $sp))
    (if (global.get $deep) (then (return_call $add_args)))
    (i32.store (local.get $s)
      (i32.add (i32.load (local.get $s)) (i32.load offset=4 (local.get $s)))))

  (func (export "args_above") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 30))
    (i32.store offset=4 (local.get $fp) (i32.const 12))
    (call $add_args)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "twice") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 1))
    (i32.store offset=4 (local.get $fp) (i32.const 2))
    (call $add_args)
    (i32.store offset=4 (local.get $fp) (i32.const 3))
    (call $add_args)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))
)
