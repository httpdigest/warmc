;; References the host makes, through any.convert_extern
;; (test/link/externdriver.c): what kind t sees a reference as -- eq, i31,
;; struct, each a digit -- and what rt gives back of one converted there and
;; back.  A host's reference is none of the three and comes back as it went;
;; a struct the module hands out as an externref comes back a struct.
(module
  (type $s (struct (field i64)))
  (func (export "t") (param $e externref) (result i32)
    (local $a anyref)
    (local.set $a (any.convert_extern (local.get $e)))
    (i32.add (i32.add (i32.mul (i32.const 100) (ref.test (ref eq) (local.get $a)))
                      (i32.mul (i32.const 10) (ref.test (ref i31) (local.get $a))))
             (ref.test (ref struct) (local.get $a))))
  (func (export "rt") (param $e externref) (result externref)
    (extern.convert_any (any.convert_extern (local.get $e))))
  (func (export "mk") (result externref)
    (extern.convert_any (struct.new $s (i64.const 5)))))
