;; A ref.as_non_null of a reference the way to it already knows is no null
;; -- by its type, past a branch on its null test, an access of it or an
;; earlier ref.as_non_null of it -- makes no trap (opt.c pass_gc_nullchk):
;; luawasm's casts of a metatable it has just tested for nil, to the
;; non-null type a call takes, and of `self`, a param of a type that takes
;; no null.  The references come out of an array the optimiser knows
;; nothing of: a null at 0, a struct holding 7 at 1, one holding 9 at 2.
;;
;;   branch   past the null branch: no trap (one cbz, the branch's)
;;   typed    of a field of a type that takes no null: no trap, no cbz
;;   twice    two in a row: the first traps, the second not
;;   access   past a read of the object: the read's trap alone
;;   nullarm  in the null test's arm where it is null: it traps
;;   merge    past a merge one of whose ways tested it: it traps
;;
;; The traps dropped, branch's, typed's, twice's second and access', 4.
;; Answers worked out by hand.
;;
;; stats -O2: gc_nulltraps=4
;; shape: branch cb(n)?z\s+x\d+,=1
;; shape: typed cb(n)?z\s+x\d+,=0
;; shape: twice cb(n)?z\s+x\d+,=1
;; shape: access cb(n)?z\s+x\d+,=1
;;
;; run: branch 0 => i32:-1
;; run: branch 1 => i32:7
;; run: typed 0 => i32:5
;; run: typed 1 => i32:6
;; run: twice 0 => trap: null reference
;; run: twice 2 => i32:18
;; run: access 0 => trap: null reference
;; run: access 1 => i32:14
;; run: nullarm 0 => trap: null reference
;; run: nullarm 1 => i32:7
;; run: merge 0 => trap: null reference
;; run: merge 1 => i32:7
;; run: merge 2 => i32:9
(module
  (type $s (struct (field i32)))
  (type $refs (array (mut (ref null $s))))
  (type $holder (struct (field (ref $s))))
  (type $hs (array (ref $holder)))
  (global $tab (ref $refs)
    (array.new_fixed $refs 3 (ref.null $s) (struct.new $s (i32.const 7)) (struct.new $s (i32.const 9))))
  (global $holders (ref $hs)
    (array.new_fixed $hs 2 (struct.new $holder (struct.new $s (i32.const 5)))
      (struct.new $holder (struct.new $s (i32.const 6)))))
  (global $sink (mut (ref $s)) (struct.new $s (i32.const 0)))
  (func $keep (param $x (ref $s)) (global.set $sink (local.get $x)))
  (func $branch (export "branch") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))
    (call $keep (ref.as_non_null (local.get $x)))
    (struct.get $s 0 (global.get $sink)))
  (func $typed (export "typed") (param $i i32) (result i32)
    (call $keep (ref.as_non_null
      (struct.get $holder 0 (array.get $hs (global.get $holders) (local.get $i)))))
    (struct.get $s 0 (global.get $sink)))
  (func $twice (export "twice") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (call $keep (ref.as_non_null (local.get $x)))
    (call $keep (ref.as_non_null (local.get $x)))
    (i32.mul (struct.get $s 0 (global.get $sink)) (i32.const 2)))
  (func $access (export "access") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $v i32)
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $v (struct.get $s 0 (local.get $x)))
    (call $keep (ref.as_non_null (local.get $x)))
    (i32.add (local.get $v) (struct.get $s 0 (global.get $sink))))
  (func $nullarm (export "nullarm") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (call $keep (ref.as_non_null (local.get $x)))))
    (struct.get $s 0 (local.get $x)))
  (func $merge (export "merge") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (if (i32.eq (local.get $i) (i32.const 2))
      (then (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))))
    (call $keep (ref.as_non_null (local.get $x)))
    (struct.get $s 0 (global.get $sink)))
)
