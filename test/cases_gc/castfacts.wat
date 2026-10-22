;; What a ref.test or a cast proves of its object, and what it does not
;; (pass_gc_nullchk in src/opt.c), and the answers the arm64 and x64 back ends
;; give a test of a concrete type inline from the object's type slot
;; (rtest_slots in src/isel_core.inc).
;;
;; Kotlin matches a sealed class with a chain of `ref.test`s and casts the
;; object again in the arm that matched, so the cast a test dominates is
;; meant to fold away.  These cases are the edges of that: the false arm,
;; where nothing is proved but -- after a nullable test -- that the object is
;; not null; a nullable test, which null passes, so its true arm proves the
;; type but not that the object is there; a subtype tested and its supertype
;; cast, which folds; a supertype tested and a subtype cast, which still has
;; to check; types no value can share, and abstract types.
;;
;; The objects come out of a mutable global, by an index given on the command
;; line, so nothing but the facts under test knows what they are.  The type
;; order is part of the test: the slots of $base's subtypes are not all
;; adjacent, $twin1 and $twin2 are one type by structure with a slot between
;; them, and $ghost is never allocated, so has no slot at all.
;;
;; Every export checks its own answer and traps when it is wrong, as in
;; gc.wat; test/run_gc.sh says which index each answer is for.  Assembled with
;; wasm-tools, not wat2wasm -- see test/run_gc.sh.

