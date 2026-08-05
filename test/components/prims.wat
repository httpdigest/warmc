;; The primitives whose core representation is wider than their value.
;;
;; Two of the fourteen are not the identity on the way in.  A `bool` is one of
;; two values and an i32 is 2^32 bit patterns, so the specification lifts any
;; non-zero to true and lowers true to exactly 1.  A `char` is a Unicode
;; scalar value, which is not every i32: above U+10FFFF and in the surrogate
;; range the call traps.  The four narrow integers are a third case -- they
;; arrive in a wider core type and the specification masks them to their own
;; width, sign-extending the two that are signed.
;;
;; None of that can be reached by a generated value.  Every one of those comes
;; out of the reference, which has already normalised the bool, checked the
;; char and masked the integer -- so the inputs that tell the two
;; implementations apart are exactly the ones the generator cannot produce,
;; and they have to be written by hand.  This fixture is the shapes they need
;; to be written *against*.
;;
;; The bare types give every walk a target whose flat form is one value, so a
;; probe can change one slot without changing what the others mean.  The two
;; lists are here for a different walk: `emit_copy` moves a value from memory
;; to memory, and the only way to reach it with a primitive is one element of
;; a list, because everything else copies through the flat form.
(component
  (type $bool-list (list bool))
  (type $char-list (list char))
  (type $narrow (tuple u8 s8 u16 s16))
  (type $mixed (tuple bool char))
  (type $b bool)
  (type $c char)
  (type $u8v u8)
  (type $s8v s8)
  (type $u16v u16)
  (type $s16v s16)

  (core module $m
    (memory (export "mem") 1)
    (global $bump (mut i32) (i32.const 1024))
    (func (export "realloc") (param i32 i32 i32 i32) (result i32)
      global.get $bump
      global.get $bump local.get 3 i32.add global.set $bump)
    (func (export "id") (param $n i32) (result i32) (local.get $n)))
  (core instance $i (instantiate $m))
  (func (export "id") (param "n" u32) (result u32)
    (canon lift (core func $i "id")))
)
