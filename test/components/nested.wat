;; Nesting and the two things only nesting reaches: an outer alias, and an
;; instance built from a list of inline exports rather than by instantiating.
(component
  (type $t (func (param "n" u32) (result u32)))
  (component $inner
    (core module $m
      (func (export "f") (param i32) (result i32) local.get 0))
    (core instance $ci (instantiate $m))
    (func (export "f") (param "n" u32) (result u32)
      (canon lift (core func $ci "f")))
  )
  (instance $i (instantiate $inner))
  (alias export $i "f" (func $f))
  (export "f" (func $f))
)
