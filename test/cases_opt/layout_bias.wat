;; Without a profile, which way of a test is the rare one (ir.c cond_bias,
;; ir_unlikely_way, ir_emit_order): a block that only a rare way reaches is
;; emitted after the function's other blocks, with the blocks a branch
;; hint calls rare (cold), so the way that runs falls through.  Each
;; function tests an object of the table -- 0 null, 1 the $base of 5 that
;; $obj5 holds, 2 an $a, 3 a $b -- or an index, and stores a mark of the
;; arm it takes, 0x1NN in the then arm and 0x2NN in the else arm, which
;; tells the arms' code apart in the late blocks.
;;
;;   reftest   ref.test holds: the else arm late
;;   refeq     an object equal to the one a global holds: the else arm late
;;   isnull    a null test fails: the then arm late
;;   notnull   the eqz of a null test holds (the opposite of its operand):
;;             the else arm late
;;   nil       Lua's nil test, an and of a null test and a zero test, fails
;;             (an and is as rare as either side): the then arm late
;;   bound     an index at or past an array's length: the then arm late
;;   boundmin  past the lesser of the length and 3 (a select of a length,
;;             luawasm's array part): the then arm late
;;   inside    an index below the length: the else arm late
;;   intbound  an index at or past an integer that is no GC length: neither
;;   andtest   a type test and an equality: the else arm late
;;   ornull    either of two null tests (an or rare when both sides are):
;;             the then arm late
;;   ortest    a type test or a zero test (an or as likely as a side): the
;;             else arm late
;;   cold      a branch hint's rare way, an integer test: the then arm late
;;   inteq     an integer equal to a constant, which the order rules call
;;             rare but no rule moves: neither
;;   andmix    a type test and a null test (an and as rare as its rare
;;             side): the then arm late -- as two branches too (opt.c
;;             pass_test_chain), the null's in a block of its own, where
;;             it is no bare null test but a side of the and
;;   andmix2   the same, the null test first: the type test goes to the
;;             block of its own, and the null test left behind is a side
;;             of the and still
;;   eqzint    the eqz of an integer, no condition's opposite: neither
;;   storeidx  an index at or past a length ahead of a store of that
;;             element, the test of a table that grows by its stores (Lua's
;;             t[i] = x from 1 up), which goes either way: neither
;;   loadidx   the same ahead of a read of the element and then a store:
;;             the read's test, the then arm late
;;   storekind the same with a test of an integer between the index's test
;;             and the store, luawasm's of the table's kind, and a store
;;             down each of its ways: neither
;;   mixkind   the same where one way of the integer test reads the element
;;             before it stores it: a read down any way makes it the read's
;;             test, the then arm late
;;   nullloop  a null test's arm in a loop, a block that stores: a null test
;;             and nothing else moves no block after the rest (null is as
;;             often the way things are), and the arm stays in the loop's
;;             code, ahead of the function's ret
;;   joined    an equality with the global's object, its else a block an
;;             integer test ahead of it reaches too, the likely way by that
;;             test's rule, so the block stays in line: the branch goes to
;;             it (b.ne) and the then arm falls through, as only the
;;             layout's own order (ir_emit_order) has it -- the allocator's
;;             falls into the else, and branches to the then arm (b.eq).
;;             All of it in the arm of a null test that holds, a rare way
;;             no block of which gets a copy for its rare ways in
;;             (pass_hot_tails), which would take the else's out of line
;;
;; Counted in luawasm's kernels (2026-11-09), each of those always went the
;; way it is laid out for.  Answers worked out in Python.
;;
;; late: reftest #0x201\b=1
;; late: reftest #0x101\b=0
;; late: refeq #0x202\b=1
;; late: refeq #0x102\b=0
;; late: isnull #0x103\b=1
;; late: isnull #0x203\b=0
;; late: notnull #0x204\b=1
;; late: notnull #0x104\b=0
;; late: nil #0x105\b=1
;; late: nil #0x205\b=0
;; late: bound #0x106\b=1
;; late: bound #0x206\b=0
;; late: boundmin #0x107\b=1
;; late: boundmin #0x207\b=0
;; late: inside #0x208\b=1
;; late: inside #0x108\b=0
;; late: intbound #0x109\b=0
;; late: intbound #0x209\b=0
;; late: andtest #0x20a\b=1
;; late: andtest #0x10a\b=0
;; late: ornull #0x10b\b=1
;; late: ornull #0x20b\b=0
;; late: ortest #0x20c\b=1
;; late: ortest #0x10c\b=0
;; late: cold #0x10d\b=1
;; late: cold #0x20d\b=0
;; late: inteq #0x10e\b=0
;; late: inteq #0x20e\b=0
;; late: andmix #0x10f\b=1
;; late: andmix #0x20f\b=0
;; late: andmix2 #0x117\b=1
;; late: andmix2 #0x217\b=0
;; late: eqzint #0x110\b=0
;; late: eqzint #0x210\b=0
;; late: storeidx #0x112\b=0
;; late: storeidx #0x212\b=0
;; late: loadidx #0x113\b=1
;; late: loadidx #0x213\b=0
;; late: storekind #0x115\b=0
;; late: storekind #0x215\b=0
;; late: storekind #0x315\b=0
;; late: mixkind #0x116\b=1
;; late: mixkind #0x216\b=0
;; late: nullloop #0x114\b=0
;; shape: joined b\.eq\b=0
;;
;; run: reftest 0 => i32:200
;; run: reftest 1 => i32:201
;; run: reftest 2 => i32:102
;; run: reftest 3 => i32:203
;; run: refeq 0 => i32:200
;; run: refeq 1 => i32:101
;; run: refeq 2 => i32:202
;; run: refeq 3 => i32:203
;; run: isnull 0 => i32:100
;; run: isnull 1 => i32:201
;; run: isnull 2 => i32:202
;; run: isnull 3 => i32:203
;; run: notnull 0 => i32:200
;; run: notnull 1 => i32:101
;; run: notnull 2 => i32:102
;; run: notnull 3 => i32:103
;; run: nil 0 => i32:100
;; run: nil 1 => i32:201
;; run: nil 2 => i32:202
;; run: nil 3 => i32:203
;; run: bound 0 => i32:200
;; run: bound 2 => i32:202
;; run: bound 3 => i32:203
;; run: bound 4 => i32:104
;; run: bound 7 => i32:107
;; run: boundmin 0 => i32:200
;; run: boundmin 2 => i32:202
;; run: boundmin 3 => i32:103
;; run: boundmin 4 => i32:104
;; run: boundmin 7 => i32:107
;; run: inside 0 => i32:100
;; run: inside 2 => i32:102
;; run: inside 3 => i32:103
;; run: inside 4 => i32:204
;; run: inside 7 => i32:207
;; run: intbound 0 2 => i32:200
;; run: intbound 0 3 => i32:200
;; run: intbound 1 2 => i32:201
;; run: intbound 1 3 => i32:201
;; run: intbound 2 2 => i32:102
;; run: intbound 2 3 => i32:202
;; run: intbound 3 2 => i32:103
;; run: intbound 3 3 => i32:103
;; run: andtest 0 => i32:200
;; run: andtest 1 => i32:101
;; run: andtest 2 => i32:202
;; run: andtest 3 => i32:203
;; run: ornull 0 0 => i32:100
;; run: ornull 0 2 => i32:100
;; run: ornull 1 0 => i32:101
;; run: ornull 1 2 => i32:201
;; run: ornull 2 0 => i32:102
;; run: ornull 2 2 => i32:202
;; run: ornull 3 0 => i32:103
;; run: ornull 3 2 => i32:203
;; run: ortest 0 => i32:100
;; run: ortest 1 => i32:201
;; run: ortest 2 => i32:102
;; run: ortest 3 => i32:203
;; run: cold 0 => i32:100
;; run: cold 1 => i32:201
;; run: cold 3 => i32:203
;; run: cold 5 => i32:205
;; run: inteq 0 => i32:200
;; run: inteq 1 => i32:201
;; run: inteq 3 => i32:103
;; run: inteq 5 => i32:205
;; run: andmix 2 0 => i32:102
;; run: andmix 2 1 => i32:202
;; run: andmix 1 0 => i32:201
;; run: andmix 0 0 => i32:200
;; run: andmix2 2 0 => i32:102
;; run: andmix2 2 1 => i32:202
;; run: andmix2 1 0 => i32:201
;; run: andmix2 0 0 => i32:200
;; run: eqzint 0 => i32:100
;; run: eqzint 1 => i32:201
;; run: eqzint 5 => i32:205
;; run: storeidx 0 => i32:200
;; run: storeidx 1 => i32:201
;; run: storeidx 2 => i32:102
;; run: storeidx 9 => i32:109
;; run: loadidx 0 => i32:200
;; run: loadidx 1 => i32:201
;; run: loadidx 2 => i32:102
;; run: storekind 0 0 => i32:200
;; run: storekind 1 0 => i32:201
;; run: storekind 1 1 => i32:301
;; run: storekind 2 0 => i32:102
;; run: mixkind 0 0 => i32:200
;; run: mixkind 1 1 => i32:301
;; run: mixkind 2 1 => i32:102
;; run: nullloop 0 => i32:0
;; run: nullloop 1 => i32:7
;; run: nullloop 8 => i32:20
;; run: joined 0 => i32:200
;; run: joined 1 => i32:101
;; run: joined 2 => i32:202
;; run: joined 3 => i32:203
(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $b (sub $base (struct (field i32) (field i64))))
  (type $objs (array (mut (ref null $base))))
  (global $obj5 (ref $base) (struct.new $base (i32.const 5)))
  (global $tab (ref $objs)
    (array.new_fixed $objs 4 (ref.null $base) (global.get $obj5)
      (struct.new $a (i32.const 6) (i32.const 60)) (struct.new $b (i32.const 7) (i64.const 70))))
  (type $ints (array (mut i32)))
  (global $buf (ref $ints) (array.new_default $ints (i32.const 2)))
  (global $mtab (mut (ref $objs)) (global.get $tab))
  (global $mbuf (mut (ref $ints)) (global.get $buf))
  (func $at (param $i i32) (result (ref null $base))
    (array.get $objs (global.get $tab) (local.get $i)))
  (func $reftest (export "reftest") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (ref.test (ref $a) (local.get $x))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x101)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x201)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $refeq (export "refeq") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (ref.eq (local.get $x) (global.get $obj5))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x102)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x202)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $isnull (export "isnull") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x103)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x203)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $notnull (export "notnull") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.eqz (ref.is_null (local.get $x)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x104)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x204)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $nil (export "nil") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.and (ref.is_null (local.get $x)) (i32.eqz (local.get $i)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x105)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x205)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $bound (export "bound") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (if (i32.ge_u (local.get $i) (array.len (global.get $mtab)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x106)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x206)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $boundmin (export "boundmin") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (if (i32.ge_u (local.get $i) (select (array.len (global.get $mtab)) (i32.const 3) (i32.lt_u (array.len (global.get $mtab)) (i32.const 3))))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x107)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x207)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $inside (export "inside") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (if (i32.lt_u (local.get $i) (array.len (global.get $mtab)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x108)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x208)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $intbound (export "intbound") (param $i i32) (param $n i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (if (i32.ge_u (local.get $i) (local.get $n))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x109)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x209)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $andtest (export "andtest") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.and (ref.test (ref $base) (local.get $x)) (ref.eq (local.get $x) (global.get $obj5)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10a)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20a)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $ornull (export "ornull") (param $i i32) (param $n i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.or (ref.is_null (local.get $x)) (ref.is_null (call $at (local.get $n))))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10b)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20b)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $ortest (export "ortest") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.or (ref.test (ref $a) (local.get $x)) (i32.eqz (local.get $i)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10c)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20c)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $cold (export "cold") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (@metadata.code.branch_hint "\00") (if (i32.lt_u (local.get $i) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10d)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20d)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $inteq (export "inteq") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $r i32)
    (if (i32.eq (local.get $i) (i32.const 3))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10e)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20e)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $andmix (export "andmix") (param $i i32) (param $n i32) (result i32)
    (local $x (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.and (ref.test (ref $a) (local.get $x)) (ref.is_null (call $at (local.get $n))))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x10f)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x20f)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $andmix2 (export "andmix2") (param $i i32) (param $n i32) (result i32)
    (local $x (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (i32.and (ref.is_null (call $at (local.get $n))) (ref.test (ref $a) (local.get $x)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x117)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x217)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $eqzint (export "eqzint") (param $i i32) (result i32)
    (local $r i32)
    (if (i32.eqz (local.get $i))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x110)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x210)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $storeidx (export "storeidx") (param $i i32) (result i32)
    (local $r i32)
    (if (i32.ge_u (local.get $i) (array.len (global.get $mbuf)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x112)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $mbuf) (local.get $i) (i32.const 0x212)) (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $loadidx (export "loadidx") (param $i i32) (result i32)
    (local $r i32)
    (if (i32.ge_u (local.get $i) (array.len (global.get $mbuf)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x113)) (local.set $r (i32.const 1)))
      (else (array.set $ints (global.get $mbuf) (local.get $i)
              (i32.or (array.get $ints (global.get $mbuf) (local.get $i)) (i32.const 0x213)))
            (local.set $r (i32.const 2))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $storekind (export "storekind") (param $i i32) (param $k i32) (result i32)
    (local $r i32)
    (if (i32.ge_u (local.get $i) (array.len (global.get $mbuf)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x115)) (local.set $r (i32.const 1)))
      (else
        (if (i32.eq (local.get $k) (i32.const 1))
          (then (array.set $ints (global.get $mbuf) (local.get $i) (i32.const 0x315)) (local.set $r (i32.const 3)))
          (else (array.set $ints (global.get $mbuf) (local.get $i) (i32.const 0x215)) (local.set $r (i32.const 2))))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $mixkind (export "mixkind") (param $i i32) (param $k i32) (result i32)
    (local $r i32)
    (if (i32.ge_u (local.get $i) (array.len (global.get $mbuf)))
      (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x116)) (local.set $r (i32.const 1)))
      (else
        (if (i32.eq (local.get $k) (i32.const 1))
          (then (array.set $ints (global.get $mbuf) (local.get $i)
                  (i32.or (array.get $ints (global.get $mbuf) (local.get $i)) (i32.const 0x316)))
                (local.set $r (i32.const 3)))
          (else (array.set $ints (global.get $mbuf) (local.get $i) (i32.const 0x216)) (local.set $r (i32.const 2))))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
  (func $nullloop (export "nullloop") (param $n i32) (result i32)
    (local $j i32) (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (if (ref.is_null (call $at (i32.and (local.get $j) (i32.const 3))))
          (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x114))
                (local.set $s (i32.add (local.get $s) (i32.const 7))))
          (else (local.set $s (i32.add (local.get $s) (i32.const 1)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $joined (export "joined") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $r i32)
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (call $at (i32.const 0)))
      (then
        (block $done
          (block $rare
            (br_if $rare (i32.ne (local.get $i) (i32.const 1)))
            (if (ref.eq (local.get $x) (global.get $obj5))
              (then (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x111))
                    (local.set $r (i32.const 1)) (br $done)))
            (br $rare))
          (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x211))
          (local.set $r (i32.const 2)))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $i)))
)
