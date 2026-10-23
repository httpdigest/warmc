;; call_indirect code tables (src/module_data.c, codegen_plan_icall_tables)
;; under function subtyping.  A table's slot holds an entry's code when the
;; call's type check passes for it, and that is the runtime's check: a final
;; type's id against the entry's, a non-final one's id in the entry's
;; display at its depth.  So a call of $f must reach $f's subtypes $g and $h
;; and not $o, structurally the same function but a type of its own; a call
;; of $g only $g, not its supertype or its sibling; and a call of the final
;; $h only $h.  The answers are asserted, a wrong slot is a trap or an
;; unreachable.
(module
  (type $f (sub (func (param i32) (result i32))))
  (type $g (sub $f (func (param i32) (result i32))))
  (type $h (sub final $f (func (param i32) (result i32))))
  (type $o (func (param i32) (result i32)))
  (memory 1)
  (table 8 funcref)
  (elem (i32.const 1) func $fa $ga $ha $oa)
  ;; the indices, from memory so that nothing folds them
  (data (i32.const 0) "\01\02\03\04\00")

  (func $fa (type $f) (i32.add (local.get 0) (i32.const 1)))
  (func $ga (type $g) (i32.add (local.get 0) (i32.const 2)))
  (func $ha (type $h) (i32.add (local.get 0) (i32.const 3)))
  (func $oa (type $o) (i32.add (local.get 0) (i32.const 4)))

  (func $idx (param $k i32) (result i32) (i32.load8_u (local.get $k)))
  (func $cf (param $k i32) (result i32)
    (call_indirect (type $f) (i32.const 10) (call $idx (local.get $k))))
  (func $cg (param $k i32) (result i32)
    (call_indirect (type $g) (i32.const 10) (call $idx (local.get $k))))
  (func $ch (param $k i32) (result i32)
    (call_indirect (type $h) (i32.const 10) (call $idx (local.get $k))))
  (func $co (param $k i32) (result i32)
    (call_indirect (type $o) (i32.const 10) (call $idx (local.get $k))))

  (func (export "sub_ok") (result i32)
    (if (i32.ne (call $cf (i32.const 0)) (i32.const 11)) (then (unreachable)))
    (if (i32.ne (call $cf (i32.const 1)) (i32.const 12)) (then (unreachable)))
    (if (i32.ne (call $cf (i32.const 2)) (i32.const 13)) (then (unreachable)))
    (if (i32.ne (call $cg (i32.const 1)) (i32.const 12)) (then (unreachable)))
    (if (i32.ne (call $ch (i32.const 2)) (i32.const 13)) (then (unreachable)))
    (if (i32.ne (call $co (i32.const 3)) (i32.const 14)) (then (unreachable)))
    (i32.const 0))
  (func (export "sub_f_other") (result i32) (call $cf (i32.const 3)))
  (func (export "sub_f_null") (result i32) (call $cf (i32.const 4)))
  (func (export "sub_g_super") (result i32) (call $cg (i32.const 0)))
  (func (export "sub_g_sibling") (result i32) (call $cg (i32.const 2)))
  (func (export "sub_h_sibling") (result i32) (call $ch (i32.const 1)))
  (func (export "sub_o_struct") (result i32) (call $co (i32.const 0)))
)
