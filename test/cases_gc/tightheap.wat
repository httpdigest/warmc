;; A heap with little room to spare: run_gc.sh runs these in a 4 MB heap,
;; stopped-the-world, whose cycles come when the program has allocated a
;; quarter of it or when an allocation fails.
;;
;; frag and frag_structs hold one large array of `big` i32s, then make
;; `rounds` rounds of n objects, keeping every `every`-th of a round until
;; the next round's takes its place: every page they fill has a few of them
;; live, so none comes free unless relocation compacts it, and relocation
;; needs room to compact into.  The heap keeps that room from the program's
;; allocations (rt.c gc_reserve_gran) and gives it up only to one that two
;; cycles have not made room for; the program's allocation buffers are
;; small pages of their own, whose objects compact a granule at a time (in a
;; medium page they took eight free granules together); and an allocation
;; that fails waits for two cycles, since the pages one relocates out of
;; come free only at the next one's mark end.  frag's arrays are of a
;; length the code works out, bumped inline; frag_structs' are struct.new.
;;
;; full_then_throw fills the heap with arrays that stay live, past where the
;; program's allocations stop for the room relocation keeps, then throws:
;; the exception's object is allocated where no cycle may run (rt.c
;; exn_alloc), and takes that room at once.
;;
;; An export fails with unreachable on the first value that is not what it
;; was, and runs out of memory with "allocation failed".
(module
  (type $a32 (array (mut i32)))
  (type $s (struct (field (mut i32)) (field i64) (field i64) (field i64) (field i64)))
  (type $keep (array (mut (ref null $a32))))
  (type $keeps (array (mut (ref null $s))))
  (global $big (mut (ref null $a32)) (ref.null $a32))
  (global $last (mut (ref null $s)) (ref.null $s))
  (func $fail (unreachable))

  ;; thrown through a funcref, a call the optimiser cannot see into: thrown
  ;; where it is caught, it would be a jump with no exception object
  (tag $t (param i32))
  (type $raise_t (func (param i32)))
  (global $raise (mut (ref null $raise_t)) (ref.func $raise))
  (func $raise (type $raise_t) (param $v i32) (throw $t (local.get $v)))

  (func (export "frag") (param $big i32) (param $n i32) (param $every i32)
        (param $len i32) (param $rounds i32)
    (local $k (ref $keep)) (local $r i32) (local $i i32) (local $a (ref $a32))
    (global.set $big (array.new_default $a32 (local.get $big)))
    (local.set $k (array.new_default $keep
      (i32.add (i32.div_u (local.get $n) (local.get $every)) (i32.const 1))))
    (loop $rl
      (local.set $i (i32.const 0))
      (block $d (loop $l
        (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $a (array.new_default $a32 (local.get $len)))
        (array.set $a32 (local.get $a) (i32.const 0) (i32.add (local.get $i) (local.get $r)))
        (if (i32.eqz (i32.rem_u (local.get $i) (local.get $every)))
          (then (array.set $keep (local.get $k)
                  (i32.div_u (local.get $i) (local.get $every)) (local.get $a))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
      (local.set $i (i32.const 0))
      (block $d (loop $l
        (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $a (ref.as_non_null (array.get $keep (local.get $k)
                        (i32.div_u (local.get $i) (local.get $every)))))
        (if (i32.ne (array.len (local.get $a)) (local.get $len)) (then (call $fail)))
        (if (i32.ne (array.get $a32 (local.get $a) (i32.const 0))
                    (i32.add (local.get $i) (local.get $r)))
          (then (call $fail)))
        (local.set $i (i32.add (local.get $i) (local.get $every)))
        (br $l)))
      (local.set $r (i32.add (local.get $r) (i32.const 1)))
      (br_if $rl (i32.lt_u (local.get $r) (local.get $rounds))))
    (if (i32.ne (array.len (ref.as_non_null (global.get $big))) (local.get $big))
      (then (call $fail))))

  ;; each struct also in $last, so that every one escapes, not only the kept
  (func (export "frag_structs") (param $big i32) (param $n i32) (param $every i32)
        (param $rounds i32)
    (local $k (ref $keeps)) (local $r i32) (local $i i32) (local $a (ref $s))
    (global.set $big (array.new_default $a32 (local.get $big)))
    (local.set $k (array.new_default $keeps
      (i32.add (i32.div_u (local.get $n) (local.get $every)) (i32.const 1))))
    (loop $rl
      (local.set $i (i32.const 0))
      (block $d (loop $l
        (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $a (struct.new $s (i32.add (local.get $i) (local.get $r))
                        (i64.const 0) (i64.const 0) (i64.const 0) (i64.const 0)))
        (global.set $last (local.get $a))
        (if (i32.eqz (i32.rem_u (local.get $i) (local.get $every)))
          (then (array.set $keeps (local.get $k)
                  (i32.div_u (local.get $i) (local.get $every)) (local.get $a))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
      (local.set $i (i32.const 0))
      (block $d (loop $l
        (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.ne (struct.get $s 0 (ref.as_non_null (array.get $keeps (local.get $k)
                      (i32.div_u (local.get $i) (local.get $every)))))
                    (i32.add (local.get $i) (local.get $r)))
          (then (call $fail)))
        (local.set $i (i32.add (local.get $i) (local.get $every)))
        (br $l)))
      (local.set $r (i32.add (local.get $r) (i32.const 1)))
      (br_if $rl (i32.lt_u (local.get $r) (local.get $rounds)))))

  ;; n arrays of 4096 bytes, the header's 8 and 1022 i32s, held by two
  ;; arrays of 4096 bytes, 511 references each: sixteen to a 64 KB page,
  ;; which n + 2 a multiple of 16 leaves full, so that the exception's
  ;; object needs a page of its own
  (func (export "full_then_throw") (param $n i32)
    (local $k0 (ref $keep)) (local $k1 (ref $keep)) (local $i i32)
    (local $a (ref $a32)) (local $got i32)
    (local.set $k0 (array.new_default $keep (i32.const 511)))
    (local.set $k1 (array.new_default $keep (i32.const 511)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $a (array.new_default $a32 (i32.const 1022)))
      (array.set $a32 (local.get $a) (i32.const 0) (local.get $i))
      (array.set $a32 (local.get $a) (i32.const 1021) (i32.xor (local.get $i) (i32.const -1)))
      (if (i32.lt_u (local.get $i) (i32.const 511))
        (then (array.set $keep (local.get $k0) (local.get $i) (local.get $a)))
        (else (array.set $keep (local.get $k1) (i32.sub (local.get $i) (i32.const 511))
                (local.get $a))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $got
      (block $caught (result i32)
        (try_table (catch $t $caught)
          (call_ref $raise_t (i32.add (local.get $n) (i32.const 7))
            (ref.as_non_null (global.get $raise))))
        (i32.const -1)))
    (if (i32.ne (local.get $got) (i32.add (local.get $n) (i32.const 7))) (then (call $fail)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $a (ref.as_non_null
        (if (result (ref null $a32)) (i32.lt_u (local.get $i) (i32.const 511))
          (then (array.get $keep (local.get $k0) (local.get $i)))
          (else (array.get $keep (local.get $k1) (i32.sub (local.get $i) (i32.const 511)))))))
      (if (i32.ne (array.get $a32 (local.get $a) (i32.const 0)) (local.get $i))
        (then (call $fail)))
      (if (i32.ne (array.get $a32 (local.get $a) (i32.const 1021))
                  (i32.xor (local.get $i) (i32.const -1)))
        (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
)
