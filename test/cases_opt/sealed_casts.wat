;; Casts and type tests in a module no other module's object can reach: no
;; reference crosses its imports or exports (compile.c module_gc_sealed), so
;; each test of one of its own types is decided inline from the object's
;; type slot (I_REF_SLOT, codegen.c seal_tests), and, the module making no
;; i31, without an i31 test.  A nullable cast whose result a GC access then
;; checks for null tests for null once, trapping as the access would; with
;; something between the two that can trap, or the access in another block,
;; the cast lets null through as before.
;;
;; The objects by index: 0 null, 1 $base, 2 $d1, 3 $leaf, 4 $d2, 5 $other,
;; 6 $twin1, 7 $twin2, 8 $arr, 9 $pad, 10 $d3.  The type order is part of
;; the test: $d1's subtypes take the slots after its own, $base's do not
;; ($d3 comes after $other), $twin1 and $twin2 are one type by structure
;; with $pad between them, and $ghost is never allocated, so no slot
;; passes a test of it.
;;
;; Every test but those of $base, $twin1 and the function types is decided
;; from the slot, 23, and every nullable cast an access follows checks null
;; for it, 8:
;; stats: slot_tests=23 slot_nullchks=8
;;
;; one slot, a cast that does not take null
;; run: cast_leaf 3 => i32:33
;; run: cast_leaf 0 => trap: cast failure
;; run: cast_leaf 2 => trap: cast failure
;; run: cast_leaf 5 => trap: cast failure
;; run: cast_leaf 8 => trap: cast failure
;; a run of two slots
;; run: cast_d1 2 => i32:21
;; run: cast_d1 3 => i32:23
;; run: cast_d1 1 => trap: cast failure
;; run: cast_d1 4 => trap: cast failure
;; run: cast_d1 0 => trap: cast failure
;; a nullable cast and a read of a field: null traps as the read would
;; run: castn_d1_get 0 => trap: null reference
;; run: castn_d1_get 2 => i32:21
;; run: castn_d1_get 3 => i32:23
;; run: castn_d1_get 1 => trap: cast failure
;; run: castn_d1_get 10 => trap: cast failure
;; and of a write
;; run: castn_leaf_set 0 => trap: null reference
;; run: castn_leaf_set 3 => i32:77
;; run: castn_leaf_set 2 => trap: cast failure
;; of an array's length, an element read, an element written
;; run: castn_arr_len 0 => trap: null reference
;; run: castn_arr_len 8 => i32:2
;; run: castn_arr_len 1 => trap: cast failure
;; run: castn_arr_get 0 0 => trap: null reference
;; run: castn_arr_get 8 1 => i32:8
;; run: castn_arr_get 8 5 => trap: out of bounds array access
;; run: castn_arr_get 6 0 => trap: cast failure
;; run: castn_arr_set 0 => trap: null reference
;; run: castn_arr_set 8 => i32:99
;; run: castn_arr_set 9 => trap: cast failure
;; a ref.as_non_null; after a cast null does not pass, null fails the cast
;; run: castn_leaf_asnn 0 => trap: null reference
;; run: castn_leaf_asnn 3 => i32:33
;; run: castn_leaf_asnn 1 => trap: cast failure
;; run: cast_leaf_asnn 0 => trap: cast failure
;; run: cast_leaf_asnn 3 => i32:33
;; a cast of a cast: the access checks the object both cast
;; run: castn_twice 0 => trap: null reference
;; run: castn_twice 3 => i32:33
;; run: castn_twice 2 => trap: cast failure
;; run: castn_twice 1 => trap: cast failure
;; nothing checks for null after the cast: null passes
;; run: castn_leaf_isnull 0 => i32:1
;; run: castn_leaf_isnull 3 => i32:0
;; run: castn_leaf_isnull 2 => trap: cast failure
;; a division that can trap between the cast and the read: it goes first
;; run: castn_div 0 0 => trap: integer divide by zero
;; run: castn_div 0 1 => trap: null reference
;; run: castn_div 3 1 => i32:133
;; run: castn_div 3 0 => trap: integer divide by zero
;; run: castn_div 2 0 => trap: cast failure
;; the read in another block, after a test for null
;; run: castn_branch 0 => i32:7
;; run: castn_branch 3 => i32:33
;; run: castn_branch 2 => trap: cast failure
;; the value of a test
;; run: test_leaf 0 => i32:0
;; run: test_leaf 3 => i32:1
;; run: test_leaf 2 => i32:0
;; run: test_leaf 5 => i32:0
;; run: testn_leaf 0 => i32:1
;; run: testn_leaf 3 => i32:1
;; run: testn_leaf 4 => i32:0
;; run: test_d1 2 => i32:1
;; run: test_d1 3 => i32:1
;; run: test_d1 1 => i32:0
;; run: test_d1 4 => i32:0
;; run: test_d1 0 => i32:0
;; a type nothing has
;; run: test_ghost 0 => i32:0
;; run: test_ghost 3 => i32:0
;; run: testn_ghost 0 => i32:1
;; run: testn_ghost 3 => i32:0
;; run: cast_ghost 0 => i32:1
;; run: cast_ghost 3 => trap: cast failure
;; slots that are not one run: two types that are one, a supertype
;; run: test_twin1 6 => i32:1
;; run: test_twin1 7 => i32:1
;; run: test_twin1 9 => i32:0
;; run: test_base 1 => i32:1
;; run: test_base 3 => i32:1
;; run: test_base 4 => i32:1
;; run: test_base 10 => i32:1
;; run: test_base 5 => i32:0
;; run: test_base 0 => i32:0
;; run: cast_base 10 => i32:14
;; run: cast_base 5 => trap: cast failure
;; a function type's test, which no slot decides
;; run: test_func 1 => i32:1
;; run: test_func 0 => i32:0
;; run: cast_func 1 => i32:1
;; run: cast_func 0 => trap: cast failure
;; br_on_cast and br_on_cast_fail
;; run: brcast_d1 2 => i32:21
;; run: brcast_d1 3 => i32:23
;; run: brcast_d1 1 => i32:-1
;; run: brcast_d1 0 => i32:-1
;; run: brfail_leaf 3 => i32:33
;; run: brfail_leaf 2 => i32:-1
;; run: brfail_leaf 0 => i32:-1
;; a loop over an array of objects, as a Java array's, one of them null
;; run: loop_sum 9 => i32:132
;; run: loop_sum 2 => trap: null reference
;; run: loop_sum 3 => trap: cast failure
;; run: loop_sum 4 => i32:132
(module
  (type $base (sub (struct (field $a (mut i32)))))
  (type $d1 (sub $base (struct (field $a (mut i32)) (field $b i32))))
  (type $leaf (sub final $d1 (struct (field $a (mut i32)) (field $b i32) (field $z i32))))
  (type $d2 (sub final $base (struct (field $a (mut i32)) (field $c i64))))
  (type $other (sub final (struct (field $o f64))))
  (type $twin1 (struct (field i64) (field i64)))
  (type $pad (struct (field i8)))
  (type $twin2 (struct (field i64) (field i64)))
  (type $arr (array (mut i32)))
  (type $d3 (sub final $base (struct (field $a (mut i32)) (field $q i32))))
  (type $ghost (sub final (struct (field i32) (field i32) (field i32) (field i32))))
  (type $objs_t (array (mut anyref)))
  (type $fi (func (result i32)))
  (type $fj (func (param i32) (result i32)))

  (global $objs (mut (ref null $objs_t)) (ref.null $objs_t))

  (func $obj (param $k i32) (result anyref)
    (local $a (ref null $objs_t))
    (local.set $a (global.get $objs))
    (if (ref.is_null (local.get $a))
      (then
        (local.set $a (array.new_fixed $objs_t 11
          (ref.null any)
          (struct.new $base (i32.const 10))
          (struct.new $d1 (i32.const 11) (i32.const 21))
          (struct.new $leaf (i32.const 13) (i32.const 23) (i32.const 33))
          (struct.new $d2 (i32.const 12) (i64.const 32))
          (struct.new $other (f64.const 1.5))
          (struct.new $twin1 (i64.const 1) (i64.const 2))
          (struct.new $twin2 (i64.const 3) (i64.const 4))
          (array.new_fixed $arr 2 (i32.const 7) (i32.const 8))
          (struct.new $pad (i32.const 9))
          (struct.new $d3 (i32.const 14) (i32.const 44))))
        (global.set $objs (local.get $a))))
    (array.get $objs_t (local.get $a) (local.get $k)))

  (func (export "cast_leaf") (param $k i32) (result i32)
    (struct.get $leaf $z (ref.cast (ref $leaf) (call $obj (local.get $k)))))

  (func (export "cast_d1") (param $k i32) (result i32)
    (struct.get $d1 $b (ref.cast (ref $d1) (call $obj (local.get $k)))))

  (func (export "castn_d1_get") (param $k i32) (result i32)
    (struct.get $d1 $b (ref.cast (ref null $d1) (call $obj (local.get $k)))))

  (func (export "castn_leaf_set") (param $k i32) (result i32)
    (local $x (ref null $leaf))
    (local.set $x (ref.cast (ref null $leaf) (call $obj (local.get $k))))
    (struct.set $leaf $a (local.get $x) (i32.const 77))
    (struct.get $leaf $a (local.get $x)))

  (func (export "castn_arr_len") (param $k i32) (result i32)
    (array.len (ref.cast (ref null $arr) (call $obj (local.get $k)))))

  (func (export "castn_arr_get") (param $k i32) (param $i i32) (result i32)
    (array.get $arr (ref.cast (ref null $arr) (call $obj (local.get $k))) (local.get $i)))

  (func (export "castn_arr_set") (param $k i32) (result i32)
    (local $x (ref null $arr))
    (local.set $x (ref.cast (ref null $arr) (call $obj (local.get $k))))
    (array.set $arr (local.get $x) (i32.const 0) (i32.const 99))
    (array.get $arr (local.get $x) (i32.const 0)))

  (func (export "castn_leaf_asnn") (param $k i32) (result i32)
    (struct.get $leaf $z
      (ref.as_non_null (ref.cast (ref null $leaf) (call $obj (local.get $k))))))

  (func (export "cast_leaf_asnn") (param $k i32) (result i32)
    (struct.get $leaf $z
      (ref.as_non_null (ref.cast (ref $leaf) (call $obj (local.get $k))))))

  (func (export "castn_twice") (param $k i32) (result i32)
    (struct.get $leaf $z
      (ref.cast (ref null $leaf) (ref.cast (ref null $d1) (call $obj (local.get $k))))))

  (func $f1 (type $fi) (i32.const 1))
  (func $f2 (type $fj) (local.get 0))
  (elem declare func $f1 $f2)
  (func $fn (param $k i32) (result funcref)
    (if (result funcref) (local.get $k)
      (then (ref.func $f1))
      (else (ref.func $f2))))

  (func (export "test_func") (param $k i32) (result i32)
    (ref.test (ref $fi) (call $fn (local.get $k))))

  (func (export "cast_func") (param $k i32) (result i32)
    (call_ref $fi (ref.cast (ref $fi) (call $fn (local.get $k)))))

  (func (export "castn_leaf_isnull") (param $k i32) (result i32)
    (ref.is_null (ref.cast (ref null $leaf) (call $obj (local.get $k)))))

  (func (export "castn_div") (param $k i32) (param $d i32) (result i32)
    (local $x (ref null $leaf)) (local $q i32)
    (local.set $x (ref.cast (ref null $leaf) (call $obj (local.get $k))))
    (local.set $q (i32.div_s (i32.const 100) (local.get $d)))
    (i32.add (local.get $q) (struct.get $leaf $z (local.get $x))))

  (func (export "castn_branch") (param $k i32) (result i32)
    (local $x (ref null $leaf))
    (local.set $x (ref.cast (ref null $leaf) (call $obj (local.get $k))))
    (if (ref.is_null (local.get $x)) (then (return (i32.const 7))))
    (struct.get $leaf $z (local.get $x)))

  (func (export "test_leaf") (param $k i32) (result i32)
    (ref.test (ref $leaf) (call $obj (local.get $k))))

  (func (export "testn_leaf") (param $k i32) (result i32)
    (ref.test (ref null $leaf) (call $obj (local.get $k))))

  (func (export "test_d1") (param $k i32) (result i32)
    (ref.test (ref $d1) (call $obj (local.get $k))))

  (func (export "test_ghost") (param $k i32) (result i32)
    (ref.test (ref $ghost) (call $obj (local.get $k))))

  (func (export "testn_ghost") (param $k i32) (result i32)
    (ref.test (ref null $ghost) (call $obj (local.get $k))))

  (func (export "cast_ghost") (param $k i32) (result i32)
    (ref.is_null (ref.cast (ref null $ghost) (call $obj (local.get $k)))))

  (func (export "test_twin1") (param $k i32) (result i32)
    (ref.test (ref $twin1) (call $obj (local.get $k))))

  (func (export "test_base") (param $k i32) (result i32)
    (ref.test (ref $base) (call $obj (local.get $k))))

  (func (export "cast_base") (param $k i32) (result i32)
    (struct.get $base $a (ref.cast (ref null $base) (call $obj (local.get $k)))))

  (func (export "brcast_d1") (param $k i32) (result i32)
    (struct.get $d1 $b
      (block $is (result (ref $d1))
        (br_on_cast $is anyref (ref $d1) (call $obj (local.get $k)))
        (drop)
        (return (i32.const -1)))))

  (func (export "brfail_leaf") (param $k i32) (result i32)
    (drop
      (block $not (result anyref)
        (return
          (struct.get $leaf $z
            (br_on_cast_fail $not anyref (ref $leaf) (call $obj (local.get $k)))))))
    (i32.const -1))

  ;; Four $leafs, 33 each, with the one at $k replaced by the object at
  ;; index $k - 2 of $obj's when $k is below 4: 2 puts null there, 3 a
  ;; $base.  A $k of 4 or more replaces none of them: 4 * 33.
  (func (export "loop_sum") (param $k i32) (result i32)
    (local $a (ref $objs_t)) (local $i i32) (local $s i32)
    (local.set $a (array.new_fixed $objs_t 4
      (struct.new $leaf (i32.const 1) (i32.const 2) (i32.const 33))
      (struct.new $leaf (i32.const 1) (i32.const 2) (i32.const 33))
      (struct.new $leaf (i32.const 1) (i32.const 2) (i32.const 33))
      (struct.new $leaf (i32.const 1) (i32.const 2) (i32.const 33))))
    (if (i32.lt_u (local.get $k) (i32.const 4))
      (then (array.set $objs_t (local.get $a) (local.get $k)
              (call $obj (i32.sub (local.get $k) (i32.const 2))))))
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (struct.get $leaf $z
          (ref.cast (ref null $leaf) (array.get $objs_t (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 4))))
    (local.get $s))
)
