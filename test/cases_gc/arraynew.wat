;; array.new_default of a length the code works out is allocated inline,
;; in the allocation buffer, when it fits (isel.c emit_gc_fastnew_len): its
;; size made from the length, a whole number of units, its header and length
;; written, its elements the buffer's zero bytes; one that does not fit goes
;; to the runtime's call.  For every element width, from a byte to a v128,
;; and for references: every length from 0 to 300, so each rounding of the
;; size to a unit happens; lengths past an allocation buffer (16 KB), which
;; take the call; arrays kept across the collections that thousands of them
;; cause, each checked after; and a length too large for the heap, which
;; traps.  An export fails with unreachable on the first wrong element.
(module
  (type $a8 (array (mut i8)))
  (type $a16 (array (mut i16)))
  (type $a32 (array (mut i32)))
  (type $a64 (array (mut i64)))
  (type $af32 (array (mut f32)))
  (type $af64 (array (mut f64)))
  (type $av128 (array (mut v128)))
  (type $ar (array (mut (ref null $a32))))
  (type $keep (array (mut (ref null $a32))))
  (func $fail (unreachable))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $one8 (param $n i32)
    (local $a (ref $a8)) (local $i i32)
    (local.set $a (array.new_default $a8 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get_u $a8 (local.get $a) (local.get $i)) (i32.const 0)) (then (call $fail)))
      (array.set $a8 (local.get $a) (local.get $i) (i32.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get_u $a8 (local.get $a) (local.get $i)) (i32.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_8")
    (local $n i32)
    (loop $l
      (call $one8 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $one8 (i32.const 16383))
    (call $one8 (i32.const 16385))
    (call $one8 (i32.const 100000)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $one16 (param $n i32)
    (local $a (ref $a16)) (local $i i32)
    (local.set $a (array.new_default $a16 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get_u $a16 (local.get $a) (local.get $i)) (i32.const 0)) (then (call $fail)))
      (array.set $a16 (local.get $a) (local.get $i) (i32.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get_u $a16 (local.get $a) (local.get $i)) (i32.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_16")
    (local $n i32)
    (loop $l
      (call $one16 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $one16 (i32.const 8191))
    (call $one16 (i32.const 8193))
    (call $one16 (i32.const 50000)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $one32 (param $n i32)
    (local $a (ref $a32)) (local $i i32)
    (local.set $a (array.new_default $a32 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get $a32 (local.get $a) (local.get $i)) (i32.const 0)) (then (call $fail)))
      (array.set $a32 (local.get $a) (local.get $i) (i32.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.ne (array.get $a32 (local.get $a) (local.get $i)) (i32.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_32")
    (local $n i32)
    (loop $l
      (call $one32 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $one32 (i32.const 4095))
    (call $one32 (i32.const 4097))
    (call $one32 (i32.const 25000)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $one64 (param $n i32)
    (local $a (ref $a64)) (local $i i32)
    (local.set $a (array.new_default $a64 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i64.ne (array.get $a64 (local.get $a) (local.get $i)) (i64.const 0)) (then (call $fail)))
      (array.set $a64 (local.get $a) (local.get $i) (i64.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i64.ne (array.get $a64 (local.get $a) (local.get $i)) (i64.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_64")
    (local $n i32)
    (loop $l
      (call $one64 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $one64 (i32.const 2047))
    (call $one64 (i32.const 2049))
    (call $one64 (i32.const 12500)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $onef32 (param $n i32)
    (local $a (ref $af32)) (local $i i32)
    (local.set $a (array.new_default $af32 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (f32.ne (array.get $af32 (local.get $a) (local.get $i)) (f32.const 0)) (then (call $fail)))
      (array.set $af32 (local.get $a) (local.get $i) (f32.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (f32.ne (array.get $af32 (local.get $a) (local.get $i)) (f32.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_f32")
    (local $n i32)
    (loop $l
      (call $onef32 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $onef32 (i32.const 4095))
    (call $onef32 (i32.const 4097))
    (call $onef32 (i32.const 25000)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $onef64 (param $n i32)
    (local $a (ref $af64)) (local $i i32)
    (local.set $a (array.new_default $af64 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (f64.ne (array.get $af64 (local.get $a) (local.get $i)) (f64.const 0)) (then (call $fail)))
      (array.set $af64 (local.get $a) (local.get $i) (f64.const 7))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (f64.ne (array.get $af64 (local.get $a) (local.get $i)) (f64.const 7)) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_f64")
    (local $n i32)
    (loop $l
      (call $onef64 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $onef64 (i32.const 2047))
    (call $onef64 (i32.const 2049))
    (call $onef64 (i32.const 12500)))

  ;; a fresh array of n: its length, every element zero, then each written
  (func $onev128 (param $n i32)
    (local $a (ref $av128)) (local $i i32)
    (local.set $a (array.new_default $av128 (local.get $n)))
    (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (v128.any_true (array.get $av128 (local.get $a) (local.get $i))) (then (call $fail)))
      (array.set $av128 (local.get $a) (local.get $i) (v128.const i32x4 1 2 3 4))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (if (i32.eqz (i32x4.all_true (i32x4.eq (array.get $av128 (local.get $a) (local.get $i)) (v128.const i32x4 1 2 3 4)))) (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))
  (func (export "lengths_v128")
    (local $n i32)
    (loop $l
      (call $onev128 (local.get $n))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300))))
    (call $onev128 (i32.const 1023))
    (call $onev128 (i32.const 1025))
    (call $onev128 (i32.const 6250)))

  ;; references: null, each, and then the arrays they name
  (func (export "lengths_ref")
    (local $n i32) (local $a (ref $ar)) (local $i i32)
    (loop $ln
      (local.set $a (array.new_default $ar (local.get $n)))
      (if (i32.ne (array.len (local.get $a)) (local.get $n)) (then (call $fail)))
      (local.set $i (i32.const 0))
      (block $d (loop $l
        (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.eqz (ref.is_null (array.get $ar (local.get $a) (local.get $i)))) (then (call $fail)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
      (br_if $ln (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 300)))))

  ;; n arrays of length i % 97 each, every 61st kept, each kept one's elements
  ;; set to its length; the collections they cause must leave the kept ones
  ;; as they were
  (func (export "keep_across_cycles") (param $n i32)
    (local $k (ref $keep)) (local $i i32) (local $a (ref $a32)) (local $j i32) (local $len i32)
    (local.set $k (array.new_default $keep (i32.add (i32.div_u (local.get $n) (i32.const 61)) (i32.const 1))))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $len (i32.rem_u (local.get $i) (i32.const 97)))
      (local.set $a (array.new_default $a32 (local.get $len)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_u (local.get $j) (local.get $len)))
        (array.set $a32 (local.get $a) (local.get $j) (local.get $len))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 61)))
        (then (array.set $keep (local.get $k) (i32.div_u (local.get $i) (i32.const 61)) (local.get $a))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $a (ref.as_non_null (array.get $keep (local.get $k) (i32.div_u (local.get $i) (i32.const 61)))))
      (local.set $len (i32.rem_u (local.get $i) (i32.const 97)))
      (if (i32.ne (array.len (local.get $a)) (local.get $len)) (then (call $fail)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_u (local.get $j) (local.get $len)))
        (if (i32.ne (array.get $a32 (local.get $a) (local.get $j)) (local.get $len)) (then (call $fail)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (local.set $i (i32.add (local.get $i) (i32.const 61)))
      (br $l))))

  ;; more bytes than the heap holds: the call, which traps
  (func (export "too_long") (param $n i32)
    (drop (array.new_default $a64 (local.get $n))))
)
