;; A lowered import declared with a type the callee was not lifted with.
;;
;; The plan holds both the lift's function type and the lowering's declared
;; one and compared neither: the adapter was generated from the lowering's
;; alone, and the runtime's import check sees only the flattened core
;; signature -- and `func(a: u32, b: u32) -> u32` and `func(l: list<u8>) -> u32`
;; both flatten to (i32, i32) -> (i32).  So the caller below hands the callee
;; a pointer and a length of its own choosing where the callee expects a list
;; the canonical ABI copied for it, which is the guarantee emit_list_copy
;; exists to make.
;;
;; wasm-tools assembles this and refuses to validate it, so it is not a
;; composition anybody would write by accident -- but nothing between the
;; bytes and the generated adapter had anything to say about it.
(component
  (component $callee
    (core module $m
      (memory (export "mem") 1)
      (func (export "f") (param i32 i32) (result i32) local.get 1)
      (func (export "r") (param i32 i32 i32 i32) (result i32) i32.const 16))
    (core instance $mi (instantiate $m))
    (func (export "f") (param "l" (list u8)) (result u32)
      (canon lift (core func $mi "f") (memory (core memory $mi "mem"))
                  (realloc (core func $mi "r")))))
  (component $caller
    (import "f" (func $f (param "a" u32) (param "b" u32) (result u32)))
    (core func $lf (canon lower (func $f)))
    (core module $n
      (import "" "f" (func $g (param i32 i32) (result i32)))
      (func (export "run") (result i32) (call $g (i32.const 0) (i32.const 0))))
    (core instance $ni (instantiate $n (with "" (instance (export "f" (func $lf))))))
    (func (export "run") (result u32) (canon lift (core func $ni "run"))))
  (instance $c1 (instantiate $callee))
  (alias export $c1 "f" (func $cf))
  (instance $c2 (instantiate $caller (with "f" (func $cf))))
  (alias export $c2 "run" (func $r))
  (export "run" (func $r))
)
