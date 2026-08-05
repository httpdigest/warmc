;; A `canon lower` of a `canon lift` of the *same* component instance -- the
;; one shape where the two implementations warmc is checked against disagree.
;;
;; `wasmtime run --invoke "run(5)"` aborts with `wasm trap: cannot enter
;; component instance`.  The specification's executable reference does not
;; trap: `may_enter_from` tests `self_and_ancestors()` minus the *caller's*
;; `self_and_ancestors()`, and when the caller is the callee that set is empty,
;; so there is nothing in it whose `may_enter` is false and the lift proceeds.
;; Left alone warmc would agree with the reference and answer 11, because the
;; adapter converts perfectly well when both of its memories are one memory.
;;
;; So it is refused by name.  An answer no other implementation agrees with is
;; worse than a refusal, and nothing that matters is lost: this is the
;; degenerate shape, and a component that actually wants to call another one
;; still spans two instances and so has two memories.
;;
;; The graph itself resolves completely -- the refusal is about the ABI and
;; the plan shows so.
(component
  (core module $m
    (memory (export "mem") 1)
    (global $bump (mut i32) (i32.const 1024))
    (func (export "realloc") (param i32 i32 i32 i32) (result i32)
      global.get $bump
      global.get $bump local.get 3 i32.add global.set $bump)
    (func (export "twice") (param $n i32) (result i32)
      (i32.mul (local.get $n) (i32.const 2))))
  (core instance $i (instantiate $m))
  (func $t (param "n" u32) (result u32)
    (canon lift (core func $i "twice")))
  (core func $lt (canon lower (func $t)))
  (core module $c
    (import "" "t" (func $t (param i32) (result i32)))
    (func (export "run") (param $n i32) (result i32)
      (i32.add (call $t (local.get $n)) (i32.const 1))))
  (core instance $ci (instantiate $c
    (with "" (instance (export "t" (func $lt))))))
  (func (export "run") (param "n" u32) (result u32)
    (canon lift (core func $ci "run")))
)
