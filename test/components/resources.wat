;; A resource, its canonical built-ins, and own/borrow handles.
(component
  (core module $m
    (func (export "dtor") (param i32))
    (memory (export "mem") 1))
  (core instance $i (instantiate $m))
  (type $r (resource (rep i32) (dtor (core func $i "dtor"))))
  (type $own (own $r))
  (type $brw (borrow $r))
  (core func $new (canon resource.new $r))
  (core func $drop (canon resource.drop $r))
  (core func $rep (canon resource.rep $r))
  (export "r" (type $r))
)
