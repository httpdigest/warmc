;; A callee whose indirect calls go through one of its parameters, the way
;; TinyGo calls a closure: `apply(n, ctx, f)` loops over f(ctx, i), and LLVM
;; at -opt=2 unrolls the loop into four call_indirects.  A call site that
;; passes f as a constant turns all four direct once the callee is inlined,
;; so the inliner no longer counts them as calls the caller keeps crossing
;; (param_indirect_calls in src/inline.c), and pass_devirt then makes them
;; direct and the closures small enough to inline in turn.
;;
;; What there is to get wrong: the constant must name a function of the
;; right type, which test/cases_traps/traps.wat's ind_param_* check -- a
;; function of another type, a slot nothing fills and one past the table each
;; still trap as the call_indirect does -- and a constant passed for some
;; other parameter (`ctxconst`) says nothing about the calls.
(module
  (type $cl (func (param i32 i32) (result i32)))
  (type $other (func (param i32) (result i32)))
  (table 8 funcref)
  (elem (i32.const 1) func $add $mul $neg1)
  (elem (i32.const 5) func $xorc)
  (global $g (mut i32) (i32.const 7))

  (func $add (type $cl) (param $ctx i32) (param $x i32) (result i32)
    (i32.add (local.get $x) (local.get $ctx)))
  (func $mul (type $cl) (param $ctx i32) (param $x i32) (result i32)
    (i32.mul (local.get $x) (i32.add (local.get $ctx) (i32.const 1))))
  (func $neg1 (type $other) (param $x i32) (result i32)
    (i32.sub (i32.const 0) (local.get $x)))
  (func $xorc (type $cl) (param $ctx i32) (param $x i32) (result i32)
    (i32.xor (local.get $x) (i32.shl (local.get $ctx) (i32.const 3))))

  ;; four calls a trip, through the parameter $f
  (func $apply (param $n i32) (param $ctx i32) (param $f i32) (result i32)
    (local $i i32) (local $s i32)
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (call_indirect (type $cl) (local.get $ctx) (local.get $i) (local.get $f))))
      (local.set $s (i32.add (local.get $s)
        (call_indirect (type $cl) (local.get $ctx) (i32.add (local.get $i) (i32.const 1))
                       (local.get $f))))
      (local.set $s (i32.xor (local.get $s)
        (call_indirect (type $cl) (local.get $ctx) (i32.add (local.get $i) (i32.const 2))
                       (local.get $f))))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))
        (call_indirect (type $cl) (local.get $ctx) (i32.add (local.get $i) (i32.const 3))
                       (local.get $f))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  (func (export "consts") (result i32)
    (local $r i32) (local $h i32)
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $r) (i32.const 50)))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (i32.add (call $apply (i32.const 64) (i32.and (local.get $r) (i32.const 7)) (i32.const 1))
                 (call $apply (i32.const 64) (local.get $r) (i32.const 2)))))
      (local.set $h (i32.xor (local.get $h)
        (call $apply (i32.const 32) (global.get $g) (i32.const 5))))
      (local.set $r (i32.add (local.get $r) (i32.const 1)))
      (br $l)))
    (local.get $h))

  ;; the index from a global: nothing is known at the site
  (func (export "notconst") (result i32)
    (global.set $g (i32.const 2))
    (call $apply (i32.const 40) (i32.const 3) (global.get $g)))

  ;; a constant for the context, not for the function
  (func (export "ctxconst") (result i32)
    (global.set $g (i32.const 5))
    (call $apply (i32.const 40) (i32.const 9) (global.get $g)))

  ;; a site whose index names a function of another type, but whose loop
  ;; never runs: nothing traps
  (func (export "zerotrips") (result i32)
    (call $apply (i32.const 0) (i32.const 1) (i32.const 3)))
)
