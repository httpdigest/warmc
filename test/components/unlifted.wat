;; A component with nothing unresolved and an export that still cannot be run.
;;
;; Every other refusal in the corpus is a *blocker*: an import with no host
;; behind it, a built-in that is not emitted.  This one's graph resolves
;; completely -- one module, one instance, one lift, one export -- and it is
;; refused for the other reason, which is that lifting a `string` is
;; conversion code and warmc emits none as part of a component yet.
;;
;; It matters that this is refused rather than compiled.  The core function
;; returns an i32 and the component function returns a string; running the
;; core one and printing its i32 would be a *wrong answer* rather than a
;; failure, and nothing else in the corpus can tell that apart.
(component
  (core module $m
    (memory (export "mem") 1)
    (func (export "name") (result i32) i32.const 0)
    (func (export "cabi_realloc") (param i32 i32 i32 i32) (result i32)
      i32.const 8))
  (core instance $i (instantiate $m))
  (func $name (result string)
    (canon lift (core func $i "name")
      (memory (core memory $i "mem")) (realloc (core func $i "cabi_realloc"))))
  (export "name" (func $name))
)
