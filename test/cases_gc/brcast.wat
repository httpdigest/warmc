;; What reaches a br_on_cast's target is the cast's result, of the cast's type,
;; even where the value is the same pointer as the operand.
;;
;; The IR builder handed the target the operand itself, so where the target's
;; parameter folded into it, the code past the cast read the narrower type's
;; fields from a value typed as the wider one, and nothing said a test had
;; made that layout true.  LICM, asking whether the checks a GC read was let off
;; still hold in the preheader (gc_checks_hold_at in src/opt.c), found no
;; conversion to ask about and moved the reads of a matched object's fields --
;; and the ref.test of what one of them held -- out of the loop, above the cast
;; that matched it.  Run on an object of another type, the read took whatever
;; sat at that offset for a reference and the test dereferenced it: a Java
;; switch over record patterns, which javac makes a loop that restarts the
;; type tests when a guard fails, faulted on the first object that was not the
;; first case's record (javatests/records).
;;
;; $b holds the bits 0x28 where $a holds its reference, and $c two i32s, so a
;; read moved above the cast faults rather than answering wrongly.  Every
;; export checks its own answer and traps when it is wrong; test/run_gc.sh
;; says which object each call is for.  Assembled with wasm-tools.

(module
  (type $base (sub (struct (field $tag i32))))
  (type $a (sub final $base (struct (field $tag i32) (field $ref (ref null $base)))))
  (type $b (sub final $base (struct (field $tag i32) (field $bits i64))))
  (type $c (sub final $base (struct (field $tag i32) (field $x i32) (field $y i32))))
  (type $objs_t (array (mut (ref null $base))))

  (global $objs (mut (ref null $objs_t)) (ref.null $objs_t))

  (func $init
    (local $a2 (ref $a))
    (local.set $a2 (struct.new $a (i32.const 2) (ref.null $base)))
    (global.set $objs
      (array.new_fixed $objs_t 5
        (struct.new $a (i32.const 1) (local.get $a2))
        (struct.new $b (i32.const 3) (i64.const 0x28))
        (struct.new $c (i32.const 4) (i32.const 5) (i32.const 5))
        (local.get $a2)
        (ref.null $base))))
  (start $init)

  (func $obj (param $k i32) (result (ref null $base))
    (array.get $objs_t (global.get $objs) (local.get $k)))

  (func $check (param $got i32) (param $want i32)
    (if (i32.ne (local.get $got) (local.get $want)) (then unreachable)))

  ;; n rounds over one object: an $a whose field is an $a counts.  The read of
  ;; the field and its test are invariant, and ran only past the cast.
  (func (export "loop_field") (param $k i32) (param $n i32) (param $want i32)
    (local $o (ref null $base)) (local $i i32) (local $hits i32)
    (local.set $o (call $obj (local.get $k)))
    (if (ref.is_null (local.get $o)) (then unreachable))
    (loop $l
      (block $next
        (block $isa (result (ref $a))
          (br_on_cast $isa (ref null $base) (ref $a) (local.get $o))
          (drop)
          (br $next))
        (struct.get $a $ref)
        (ref.test (ref $a))
        (local.get $hits)
        (i32.add)
        (local.set $hits))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $check (local.get $hits) (local.get $want)))

  ;; javac's shape for a switch over patterns with a guard: the case is chosen
  ;; by tests from a restart index, and a guard that fails restarts the tests
  ;; past its case.  Case 0 is an $a whose field is an $a, case 1 any other $a,
  ;; case 2 a $c whose x and y agree, case 3 anything else.
  (func (export "restart") (param $k i32) (param $want i32)
    (local $o (ref null $base)) (local $restart i32) (local $case i32)
    (local.set $o (call $obj (local.get $k)))
    (if (ref.is_null (local.get $o)) (then unreachable))
    (block $done
      (loop $tests
        (local.set $case (i32.const 3))
        (block $chosen
          (block $from2
            (block $from1
              (block $from0
                (br_table $from0 $from1 $from2 $chosen (local.get $restart)))
              (if (ref.test (ref $a) (local.get $o))
                (then (local.set $case (i32.const 0)) (br $chosen))))
            (if (ref.test (ref $a) (local.get $o))
              (then (local.set $case (i32.const 1)) (br $chosen))))
          (if (ref.test (ref $c) (local.get $o))
            (then (local.set $case (i32.const 2)) (br $chosen))))
        (block $c3
          (block $c2
            (block $c1
              (block $c0
                (br_table $c0 $c1 $c2 $c3 (local.get $case)))
              ;; case 0, guarded: the field is an $a
              (block $isa (result (ref $a))
                (br_on_cast $isa (ref null $base) (ref $a) (local.get $o))
                (unreachable))
              (if (ref.test (ref $a) (struct.get $a $ref))
                (then (local.set $case (i32.const 0)) (br $done)))
              (local.set $restart (i32.const 1))
              (br $tests))
            ;; case 1
            (block $isa1 (result (ref $a))
              (br_on_cast $isa1 (ref null $base) (ref $a) (local.get $o))
              (unreachable))
            (drop)
            (br $done))
          ;; case 2, guarded: x == y
          (block $isc (result (ref $c))
            (br_on_cast $isc (ref null $base) (ref $c) (local.get $o))
            (unreachable))
          (if (i32.eq (struct.get $c $x) (struct.get $c $y (ref.cast (ref $c) (local.get $o))))
            (then (br $done)))
          (local.set $restart (i32.const 3))
          (br $tests))
        (br $done)))
    (call $check (local.get $case) (local.get $want)))

  ;; A br_on_cast to the function's own label returns the cast value.
  (func $as_a (param $o (ref null $base)) (result (ref null $a))
    (br_on_cast 0 (ref null $base) (ref null $a) (local.get $o))
    (drop)
    (ref.null $a))

  ;; 1 for an $a, with null going down the nullable cast's branch as null,
  ;; 0 for anything else, and the tag read past the cast.
  (func (export "to_return") (param $k i32) (param $want i32) (param $tag i32)
    (local $r (ref null $a))
    (local.set $r (call $as_a (call $obj (local.get $k))))
    (call $check (i32.eqz (ref.is_null (local.get $r))) (local.get $want))
    (if (i32.eqz (ref.is_null (local.get $r)))
      (then (call $check (struct.get $a $tag (local.get $r)) (local.get $tag)))))

  ;; A br_on_cast to a loop label, whose parameter takes the cast value: the
  ;; chain of $a objects through their fields, counted.
  (func (export "to_loop") (param $k i32) (param $want i32)
    (local $n i32)
    (block $out
      (block $first (result (ref $a))
        (br_on_cast $first (ref null $base) (ref $a) (call $obj (local.get $k)))
        (drop)
        (br $out))
      (loop $walk (param (ref $a))
        (local.set $n (i32.add (local.get $n) (i32.const 1)))
        (struct.get $a $ref)
        (br_on_cast $walk (ref null $base) (ref $a))
        (drop)))
    (call $check (local.get $n) (local.get $want)))

  ;; br_on_cast_fail: the failing value down the branch, the cast one past it.
  (func (export "fail_branch") (param $k i32) (param $n i32) (param $want i32)
    (local $o (ref $base)) (local $i i32) (local $sum i32)
    (local.set $o (ref.as_non_null (call $obj (local.get $k))))
    (loop $l
      (block $next
        (block $nota (result (ref $base))
          (br_on_cast_fail $nota (ref $base) (ref $a) (local.get $o))
          (struct.get $a $tag)
          (ref.test (ref $a) (struct.get $a $ref (ref.cast (ref $a) (local.get $o))))
          (i32.add)
          (local.get $sum)
          (i32.add)
          (local.set $sum)
          (br $next))
        (struct.get $base $tag)
        (local.get $sum)
        (i32.add)
        (local.set $sum))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $check (local.get $sum) (local.get $want))))
