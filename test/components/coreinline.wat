;; A core instance assembled from inline exports, and a module instantiated
;; with another instance as its argument -- the two core-instance forms.
(component
  (core module $provider
    (func (export "helper") (result i32) i32.const 7)
    (memory (export "mem") 1)
    (global (export "g") i32 (i32.const 3))
    (table (export "tbl") 1 funcref))
  (core instance $p (instantiate $provider))
  (core module $consumer
    (import "env" "helper" (func (result i32)))
    (import "env" "mem" (memory 1))
    (func (export "run") (result i32) call 0))
  (alias core export $p "helper" (core func $h))
  (alias core export $p "mem" (core memory $mm))
  (alias core export $p "g" (core global $gg))
  (alias core export $p "tbl" (core table $tt))
  (core instance $env
    (export "helper" (func $h))
    (export "mem" (memory $mm))
    (export "g" (global $gg))
    (export "tbl" (table $tt)))
  (core instance $c (instantiate $consumer (with "env" (instance $env))))
  (type $t (func (result u32)))
  (alias core export $c "run" (core func $run))
  (func (export "run") (result u32) (canon lift (core func $run)))
)
