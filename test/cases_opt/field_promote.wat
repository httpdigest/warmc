;; GC fields carried through a loop (opt.c pass_field_promote): read once
;; ahead of the loop, the loop's loads its value, its stores the value
;; stored, read again past a write on a rare way.  What must hold: a field
;; the loop stores reads back what was stored; a call on a rare way that
;; writes it, and another reference's store there, are seen; another
;; reference's store on the common way stops the promotion; an object the
;; loop tests before each read is read ahead only where a test above the
;; loop proved it of that type -- null, or another type, must not be read.
;; The count is of the fields taken: count's, cold_call's (its call
;; inlined, a store on the rare way), alias's and alias_cold's (their two
;; references one value once CSE has seen the global read twice),
;; cold_call2's (read again past the call) and alias_cold2's (past b's
;; store); guarded's test in the loop folds under the one above it, and LICM
;; takes the read out.
;; stats -O2: fields_promoted=6
;; run: count 10 => i32:45
;; run: count 0 => i32:0
;; run: cold_call 10 3 => i32:110
;; run: cold_call 10 20 => i32:10
;; run: alias 10 => i32:10
;; run: alias_cold 10 3 => i32:1006
;; run: guarded 1 10 => i32:70
;; run: guarded 0 10 => i32:0
;; run: unguarded 0 10 => i32:0
;; run: unguarded 2 10 => i32:0
;; run: unguarded 1 10 => i32:70
;; run: subtype 10 => i32:0
;; run: cold_call2 10 3 => i32:110
;; run: cold_call2 10 9 => i32:110
;; run: alias_cold2 10 3 => i32:1006
;; run: alias_cold2 10 0 => i32:1009
(module
  (type $C (struct (field $n (mut i32))))
  (type $A (sub (struct (field $a (mut i32)))))
  (type $B (sub $A (struct (field $a (mut i32)) (field $b (mut i32)))))
  (type $bumpt (func (param (ref $C))))
  (global $g (mut (ref null $C)) (ref.null $C))
  (global $o (mut eqref) (ref.null eq))
  (global $g2 (mut (ref null $C)) (ref.null $C))
  (table 1 funcref)
  (elem (i32.const 0) $bump)

  (func $c (result (ref $C))
    (global.set $g (struct.new $C (i32.const 0)))
    (global.set $g2 (global.get $g))
    (ref.as_non_null (global.get $g)))

  ;; adds 100 to c.n, d calls down: recursive, so never inlined
  (func $bump2 (param $c (ref $C)) (param $d i32)
    (if (local.get $d)
      (then (call $bump2 (local.get $c) (i32.sub (local.get $d) (i32.const 1))) (return)))
    (struct.set $C $n (local.get $c) (i32.add (struct.get $C $n (local.get $c)) (i32.const 100))))

  ;; what call_indirect calls: a write of the field promotion cannot see into
  (func $bump (param $c (ref $C))
    (struct.set $C $n (local.get $c) (i32.add (struct.get $C $n (local.get $c)) (i32.const 100))))

  ;; c.n += i for i below n: the store read back each time round
  (func (export "count") (param $n i32) (result i32)
    (local $c (ref $C)) (local $i i32)
    (local.set $c (call $c))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $c) (i32.add (struct.get $C $n (local.get $c)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $c)))

  ;; c.n += 1 each time, and at i == k, rarely, a call that adds 100
  (func (export "cold_call") (param $n i32) (param $k i32) (result i32)
    (local $c (ref $C)) (local $i i32)
    (local.set $c (call $c))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $c) (i32.add (struct.get $C $n (local.get $c)) (i32.const 1)))
        (block $rare
          (@metadata.code.branch_hint "\01")
          (br_if $rare (i32.ne (local.get $i) (local.get $k)))
          (call_indirect (type $bumpt) (local.get $c) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $c)))

  ;; a.n read, b.n stored, a and b one object: no promotion
  (func (export "alias") (param $n i32) (result i32)
    (local $a (ref $C)) (local $b (ref $C)) (local $i i32)
    (local.set $a (call $c))
    (local.set $b (ref.as_non_null (global.get $g)))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $b) (i32.add (struct.get $C $n (local.get $a)) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $a)))

  ;; a.n += 1 each time; at i == k, rarely, b.n = 1000, b being a
  (func (export "alias_cold") (param $n i32) (param $k i32) (result i32)
    (local $a (ref $C)) (local $b (ref $C)) (local $i i32)
    (local.set $a (call $c))
    (local.set $b (ref.as_non_null (global.get $g)))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $a) (i32.add (struct.get $C $n (local.get $a)) (i32.const 1)))
        (block $rare
          (@metadata.code.branch_hint "\01")
          (br_if $rare (i32.ne (local.get $i) (local.get $k)))
          (struct.set $C $n (local.get $b) (i32.const 1000)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $a)))

  ;; o: a C of 7 (which 1), null (0) or an A (2)
  (func $set_o (param $which i32)
    (global.set $o (ref.null eq))
    (if (i32.eq (local.get $which) (i32.const 1))
      (then (global.set $o (struct.new $C (i32.const 7)))))
    (if (i32.eq (local.get $which) (i32.const 2))
      (then (global.set $o (struct.new $A (i32.const 7))))))

  ;; tested for a C ahead of the loop, as luawasm's fast loops are, and in
  ;; it before each read
  (func (export "guarded") (param $which i32) (param $n i32) (result i32)
    (local $o eqref) (local $i i32) (local $s i32)
    (call $set_o (local.get $which))
    (local.set $o (global.get $o))
    (if (i32.eqz (ref.test (ref $C) (local.get $o))) (then (return (i32.const 0))))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (block $no
          (@metadata.code.branch_hint "\00")
          (br_if $no (i32.eqz (ref.test (ref $C) (local.get $o))))
          (local.set $s (i32.add (local.get $s) (struct.get $C $n (ref.cast (ref $C) (local.get $o))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; tested only in the loop: null or an A must not be read ahead of it
  (func (export "unguarded") (param $which i32) (param $n i32) (result i32)
    (local $o eqref) (local $i i32) (local $s i32)
    (call $set_o (local.get $which))
    (local.set $o (global.get $o))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (block $no
          (@metadata.code.branch_hint "\00")
          (br_if $no (i32.eqz (ref.test (ref $C) (local.get $o))))
          (local.set $s (i32.add (local.get $s) (struct.get $C $n (ref.cast (ref $C) (local.get $o))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; an A, tested for an A ahead of the loop and for a B in it: B's field b
  ;; is past the end of an A, and must not be read ahead
  (func (export "subtype") (param $n i32) (result i32)
    (local $o eqref) (local $i i32) (local $s i32)
    (call $set_o (i32.const 2))
    (local.set $o (global.get $o))
    (if (i32.eqz (ref.test (ref $A) (local.get $o))) (then (return (i32.const -1))))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (block $no
          (@metadata.code.branch_hint "\00")
          (br_if $no (i32.eqz (ref.test (ref $B) (local.get $o))))
          (local.set $s (i32.add (local.get $s) (struct.get $B $b (ref.cast (ref $B) (local.get $o))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; cold_call with a call that stays: the field read again past it
  (func (export "cold_call2") (param $n i32) (param $k i32) (result i32)
    (local $c (ref $C)) (local $i i32)
    (local.set $c (call $c))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $c) (i32.add (struct.get $C $n (local.get $c)) (i32.const 1)))
        (block $rare
          (@metadata.code.branch_hint "\01")
          (br_if $rare (i32.ne (local.get $i) (local.get $k)))
          (call $bump2 (local.get $c) (i32.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $c)))

  ;; c itself, d calls down: recursive, so no pass sees what it gives
  (func $same (param $c (ref $C)) (param $d i32) (result (ref $C))
    (if (local.get $d)
      (then (return (call $same (local.get $c) (i32.sub (local.get $d) (i32.const 1))))))
    (local.get $c))

  ;; alias_cold with b another reference no pass sees is a
  (func (export "alias_cold2") (param $n i32) (param $k i32) (result i32)
    (local $a (ref $C)) (local $b (ref $C)) (local $i i32)
    (local.set $a (call $c))
    (local.set $b (call $same (local.get $a) (i32.const 3)))
    (block $d
      (loop $l
        (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $C $n (local.get $a) (i32.add (struct.get $C $n (local.get $a)) (i32.const 1)))
        (block $rare
          (@metadata.code.branch_hint "\01")
          (br_if $rare (i32.ne (local.get $i) (local.get $k)))
          (struct.set $C $n (local.get $b) (i32.const 1000)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $C $n (local.get $a)))
)
