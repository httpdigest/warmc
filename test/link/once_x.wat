;; A global of TeaVM's class-initializer pattern that the module exports, so
;; that an embedder may store any function in it: warmc must not take a
;; call through it for one of the pattern's, which does nothing once the
;; global has been disarmed (opt.c once_resolve).  test/link/oncedriver.c
;; stores the function the module's `bump` global holds into `x`, and each
;; of run's three calls through it counts.
;;
;; Assembled with wasm-tools, as the GC pair is.
(module
  (type $vf (func))
  (global $count (mut i32) (i32.const 0))
  (global $X (export "x") (mut (ref null $vf)) (ref.func $X_init))
  (global $bumpref (export "bump") (ref null $vf) (ref.func $bump))
  (func $dummy (type $vf))
  (func $X_init (type $vf)
    (global.set $X (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func $bump (type $vf)
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (elem declare func $dummy $X_init $bump)
  (func (export "run") (result i32)
    (call_ref $vf (global.get $X))
    (call_ref $vf (global.get $X))
    (call_ref $vf (global.get $X))
    (global.get $count)))
