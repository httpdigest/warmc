;; What test/link/gosp_b.wat imports: a call through the reference it is
;; handed, as a host calling back would make.
(module
  (type $t (func (result i32)))
  (func (export "call_it") (param funcref) (result i32)
    (call_ref $t (ref.cast (ref $t) (local.get 0)))))