(module
  (type $base (sub (struct (field $a i32))))
  (type $d1 (sub $base (struct (field $a i32) (field $b i32))))
  (type $leaf (sub final $d1 (struct (field $a i32) (field $b i32) (field $z i32))))
  (type $d2 (sub final $base (struct (field $a i32) (field $c i64))))
  (type $other (sub final (struct (field $o f64))))
  (type $d3 (sub final $base (struct (field $a i32) (field $q i32))))
  (type $twin1 (struct (field i64) (field i64)))
  (type $pad (struct (field i8)))
  (type $twin2 (struct (field i64) (field i64)))
  (type $arr (array (mut i32)))
  (type $ghost (sub final (struct (field i32) (field i32) (field i32) (field i32))))
  (type $objs_t (array (mut anyref)))

  (global $objs (mut (ref null $objs_t)) (ref.null $objs_t))

  (func $want (param $got i32) (param $exp i32)
    (if (i32.ne (local.get $got) (local.get $exp)) (then unreachable)))

  ;; 0 null, 1 i31 5, 2 $base, 3 $d1, 4 $d2, 5 $leaf, 6 $other, 7 $twin1,
  ;; 8 $twin2, 9 $arr, 10 $d3, 11 $pad
  (func $obj (param $k i32) (result anyref)
    (local $a (ref null $objs_t))
    (local.set $a (global.get $objs))
    (if (ref.is_null (local.get $a))
      (then
        (local.set $a (array.new_fixed $objs_t 12
          (ref.null any)
          (ref.i31 (i32.const 5))
          (struct.new $base (i32.const 10))
          (struct.new $d1 (i32.const 11) (i32.const 21))
          (struct.new $d2 (i32.const 12) (i64.const 32))
          (struct.new $leaf (i32.const 13) (i32.const 23) (i32.const 33))
          (struct.new $other (f64.const 1.5))
          (struct.new $twin1 (i64.const 1) (i64.const 2))
          (struct.new $twin2 (i64.const 3) (i64.const 4))
          (array.new_fixed $arr 1 (i32.const 7))
          (struct.new $d3 (i32.const 14) (i32.const 44))
          (struct.new $pad (i32.const 9))))
        (global.set $objs (local.get $a))))
    (array.get $objs_t (local.get $a) (local.get $k)))

  ;; The chain Kotlin makes of a `when`, every arm casting again what its test
  ;; just proved.  $leaf before $d1, its supertype; $twin1 answers for a
  ;; $twin2 too, being the same type.
  (func (export "when_chain") (param $k i32) (param $exp i32)
    (local $x anyref) (local $r i32)
    (local.set $x (call $obj (local.get $k)))
    (local.set $r
      (if (result i32) (ref.test (ref $leaf) (local.get $x))
        (then (struct.get $leaf $z (ref.cast (ref null $leaf) (local.get $x))))
        (else
          (if (result i32) (ref.test (ref $d1) (local.get $x))
            (then (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x))))
            (else
              (if (result i32) (ref.test (ref $d2) (local.get $x))
                (then (i32.wrap_i64
                        (struct.get $d2 $c (ref.cast (ref null $d2) (local.get $x)))))
                (else
                  (if (result i32) (ref.test (ref $base) (local.get $x))
                    (then (struct.get $base $a (ref.cast (ref null $base) (local.get $x))))
                    (else
                      (if (result i32) (ref.test (ref $twin1) (local.get $x))
                        (then (i32.wrap_i64
                                (struct.get $twin1 0
                                  (ref.cast (ref null $twin1) (local.get $x)))))
                        (else
                          (if (result i32) (ref.test (ref i31) (local.get $x))
                            (then (i32.add (i32.const 1000)
                                    (i31.get_s (ref.cast (ref null i31) (local.get $x)))))
                            (else
                              (if (result i32) (ref.test (ref $arr) (local.get $x))
                                (then (array.get $arr
                                        (ref.cast (ref null $arr) (local.get $x))
                                        (i32.const 0)))
                                (else
                                  (if (result i32) (ref.test (ref $ghost) (local.get $x))
                                    (then (i32.const -2))
                                    (else (i32.const -7))))))))))))))))))
    (call $want (local.get $r) (local.get $exp)))

  ;; The false arm proves nothing of a non-nullable test: the cast after it
  ;; still checks, and traps for anything but a $d1.
  (func (export "false_cast") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (ref.test (ref $d1) (local.get $x)) (then (return)))
    (drop (ref.cast (ref $d1) (local.get $x))))

  ;; ... and a nullable test of the same type on it is asked, not assumed:
  ;; null passes it.
  (func (export "false_test") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref $d1) (local.get $x))
        (then (i32.const 100))
        (else (ref.test (ref null $d1) (local.get $x))))
      (local.get $exp)))

  ;; A nullable test's true arm: null or a $d1.  A non-nullable test of the
  ;; same type still asks; a nullable test of a supertype does not need to.
  (func (export "null_true") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref null $d1) (local.get $x))
        (then (i32.add
                (i32.mul (ref.test (ref $d1) (local.get $x)) (i32.const 10))
                (ref.test (ref null $base) (local.get $x))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; ... so the field read there keeps its null check,
  (func (export "null_true_get") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (ref.test (ref null $d1) (local.get $x))
      (then (drop (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x)))))))

  ;; ... and a non-nullable cast there still fails on null.
  (func (export "null_true_cast") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (ref.test (ref null $d1) (local.get $x))
      (then (drop (ref.cast (ref $d1) (local.get $x))))))

  ;; A nullable test's false arm: not null, and not a $d1.  The field read of
  ;; a $base found there needs no null check, and the object is not null.
  (func (export "null_false") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref null $d1) (local.get $x))
        (then (i32.const -1))
        (else
          (if (result i32) (ref.test (ref null $base) (local.get $x))
            (then (struct.get $base $a (ref.cast (ref null $base) (local.get $x))))
            (else (i32.add (i32.const 50) (ref.is_null (local.get $x)))))))
      (local.get $exp)))

  ;; A subtype tested, its supertypes cast: both casts are proved.
  (func (export "sub_super") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref $leaf) (local.get $x))
        (then (i32.add
                (struct.get $base $a (ref.cast (ref $base) (local.get $x)))
                (struct.get $d1 $b (ref.cast (ref $d1) (local.get $x)))))
        (else (i32.const 0)))
      (local.get $exp)))

  ;; A supertype tested, a subtype cast: the cast still checks.
  (func (export "super_sub") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (ref.test (ref $base) (local.get $x))
      (then (drop (struct.get $d1 $b (ref.cast (ref $d1) (local.get $x)))))))

  ;; ... and the subtypes' tests are all asked.
  (func (export "super_tests") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref $base) (local.get $x))
        (then (i32.add (i32.add
                (ref.test (ref $d1) (local.get $x))
                (i32.mul (ref.test (ref $d2) (local.get $x)) (i32.const 2)))
                (i32.add
                  (i32.mul (ref.test (ref $leaf) (local.get $x)) (i32.const 4))
                  (i32.mul (ref.test (ref $d3) (local.get $x)) (i32.const 8)))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; Types no value can share answer no: a $d1 is no $d2, $other, i31 or
  ;; array, and is an eq and a struct; an i31 is an eq, no struct, and passes
  ;; a nullable $base test only by being null, which it is not.
  (func (export "disjoint") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref $d1) (local.get $x))
        (then
          (i32.or (i32.or (i32.or
            (ref.test (ref $d2) (local.get $x))
            (i32.shl (ref.test (ref $other) (local.get $x)) (i32.const 1)))
            (i32.or
              (i32.shl (ref.test (ref i31) (local.get $x)) (i32.const 2))
              (i32.shl (ref.test (ref $arr) (local.get $x)) (i32.const 3))))
            (i32.or (i32.or
              (i32.shl (ref.test (ref eq) (local.get $x)) (i32.const 4))
              (i32.shl (ref.test (ref struct) (local.get $x)) (i32.const 5)))
              (i32.shl (ref.test (ref array) (local.get $x)) (i32.const 6)))))
        (else
          (if (result i32) (ref.test (ref i31) (local.get $x))
            (then
              (i32.add (i32.const 100)
                (i32.or (i32.or
                  (ref.test (ref struct) (local.get $x))
                  (i32.shl (ref.test (ref eq) (local.get $x)) (i32.const 1)))
                  (i32.shl (ref.test (ref null $base) (local.get $x)) (i32.const 2)))))
            (else (i32.const 0)))))
      (local.get $exp)))

  ;; An abstract type proved leaves a concrete one to ask.
  (func (export "abstract") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref struct) (local.get $x))
        (then (i32.add (i32.add
                (ref.test (ref $base) (local.get $x))
                (i32.mul (ref.test (ref $twin2) (local.get $x)) (i32.const 2)))
                (i32.mul (ref.test (ref eq) (local.get $x)) (i32.const 4))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; Past a cast's trap the type is proved, as on a test's true arm: with a
  ;; non-nullable cast, the object is not null either.
  (func (export "after_cast") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (drop (ref.cast (ref $d1) (local.get $x)))
    (call $want
      (i32.add (i32.add
        (ref.test (ref $base) (local.get $x))
        (i32.mul (ref.test (ref $leaf) (local.get $x)) (i32.const 2)))
        (i32.mul (ref.test (ref $d2) (local.get $x)) (i32.const 4)))
      (local.get $exp)))

  ;; A nullable cast lets null by: a non-nullable test after it still asks,
  ;; and a nullable test of an unrelated type passes null.
  (func (export "after_null_cast") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (drop (ref.cast (ref null $d1) (local.get $x)))
    (call $want
      (i32.add (i32.add
        (ref.test (ref $base) (local.get $x))
        (i32.mul (ref.test (ref null $base) (local.get $x)) (i32.const 2)))
        (i32.mul (ref.test (ref null $d2) (local.get $x)) (i32.const 4)))
      (local.get $exp)))

  ;; `x != null && x is T` -- Kotlin's `as T` on an erased generic -- leaves
  ;; its answer in a merge, the constant from the null arm and the test's
  ;; answer from the other.  The arm the merge's branch takes knows the
  ;; object is a $d1 and there, so the cast and the null check go; the other
  ;; knows nothing.
  (func (export "merge_and") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32)
          (if (result i32) (ref.is_null (local.get $x))
            (then (i32.const 0))
            (else (ref.test (ref $d1) (local.get $x))))
        (then (i32.add
                (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x)))
                (i32.mul (ref.test (ref $leaf) (local.get $x)) (i32.const 100))))
        (else (i32.sub (i32.const -1) (ref.is_null (local.get $x)))))
      (local.get $exp)))

  ;; `x == null || x is T`: the arm taken may hold null, so a non-nullable
  ;; test still asks, and the other arm knows the object is there.
  (func (export "merge_or") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32)
          (if (result i32) (ref.is_null (local.get $x))
            (then (i32.const 1))
            (else (ref.test (ref $d1) (local.get $x))))
        (then (i32.add (i32.add
                (ref.test (ref null $d1) (local.get $x))
                (i32.mul (ref.test (ref $d1) (local.get $x)) (i32.const 10)))
                (i32.mul (ref.test (ref null $base) (local.get $x)) (i32.const 100))))
        (else (i32.add (i32.const 50) (ref.is_null (local.get $x)))))
      (local.get $exp)))

  ;; ... so a field read there keeps its null check,
  (func (export "merge_or_get") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (if (result i32) (ref.is_null (local.get $x))
          (then (i32.const 1))
          (else (ref.test (ref $d1) (local.get $x))))
      (then (drop (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x)))))))

  ;; ... and a non-nullable cast there still fails on null.
  (func (export "merge_or_cast") (param $k i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (if (if (result i32) (ref.is_null (local.get $x))
          (then (i32.const 1))
          (else (ref.test (ref $d1) (local.get $x))))
      (then (drop (ref.cast (ref $d1) (local.get $x))))))

  ;; Two tests into one merge: a $leaf or a $d1 is a $d1, and not null;
  ;; which of the two it is is still asked.
  (func (export "merge_tests") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32)
          (if (result i32) (ref.test (ref $leaf) (local.get $x))
            (then (i32.const 1))
            (else (ref.test (ref $d1) (local.get $x))))
        (then (i32.add (i32.add
                (ref.test (ref $base) (local.get $x))
                (i32.mul (ref.test (ref null $leaf) (local.get $x)) (i32.const 10)))
                (i32.mul (ref.test (ref $d2) (local.get $x)) (i32.const 100))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; A $leaf or a $d2 have no one type between them that a merge could
  ;; name short of $base, so each test is asked.
  (func (export "merge_unrelated") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32)
          (if (result i32) (ref.test (ref $leaf) (local.get $x))
            (then (i32.const 1))
            (else (ref.test (ref $d2) (local.get $x))))
        (then (i32.add (i32.add
                (ref.test (ref $d1) (local.get $x))
                (i32.mul (ref.test (ref $d2) (local.get $x)) (i32.const 10)))
                (i32.mul (ref.test (ref $base) (local.get $x)) (i32.const 100))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; The same merges in a loop, where they are left standing, the branch on
  ;; the merge's param: objects $from to $to, the $d1s' $b summed.
  (func (export "merge_loop_and") (param $from i32) (param $to i32) (param $exp i32)
    (local $x anyref) (local $i i32) (local $s i32)
    (local.set $i (local.get $from))
    (loop $l
      (local.set $x (call $obj (local.get $i)))
      (if (if (result i32) (ref.is_null (local.get $x))
            (then (i32.const 0))
            (else (ref.test (ref $d1) (local.get $x))))
        (then (local.set $s (i32.add (local.get $s)
                (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.le_u (local.get $i) (local.get $to))))
    (call $want (local.get $s) (local.get $exp)))

  ;; ... and with null let through, which the field read has to trap on.
  (func (export "merge_loop_or") (param $from i32) (param $to i32) (param $exp i32)
    (local $x anyref) (local $i i32) (local $s i32)
    (local.set $i (local.get $from))
    (loop $l
      (local.set $x (call $obj (local.get $i)))
      (if (if (result i32) (ref.is_null (local.get $x))
            (then (i32.const 1))
            (else (ref.test (ref $d1) (local.get $x))))
        (then (local.set $s (i32.add (local.get $s)
                (i32.add
                  (ref.test (ref $d1) (local.get $x))
                  (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x))))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.le_u (local.get $i) (local.get $to))))
    (call $want (local.get $s) (local.get $exp)))

  ;; A value's static type is a proof too, null aside.
  (func $static_d1 (param $p (ref null $d1)) (result i32)
    (i32.add (i32.add
      (ref.test (ref $base) (local.get $p))
      (i32.mul (ref.test (ref null $base) (local.get $p)) (i32.const 2)))
      (i32.mul (ref.test (ref $leaf) (local.get $p)) (i32.const 4))))

  (func (export "static_type") (param $k i32) (param $exp i32)
    (local $x anyref)
    (local.set $x (call $obj (local.get $k)))
    (call $want
      (if (result i32) (ref.test (ref null $d1) (local.get $x))
        (then (call $static_d1 (ref.cast (ref null $d1) (local.get $x))))
        (else (i32.const -1)))
      (local.get $exp)))

  ;; br_on_cast's taken edge is a test's true arm; br_on_cast_fail's fall
  ;; through, for a nullable target, is a nullable test's: null or the type.
  (func (export "br_cast") (param $k i32) (param $exp i32)
    (local $x anyref) (local $r i32)
    (local.set $x (call $obj (local.get $k)))
    (local.set $r
      (block $hit (result i32)
        (drop
          (block $d (result (ref $d1))
            (br_on_cast $d anyref (ref $d1) (local.get $x))
            (br $hit (i32.const -1))))
        (i32.add
          (ref.test (ref $base) (local.get $x))
          (i32.mul (ref.test (ref $leaf) (local.get $x)) (i32.const 2)))))
    (call $want (local.get $r) (local.get $exp)))

  (func (export "br_cast_fail") (param $k i32) (param $exp i32)
    (local $x anyref) (local $r i32)
    (local.set $x (call $obj (local.get $k)))
    (local.set $r
      (block $miss (result i32)
        (drop
          (block $f (result anyref)
            (br_on_cast_fail $f anyref (ref null $d1) (local.get $x))
            ;; null or a $d1: the read checks for null
            (br $miss (struct.get $d1 $b (ref.cast (ref null $d1) (local.get $x))))))
        (i32.add (i32.const 77) (ref.is_null (local.get $x)))))
    (call $want (local.get $r) (local.get $exp)))
)
