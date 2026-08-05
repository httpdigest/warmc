;; An outer alias that climbs two scopes, and two modules whose first function
;; is index zero in both.
;;
;; `linkable.wat` covers the graph; these two are what it cannot reach.
;;
;; **Two levels.**  Its outer alias is `outer 1`, and one scope out is the same
;; answer as any other wrong count would give, because there is only one scope
;; to be wrong about.  Here the middle component defines a core module of its
;; own *first*, so its module 0 and the outer component's module 0 are
;; different modules that export the same name: climbing one scope answers 222
;; and climbing two answers 111.
;;
;; **Index zero twice.**  Both of those modules define their first function at
;; index 0, and both are instantiated and called.  Every module the parser
;; reads calls its first function `wasm_func_0`, so compiling a component's
;; modules into one object without renaming makes those one symbol and the
;; last one committed wins -- which is two exports returning one number.
;; Nothing else in the corpus has two modules defining a function at the same
;; index: `linkable`'s three define 0-1, 4-5 and 2, and it passes either way.
(component
  (core module $a
    (func (export "v") (result i32) i32.const 111))

  (component $mid
    (core module $b
      (func (export "v") (result i32) i32.const 222))
    (core instance $bi (instantiate $b))

    (component $leaf
      ;; Two scopes out: past $mid, whose core module 0 is $b, to the
      ;; outermost component, whose core module 0 is $a.
      (alias outer 2 0 (core module $m))
      (core instance $i (instantiate $m))
      (func (export "v") (result u32) (canon lift (core func $i "v"))))

    (instance $l (instantiate $leaf))
    (alias export $l "v" (func $lv))
    (func $bv (result u32) (canon lift (core func $bi "v")))
    (export "outer" (func $lv))
    (export "local" (func $bv)))

  (instance $m (instantiate $mid))
  (alias export $m "outer" (func $o))
  (alias export $m "local" (func $l))
  (export "outer" (func $o))
  (export "local" (func $l))
)
