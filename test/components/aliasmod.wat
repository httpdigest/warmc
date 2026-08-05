;; A core module aliased out of an instantiated component.
;;
;; The decoder advances a component's index space for *every* sort an alias
;; can name, and the planner pushed into its own vectors for two of them --
;; functions and instances -- and returned without pushing for the rest.  So
;; the first `alias export` of a core module or a component put the two
;; numberings one apart, and everything after it read the wrong slot.  The
;; read was always in range, because the count it is checked against had
;; already been bumped, so nothing was refused for the right reason: this
;; component came back with "a core module this component imports is
;; instantiated", which is not what it does.
;;
;; The outer-alias case and the re-export case had the same shape one step
;; further along -- both pushed only when the lookup succeeded, so a failure
;; desynced the space as well as reporting itself.
;;
;; warmc does not implement aliasing a core module out of an instance, and
;; this file does not ask it to.  What it pins is that the refusal names that,
;; and that the index space is still straight when it does.
(component
  (component $inner
    (core module $m0 (func (export "v") (result i32) (i32.const 111)))
    (core module $m1 (func (export "v") (result i32) (i32.const 222)))
    (export "mod" (core module $m1))
  )
  (instance $i (instantiate $inner))
  (alias export $i "mod" (core module $am))
  (core instance $ci (instantiate $am))
  (alias core export $ci "v" (core func $v))
  (func (export "run") (result u32) (canon lift (core func $v)))
)
