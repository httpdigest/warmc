;; Garbage-collection cases in warmc's own shapes.
;;
;; The upstream corpus states what the *instructions* mean and states it
;; thoroughly -- struct.wast alone is twenty-six execution assertions.  What it
;; cannot reach is what this back end does with them: which field lands at
;; which offset, when a null check is emitted and when the static type removes
;; it, what happens to a reference held across a call, and the widths a packed
;; field is written and read at.
;;
;; Every export here checks its own answer and traps when it is wrong, because
;; there is no oracle to compare against: wabt's interpreter has no garbage
;; collection in it, so the differential harness that judges test/cases cannot
;; judge these.  A wrong answer is `unreachable`; success is returning.
;;
;; Assembled with wasm-tools, not wat2wasm -- see test/run_gc.sh.

(module
  ;; Every value type a field can hold, in an order that makes the alignment
  ;; padding visible: an i8 followed by an i64 has seven bytes between them,
  ;; and a v128 field is what forces the payload to start sixteen-byte aligned.
  (type $mixed (struct
    (field $b   (mut i8))
    (field $l   (mut i64))
    (field $h   (mut i16))
    (field $i   (mut i32))
    (field $f   (mut f32))
    (field $d   (mut f64))
    (field $v   (mut v128))
    (field $r   (mut anyref))))

  (type $pt (sub (struct (field $x (mut i32)) (field $y (mut i32)))))
  ;; A subtype appends a field.  Reading $x through the *supertype's* index on
  ;; an instance of the subtype is what pins the layout rule down: the offsets
  ;; have to agree, which they do only because the fields are laid out in
  ;; declaration order.
  (type $pt3 (sub $pt (struct (field $x (mut i32)) (field $y (mut i32))
                              (field $z (mut i32)))))

  ;; The same rule when the appended field is *wider* than anything the
  ;; supertype holds.  $wpt was laid out before $wpt4 existed, so the v128
  ;; cannot retroactively push the inherited fields along: the object header
  ;; stays one word and the v128 pays for its own alignment padding.
  (type $wpt (sub (struct (field $wx (mut i64)) (field $wr (mut anyref)))))
  (type $wpt4 (sub $wpt (struct (field $wx (mut i64)) (field $wr (mut anyref))
                                (field $wv (mut v128)))))

  (type $box (struct (field $inner (mut (ref null $box))) (field $n (mut i32))))
  (type $imm (struct (field $k i32) (field $s i64)))

  (func $fail (unreachable))

  (func $eq32 (param i32 i32) (if (i32.ne (local.get 0) (local.get 1)) (then (call $fail))))
  (func $eq64 (param i64 i64) (if (i64.ne (local.get 0) (local.get 1)) (then (call $fail))))

  ;; Since escape analysis, an allocation whose reference never leaves the
  ;; function is deleted at -O1 and above -- and a case that exists to run
  ;; the *access* instructions has to keep its object real.  A store to a
  ;; global is an escape no intraprocedural analysis sees through; nulling
  ;; it after leaves the object frame-rooted as before.
  (global $sink (mut anyref) (ref.null any))
  (func $escape (param $r anyref)
    (global.set $sink (local.get $r))
    (global.set $sink (ref.null any)))

  ;; ---- every field type survives a store and a load -------------------

  (func (export "mixed_roundtrip")
    (local $o (ref $mixed))
    (local.set $o (struct.new_default $mixed))
    ;; A default struct is all zeroes, and for the reference field that means
    ;; null -- which is the same bit pattern, and the reason struct.new_default
    ;; needs no stores at all.
    (call $eq32 (struct.get_s $mixed $b (local.get $o)) (i32.const 0))
    (call $eq64 (struct.get $mixed $l (local.get $o)) (i64.const 0))
    (call $eq32 (struct.get_u $mixed $h (local.get $o)) (i32.const 0))
    (call $eq32 (struct.get $mixed $i (local.get $o)) (i32.const 0))
    (if (i32.eqz (ref.is_null (struct.get $mixed $r (local.get $o)))) (then (call $fail)))

    (struct.set $mixed $b (local.get $o) (i32.const 0x7f))
    (struct.set $mixed $l (local.get $o) (i64.const 0x0123456789abcdef))
    (struct.set $mixed $h (local.get $o) (i32.const 0x7fff))
    (struct.set $mixed $i (local.get $o) (i32.const 0x12345678))
    (struct.set $mixed $f (local.get $o) (f32.const 1.5))
    (struct.set $mixed $d (local.get $o) (f64.const -2.25))
    (struct.set $mixed $v (local.get $o)
      (v128.const i64x2 0x1122334455667788 0x99aabbccddeeff00))
    (struct.set $mixed $r (local.get $o) (struct.new_default $mixed))

    (call $eq32 (struct.get_s $mixed $b (local.get $o)) (i32.const 0x7f))
    (call $eq64 (struct.get $mixed $l (local.get $o)) (i64.const 0x0123456789abcdef))
    (call $eq32 (struct.get_u $mixed $h (local.get $o)) (i32.const 0x7fff))
    (call $eq32 (struct.get $mixed $i (local.get $o)) (i32.const 0x12345678))
    (if (f32.ne (struct.get $mixed $f (local.get $o)) (f32.const 1.5)) (then (call $fail)))
    (if (f64.ne (struct.get $mixed $d (local.get $o)) (f64.const -2.25)) (then (call $fail)))
    (call $eq64 (i64x2.extract_lane 0 (struct.get $mixed $v (local.get $o)))
                (i64.const 0x1122334455667788))
    (call $eq64 (i64x2.extract_lane 1 (struct.get $mixed $v (local.get $o)))
                (i64.const 0x99aabbccddeeff00))
    (if (ref.is_null (struct.get $mixed $r (local.get $o))) (then (call $fail))))

  ;; Writing a field of one type must not disturb its neighbours.  With an i8
  ;; beside an i64 an off-by-one in the layout is invisible in the field itself
  ;; and only shows in what it overwrote.
  (func (export "no_overlap")
    (local $o (ref $mixed))
    (local.set $o (struct.new_default $mixed))
    (struct.set $mixed $l (local.get $o) (i64.const -1))
    (struct.set $mixed $b (local.get $o) (i32.const 0))
    (struct.set $mixed $h (local.get $o) (i32.const 0))
    (struct.set $mixed $i (local.get $o) (i32.const 0))
    (call $eq64 (struct.get $mixed $l (local.get $o)) (i64.const -1))
    (struct.set $mixed $b (local.get $o) (i32.const -1))
    (struct.set $mixed $h (local.get $o) (i32.const -1))
    (struct.set $mixed $i (local.get $o) (i32.const -1))
    (struct.set $mixed $l (local.get $o) (i64.const 0))
    (call $eq32 (struct.get_u $mixed $b (local.get $o)) (i32.const 0xff))
    (call $eq32 (struct.get_u $mixed $h (local.get $o)) (i32.const 0xffff))
    (call $eq32 (struct.get $mixed $i (local.get $o)) (i32.const -1)))

  ;; ---- packed fields --------------------------------------------------

  ;; A packed field is written by an i32 and takes its low bits; reading it
  ;; names the sign.  Both directions, because a store that wrote four bytes
  ;; passes the signed read and fails only against a neighbour.
  (func (export "packed_widths")
    (local $o (ref $mixed))
    (local.set $o (struct.new_default $mixed))
    (struct.set $mixed $b (local.get $o) (i32.const 0x1234ff80))
    (call $eq32 (struct.get_s $mixed $b (local.get $o)) (i32.const -128))
    (call $eq32 (struct.get_u $mixed $b (local.get $o)) (i32.const 0x80))
    (struct.set $mixed $h (local.get $o) (i32.const 0x1234f001))
    (call $eq32 (struct.get_s $mixed $h (local.get $o)) (i32.const 0xfffff001))
    (call $eq32 (struct.get_u $mixed $h (local.get $o)) (i32.const 0xf001)))

  ;; ---- struct.new stores every field, in order ------------------------

  (func (export "new_fields")
    (local $o (ref $imm))
    (local.set $o (struct.new $imm (i32.const 11) (i64.const 22)))
    (call $escape (local.get $o))
    (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 11))
    (call $eq64 (struct.get $imm $s (local.get $o)) (i64.const 22)))

  ;; Two allocations are two objects.  Trivial to state and the thing a bump
  ;; allocator that forgot to advance would fail.
  (func (export "distinct")
    (local $a (ref $imm)) (local $b (ref $imm))
    (local.set $a (struct.new $imm (i32.const 1) (i64.const 0)))
    (local.set $b (struct.new $imm (i32.const 2) (i64.const 0)))
    (call $eq32 (struct.get $imm $k (local.get $a)) (i32.const 1))
    (call $eq32 (struct.get $imm $k (local.get $b)) (i32.const 2))
    (if (ref.eq (local.get $a) (local.get $b)) (then (call $fail))))

  ;; ---- subtyping reads through the supertype's offsets ----------------

  (func (export "subtype_offsets")
    (local $p (ref $pt3))
    (local.set $p (struct.new $pt3 (i32.const 7) (i32.const 8) (i32.const 9)))
    ;; Through the subtype's own index.
    (call $eq32 (struct.get $pt3 $x (local.get $p)) (i32.const 7))
    (call $eq32 (struct.get $pt3 $z (local.get $p)) (i32.const 9))
    ;; And through the supertype's, which is the same object at the same
    ;; offsets or this answers 8 and 0.
    (call $eq32 (struct.get $pt $x (local.get $p)) (i32.const 7))
    (call $eq32 (struct.get $pt $y (local.get $p)) (i32.const 8))
    (struct.set $pt $y (local.get $p) (i32.const 88))
    (call $eq32 (struct.get $pt3 $y (local.get $p)) (i32.const 88))
    (call $eq32 (struct.get $pt3 $z (local.get $p)) (i32.const 9)))

  ;; Reading an inherited field through the supertype's index when the subtype
  ;; carries a v128.  If the subtype's own alignment moved its header, the i64
  ;; read here comes back zero and the reference read comes back as whatever
  ;; integer sits in the slot -- and the store below lands in a slot the
  ;; subtype's trace table does not call a reference.
  (func (export "subtype_wide_offsets")
    (local $p (ref $wpt4))
    (local.set $p (struct.new $wpt4 (i64.const 0x1122334455667788)
                                    (struct.new $box (ref.null $box)
                                                     (i32.const 5))
                                    (v128.const i64x2 -1 -1)))
    (call $eq64 (struct.get $wpt4 $wx (local.get $p))
                (i64.const 0x1122334455667788))
    (call $eq64 (struct.get $wpt $wx (local.get $p))
                (i64.const 0x1122334455667788))
    (call $eq32 (struct.get $box $n
                  (ref.cast (ref $box) (struct.get $wpt $wr (local.get $p))))
                (i32.const 5))
    (struct.set $wpt $wr (local.get $p)
      (struct.new $box (ref.null $box) (i32.const 6)))
    (call $eq32 (struct.get $box $n
                  (ref.cast (ref $box) (struct.get $wpt4 $wr (local.get $p))))
                (i32.const 6))
    (call $eq64 (i64x2.extract_lane 0 (struct.get $wpt4 $wv (local.get $p)))
                (i64.const -1)))

  ;; ---- a reference held across a call ---------------------------------

  ;; An allocation is a call, so a reference live across one cannot sit in a
  ;; caller-saved register.  Two allocations with the first still live is the
  ;; smallest shape that says so; the chain makes the register allocator spill.
  (func (export "live_across_alloc")
    (local $a (ref $box)) (local $b (ref $box)) (local $c (ref $box))
    (local.set $a (struct.new $box (ref.null $box) (i32.const 1)))
    (local.set $b (struct.new $box (local.get $a) (i32.const 2)))
    (local.set $c (struct.new $box (local.get $b) (i32.const 3)))
    (call $escape (local.get $c))
    (call $eq32 (struct.get $box $n (local.get $c)) (i32.const 3))
    (call $eq32 (struct.get $box $n (ref.as_non_null (struct.get $box $inner (local.get $c))))
                (i32.const 2))
    (call $eq32 (struct.get $box $n
                  (ref.as_non_null (struct.get $box $inner
                    (ref.as_non_null (struct.get $box $inner (local.get $c))))))
                (i32.const 1)))

  ;; A `struct.set` whose reference *and* whose value are both in frame slots.
  ;;
  ;; The store read the reference into a scratch register and then read the
  ;; value into the same one.  That was invisible for as long as at least one
  ;; of the two was in a register of its own -- `rd` only touches the scratch
  ;; register it is given when the value has been spilled -- and with both
  ;; spilled the value landed on top of the address, so the store wrote the
  ;; value into itself at whatever address the value happened to be.
  ;;
  ;; What spills both is a recursive function that still needs them after its
  ;; own call: the reference and the integer both cross a call, the depth makes
  ;; the register pressure real, and the store is below all of it.
  (func $spill_pair (param $p (ref $box)) (param $k i32) (result i32)
    (local $q (ref $box)) (local $r (ref $box)) (local $t (ref $box))
    (local $u (ref $box)) (local $v (ref $box)) (local $w (ref $box))
    (local $s i32)
    (local.set $q (struct.new $box (local.get $p) (local.get $k)))
    (local.set $r (struct.new $box (local.get $q) (i32.add (local.get $k) (i32.const 1))))
    (local.set $t (struct.new $box (local.get $r) (i32.add (local.get $k) (i32.const 2))))
    (local.set $u (struct.new $box (local.get $t) (i32.add (local.get $k) (i32.const 3))))
    (local.set $v (struct.new $box (local.get $u) (i32.add (local.get $k) (i32.const 4))))
    (local.set $w (struct.new $box (local.get $v) (i32.add (local.get $k) (i32.const 5))))
    (if (local.get $k)
      (then (local.set $s (call $spill_pair (local.get $w)
                                            (i32.sub (local.get $k) (i32.const 1))))))
    ;; Every one of these is still live here, and so is $k, so the allocator
    ;; has more of both than it has callee-saved registers.
    (struct.set $box $n (local.get $q) (local.get $k))
    (struct.set $box $n (local.get $r) (local.get $k))
    (struct.set $box $n (local.get $t) (local.get $k))
    (struct.set $box $n (local.get $u) (local.get $k))
    (struct.set $box $n (local.get $v) (local.get $k))
    (struct.set $box $n (local.get $w) (local.get $k))
    (i32.add (local.get $s)
      (i32.add
        (i32.add (i32.add (struct.get $box $n (local.get $q))
                          (struct.get $box $n (local.get $r)))
                 (i32.add (struct.get $box $n (local.get $t))
                          (struct.get $box $n (local.get $u))))
        (i32.add (struct.get $box $n (local.get $v))
                 (struct.get $box $n (local.get $w))))))

  (func (export "set_both_spilled")
    ;; Each level stores its own $k into six objects: 6*(8+7+...+0) = 216.
    (call $eq32 (call $spill_pair (struct.new $box (ref.null $box) (i32.const 0))
                                  (i32.const 8))
                (i32.const 216)))

  ;; A value live across `table.size` on a table other than table 0.  Table
  ;; 0's size is an inline load; any other table's is a helper call, and a
  ;; call clobbers the caller-saved registers -- which the allocator only
  ;; respects if the opcode is in its clobber list.  Found by exactly this
  ;; shape: the value read before the size call compared against an
  ;; expression of the size after it, and the comparison read whatever the
  ;; helper left in x1.
  (table $tsz0 4 funcref)
  (table $tsz 4 (ref null $box))

  (func (export "live_across_size")
    (local $v i32)
    (table.set $tsz (i32.const 1) (struct.new $box (ref.null $box) (i32.const 55)))
    (local.set $v (struct.get $box $n
      (ref.as_non_null (table.get $tsz (i32.const 1)))))
    ;; $v is live across the call; the sum forces both to exist at once.
    (call $eq32
      (i32.add (local.get $v) (i32.mul (table.size $tsz) (i32.const 100)))
      (i32.const 455)))

  ;; The same register pressure, aimed at the array instructions.  The bug
  ;; `set_both_spilled` pinned down was a store whose two spilled operands
  ;; shared one scratch register; every instruction below takes one more
  ;; operand than that store did, so each is its own chance to make that
  ;; mistake again.
  (type $abox (array (mut (ref null $box))))

  (func $spill_arr (param $a (ref $abox)) (param $k i32) (result i32)
    (local $q (ref $box)) (local $r (ref $box)) (local $t (ref $box))
    (local $u (ref $box)) (local $v (ref $box)) (local $w (ref $box))
    (local $s i32)
    (local.set $q (struct.new $box (ref.null $box) (local.get $k)))
    (local.set $r (struct.new $box (local.get $q) (i32.add (local.get $k) (i32.const 1))))
    (local.set $t (struct.new $box (local.get $r) (i32.add (local.get $k) (i32.const 2))))
    (local.set $u (struct.new $box (local.get $t) (i32.add (local.get $k) (i32.const 3))))
    (local.set $v (struct.new $box (local.get $u) (i32.add (local.get $k) (i32.const 4))))
    (local.set $w (struct.new $box (local.get $v) (i32.add (local.get $k) (i32.const 5))))
    (if (local.get $k)
      (then (local.set $s (call $spill_arr (local.get $a)
                                           (i32.sub (local.get $k) (i32.const 1))))))
    ;; $a, $k and all six objects are live, so array, index and value of each
    ;; `array.set` are spilled together -- three operands, three reloads, and
    ;; none may land on another.
    (array.set $abox (local.get $a) (i32.and (local.get $k) (i32.const 7))
               (local.get $q))
    (array.set $abox (local.get $a)
               (i32.and (i32.add (local.get $k) (i32.const 1)) (i32.const 7))
               (local.get $w))
    (i32.add (local.get $s)
      (i32.add
        (i32.add
          (i32.add (i32.add (struct.get $box $n (local.get $q))
                            (struct.get $box $n (local.get $r)))
                   (i32.add (struct.get $box $n (local.get $t))
                            (struct.get $box $n (local.get $u))))
          (i32.add (struct.get $box $n (local.get $v))
                   (struct.get $box $n (local.get $w))))
        (i32.add
          (struct.get $box $n (ref.as_non_null
            (array.get $abox (local.get $a)
                       (i32.and (local.get $k) (i32.const 7)))))
          (struct.get $box $n (ref.as_non_null
            (array.get $abox (local.get $a)
                       (i32.and (i32.add (local.get $k) (i32.const 1))
                                (i32.const 7)))))))))

  (func (export "array_ops_spilled")
    ;; Per level: q..w hold k..k+5 (sum 6k+15), and the two elements read
    ;; back hold k and k+5 (sum 2k+5); over k = 0..8 that is 468.
    (call $eq32 (call $spill_arr (array.new_default $abox (i32.const 8))
                                 (i32.const 8))
                (i32.const 468)))

  ;; `array.fill` and `array.copy` under the same pressure: four and five
  ;; operands, every one reloaded from a slot.
  (func $spill_fc (param $a (ref $abox)) (param $k i32)
    (local $q (ref $box)) (local $r (ref $box)) (local $t (ref $box))
    (local $u (ref $box)) (local $v (ref $box)) (local $w (ref $box))
    (local.set $q (struct.new $box (ref.null $box) (local.get $k)))
    (local.set $r (struct.new $box (local.get $q) (i32.add (local.get $k) (i32.const 1))))
    (local.set $t (struct.new $box (local.get $r) (i32.add (local.get $k) (i32.const 2))))
    (local.set $u (struct.new $box (local.get $t) (i32.add (local.get $k) (i32.const 3))))
    (local.set $v (struct.new $box (local.get $u) (i32.add (local.get $k) (i32.const 4))))
    (local.set $w (struct.new $box (local.get $v) (i32.add (local.get $k) (i32.const 5))))
    (if (local.get $k)
      (then (call $spill_fc (local.get $a) (i32.sub (local.get $k) (i32.const 1)))))
    (array.fill $abox (local.get $a) (i32.and (local.get $k) (i32.const 3))
                (local.get $r) (i32.const 2))
    (if (i32.eqz (ref.eq (array.get $abox (local.get $a)
                                    (i32.and (local.get $k) (i32.const 3)))
                         (local.get $r)))
      (then (call $fail)))
    (array.copy $abox $abox (local.get $a) (i32.const 4)
                (local.get $a) (i32.const 0) (i32.const 2))
    (if (i32.eqz (ref.eq (array.get $abox (local.get $a) (i32.const 4))
                         (array.get $abox (local.get $a) (i32.const 0))))
      (then (call $fail)))
    ;; And the casts, on operands that have sat in slots the whole time.
    (call $eq32 (struct.get $box $n (ref.cast (ref $box)
                  (array.get $abox (local.get $a)
                             (i32.and (local.get $k) (i32.const 3)))))
                (i32.add (local.get $k) (i32.const 1)))
    (call $eq32 (struct.get $box $n (local.get $w))
                (i32.add (local.get $k) (i32.const 5))))

  (func (export "array_fill_copy_spilled")
    (call $spill_fc (array.new_default $abox (i32.const 8)) (i32.const 6)))

  ;; A value that is sometimes an i31 and sometimes a struct, tested and cast
  ;; after a recursion has pushed it into a slot: the test, the cast and the
  ;; select of results all read the same spilled word.
  (func $spill_cast (param $x (ref null eq)) (param $k i32) (result i32)
    (local $q (ref $box)) (local $r (ref $box)) (local $t (ref $box))
    (local $u (ref $box)) (local $v (ref $box)) (local $w (ref $box))
    (local $s i32)
    (local.set $q (struct.new $box (ref.null $box) (local.get $k)))
    (local.set $r (struct.new $box (local.get $q) (local.get $k)))
    (local.set $t (struct.new $box (local.get $r) (local.get $k)))
    (local.set $u (struct.new $box (local.get $t) (local.get $k)))
    (local.set $v (struct.new $box (local.get $u) (local.get $k)))
    (local.set $w (struct.new $box (local.get $v) (local.get $k)))
    (call $escape (local.get $w))
    (if (local.get $k)
      (then (local.set $s (call $spill_cast
        (if (result (ref null eq)) (i32.and (local.get $k) (i32.const 1))
          (then (ref.i31 (i32.add (local.get $k) (i32.const 99))))
          (else (local.get $q)))
        (i32.sub (local.get $k) (i32.const 1))))))
    (drop (struct.get $box $n (local.get $w)))
    (i32.add (local.get $s)
      (if (result i32) (ref.test (ref i31) (local.get $x))
        (then (i31.get_u (ref.cast (ref i31) (local.get $x))))
        (else (struct.get $box $n (ref.cast (ref $box)
                (ref.as_non_null (local.get $x))))))))

  (func (export "cast_spilled")
    ;; Level 8 receives box(0) from here; level k below receives what level
    ;; k+1 made -- i31(k+100) when k+1 is odd, box(n=k+1) when it is even.
    ;; i31s land at k = 0,2,4,6: 100+102+104+106 = 412; boxes at k = 1,3,5,7:
    ;; 2+4+6+8 = 20; and box(0) is 0.  Total 432.
    (call $eq32 (call $spill_cast (struct.new $box (ref.null $box) (i32.const 0))
                                  (i32.const 8))
                (i32.const 432)))

  ;; ---- i31 ------------------------------------------------------------

  ;; Thirty-one bits, so the sign is bit 30 and the top bit of the i32 is
  ;; dropped.  The four boundaries are where a shift of the wrong width shows.
  (func (export "i31_range")
    (call $eq32 (i31.get_s (ref.i31 (i32.const 0))) (i32.const 0))
    (call $eq32 (i31.get_s (ref.i31 (i32.const 1073741823))) (i32.const 1073741823))
    (call $eq32 (i31.get_s (ref.i31 (i32.const 1073741824))) (i32.const -1073741824))
    (call $eq32 (i31.get_s (ref.i31 (i32.const -1))) (i32.const -1))
    (call $eq32 (i31.get_u (ref.i31 (i32.const -1))) (i32.const 2147483647))
    (call $eq32 (i31.get_u (ref.i31 (i32.const 1073741824))) (i32.const 1073741824))
    ;; The top bit of the operand is not part of the value.
    (call $eq32 (i31.get_u (ref.i31 (i32.const 0x80000000))) (i32.const 0))
    (call $eq32 (i31.get_u (ref.i31 (i32.const 0x80000001))) (i32.const 1)))

  ;; An i31 is not a heap object, and it is not null: the low tag bit is what
  ;; keeps the three apart, so a null test on one has to say "not null".
  (func (export "i31_not_null")
    (if (ref.is_null (ref.i31 (i32.const 0))) (then (call $fail)))
    (if (ref.eq (ref.i31 (i32.const 5)) (ref.i31 (i32.const 6))) (then (call $fail)))
    (if (i32.eqz (ref.eq (ref.i31 (i32.const 5)) (ref.i31 (i32.const 5)))) (then (call $fail))))

  ;; ---- null checks ----------------------------------------------------

  (func (export "get_null") (result i32)
    (struct.get $imm $k (ref.null $imm)))
  (func (export "set_null")
    (struct.set $mixed $i (ref.null $mixed) (i32.const 1)))
  (func (export "i31_get_null") (result i32)
    (i31.get_s (ref.null i31)))
  ;; A null that arrives through a local rather than as a literal, so the
  ;; check cannot be folded away by knowing the value.
  (func (export "get_null_local") (result i32)
    (local $o (ref null $imm))
    (struct.get $imm $k (local.get $o)))

  ;; The same null, stored into by a loop with no safepoint in it -- which is
  ;; the shape whose write barrier gets lifted into the preheader, ahead of the
  ;; store and so ahead of the store's null check.  A barrier that reads the
  ;; object's header without asking turns this trap into a fault: `-O0` said
  ;; "null reference" and `-O2` died with a signal.  The value stored has to be
  ;; a reference, or there is no barrier; the object has to be loop-invariant,
  ;; or nothing is lifted; and it arrives as a parameter so that no pass can
  ;; know it is null: the branch that would fill it in is one the caller's
  ;; count never takes, and no pass can decide that.
  (func (export "set_null_loop") (param $n i32)
    (local $o (ref null $box)) (local $i i32)
    (if (i32.gt_u (local.get $n) (i32.const 1000000))
      (then (local.set $o (struct.new $box (ref.null $box) (i32.const 1)))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (struct.set $box $inner (local.get $o) (ref.null $box))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; ---- initialisers ---------------------------------------------------

  ;; Allocation is constant since garbage collection, so these run in the
  ;; runtime's own evaluator rather than in compiled code -- a second
  ;; implementation of the same layout, which is why it is worth a case.
  (global $g (ref $imm) (struct.new $imm (i32.const 314) (i64.const 159)))
  (global $gd (ref $imm) (struct.new_default $imm))
  (global $gi (ref i31) (ref.i31 (i32.const -7)))

  (func (export "const_struct")
    (call $eq32 (struct.get $imm $k (global.get $g)) (i32.const 314))
    (call $eq64 (struct.get $imm $s (global.get $g)) (i64.const 159))
    (call $eq32 (struct.get $imm $k (global.get $gd)) (i32.const 0))
    (call $eq32 (i31.get_s (global.get $gi)) (i32.const -7)))

  ;; Every width the runtime's evaluator has a case for, in one constant, and
  ;; read back through compiled code -- so the compiler's layout and the
  ;; runtime's field table have to agree on all four.
  ;;
  ;; The v128 is the field that makes this test anything.  The evaluator writes
  ;; a field by the width the table gives it, and the stores go in increasing
  ;; offset order, so a width that is too *large* is overwritten by the next
  ;; field and a width that is too small leaves the tail of the field alone --
  ;; which is invisible for every scalar, whose tail is padding, and visible
  ;; for a v128, whose tail is its high lane.  Without this the whole field
  ;; table could be a column of eights and nothing would notice.
  (type $cw (struct (field $p (mut i8)) (field $q (mut i16))
                    (field $r (mut i32)) (field $t (mut i64))
                    (field $u (mut v128))))
  (global $gw (ref $cw)
    (struct.new $cw (i32.const 0x7e) (i32.const 0x7ffe) (i32.const 0x11223344)
                    (i64.const 0x5566778899aabbcc)
                    (v128.const i64x2 0xdeadbeefcafef00d 0x0102030405060708)))

  ;; ---- arrays ---------------------------------------------------------

  (type $ai32 (array (mut i32)))
  (type $ai64 (array (mut i64)))
  (type $ai8  (array (mut i8)))
  (type $ai16 (array (mut i16)))
  (type $af32 (array (mut f32)))
  (type $af64 (array (mut f64)))
  (type $av   (array (mut v128)))
  (type $aref (array (mut anyref)))
  (type $afix (array i32))

  (data $d "\01\02\03\04\05\06\07\08\09\0a\0b\0c\0d\0e\0f\10")
  ;; A *typed* element segment, so the array's element type can be
  ;; `(ref null $cmp)` and an element can be called without a cast.  The two
  ;; functions have the same signature and opposite meanings, which is how a
  ;; segment offset that was ignored gets caught: calling the wrong one of
  ;; them with the same arguments traps.
  (type $cmp (func (param i32 i32)))
  (func $eq32c (type $cmp)
    (if (i32.ne (local.get 0) (local.get 1)) (then (call $fail))))
  (func $ne32c (type $cmp)
    (if (i32.eq (local.get 0) (local.get 1)) (then (call $fail))))
  (elem $e (ref $cmp) (ref.func $eq32c) (ref.func $ne32c))
  (type $ffn (array (mut (ref null $cmp))))

  ;; Every element width, through the allocator's fill path -- which takes the
  ;; value as two general-register words, so a float has to be reinterpreted
  ;; and a v128 taken apart into lanes before the call.  That conversion is
  ;; the part of `array.new` with anything in it.
  (func (export "array_new_widths")
    (local $b (ref $ai8)) (local $h (ref $ai16)) (local $i (ref $ai32))
    (local $l (ref $ai64)) (local $f (ref $af32)) (local $d (ref $af64))
    (local $v (ref $av))
    (local.set $b (array.new $ai8 (i32.const 0x1234ff81) (i32.const 4)))
    (local.set $h (array.new $ai16 (i32.const 0x1234f002) (i32.const 4)))
    (local.set $i (array.new $ai32 (i32.const -3) (i32.const 4)))
    (local.set $l (array.new $ai64 (i64.const -4) (i32.const 4)))
    (local.set $f (array.new $af32 (f32.const 2.5) (i32.const 4)))
    (local.set $d (array.new $af64 (f64.const -3.75) (i32.const 4)))
    (local.set $v (array.new $av
      (v128.const i64x2 0x0f0e0d0c0b0a0908 0x0706050403020100) (i32.const 4)))
    (call $eq32 (array.len (local.get $b)) (i32.const 4))
    (call $eq32 (array.len (local.get $v)) (i32.const 4))
    ;; Every element, not only the first: a fill loop that wrote one element
    ;; and a length that was never stored look the same from index 0.
    (call $eq32 (array.get_s $ai8 (local.get $b) (i32.const 3)) (i32.const -127))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 0)) (i32.const 0x81))
    (call $eq32 (array.get_s $ai16 (local.get $h) (i32.const 3)) (i32.const 0xfffff002))
    (call $eq32 (array.get $ai32 (local.get $i) (i32.const 3)) (i32.const -3))
    (call $eq64 (array.get $ai64 (local.get $l) (i32.const 3)) (i64.const -4))
    (if (f32.ne (array.get $af32 (local.get $f) (i32.const 3)) (f32.const 2.5))
      (then (call $fail)))
    (if (f64.ne (array.get $af64 (local.get $d) (i32.const 3)) (f64.const -3.75))
      (then (call $fail)))
    (call $eq64 (i64x2.extract_lane 1 (array.get $av (local.get $v) (i32.const 3)))
                (i64.const 0x0706050403020100)))

  ;; A default array is zero, and a zero fill must still store the length.
  (func (export "array_new_default")
    (local $i (ref $ai32)) (local $r (ref $aref))
    (local.set $i (array.new_default $ai32 (i32.const 5)))
    (local.set $r (array.new_default $aref (i32.const 5)))
    (call $eq32 (array.len (local.get $i)) (i32.const 5))
    (call $eq32 (array.get $ai32 (local.get $i) (i32.const 4)) (i32.const 0))
    (if (i32.eqz (ref.is_null (array.get $aref (local.get $r) (i32.const 4))))
      (then (call $fail)))
    ;; Zero elements is a legal array, and its length is zero rather than its
    ;; allocation being skipped.
    (call $eq32 (array.len (array.new_default $ai32 (i32.const 0))) (i32.const 0)))

  (func (export "array_new_fixed")
    (local $a (ref $afix))
    (local.set $a (array.new_fixed $afix 3 (i32.const 10) (i32.const 20) (i32.const 30)))
    (call $eq32 (array.len (local.get $a)) (i32.const 3))
    (call $eq32 (array.get $afix (local.get $a) (i32.const 0)) (i32.const 10))
    (call $eq32 (array.get $afix (local.get $a) (i32.const 1)) (i32.const 20))
    (call $eq32 (array.get $afix (local.get $a) (i32.const 2)) (i32.const 30))
    ;; And with no elements at all, which is the count as an immediate rather
    ;; than as an operand.
    (call $eq32 (array.len (array.new_fixed $afix 0)) (i32.const 0)))

  (func (export "array_set_get")
    (local $a (ref $ai32))
    (local.set $a (array.new_default $ai32 (i32.const 8)))
    (array.set $ai32 (local.get $a) (i32.const 0) (i32.const 100))
    (array.set $ai32 (local.get $a) (i32.const 7) (i32.const 700))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 0)) (i32.const 100))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 7)) (i32.const 700))
    ;; Neighbours untouched, which is what an index scaled by the wrong width
    ;; would break.
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 1)) (i32.const 0))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 6)) (i32.const 0)))

  ;; The stride, one width at a time: writing element k must not be visible in
  ;; element k+1.  A packed array is where an index scaled as if it were four
  ;; bytes shows up immediately.
  (func (export "array_strides")
    (local $b (ref $ai8)) (local $h (ref $ai16)) (local $v (ref $av))
    (local.set $b (array.new_default $ai8 (i32.const 8)))
    (local.set $h (array.new_default $ai16 (i32.const 8)))
    (local.set $v (array.new_default $av (i32.const 4)))
    (array.set $ai8 (local.get $b) (i32.const 3) (i32.const -1))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 3)) (i32.const 0xff))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 2)) (i32.const 0))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 4)) (i32.const 0))
    (array.set $ai16 (local.get $h) (i32.const 3) (i32.const -1))
    (call $eq32 (array.get_u $ai16 (local.get $h) (i32.const 3)) (i32.const 0xffff))
    (call $eq32 (array.get_u $ai16 (local.get $h) (i32.const 2)) (i32.const 0))
    (call $eq32 (array.get_u $ai16 (local.get $h) (i32.const 4)) (i32.const 0))
    (array.set $av (local.get $v) (i32.const 2) (v128.const i64x2 -1 -1))
    (call $eq64 (i64x2.extract_lane 1 (array.get $av (local.get $v) (i32.const 2)))
                (i64.const -1))
    (call $eq64 (i64x2.extract_lane 0 (array.get $av (local.get $v) (i32.const 1)))
                (i64.const 0))
    (call $eq64 (i64x2.extract_lane 0 (array.get $av (local.get $v) (i32.const 3)))
                (i64.const 0)))

  ;; ---- fill -----------------------------------------------------------

  (func (export "array_fill")
    (local $a (ref $ai32)) (local $v (ref $av))
    (local.set $a (array.new_default $ai32 (i32.const 8)))
    (array.fill $ai32 (local.get $a) (i32.const 2) (i32.const 9) (i32.const 3))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 1)) (i32.const 0))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 2)) (i32.const 9))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 4)) (i32.const 9))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 5)) (i32.const 0))
    ;; A zero-length fill at the very end is in range and does nothing; a
    ;; range check written as `idx < len` rather than `idx + n <= len` traps.
    (array.fill $ai32 (local.get $a) (i32.const 8) (i32.const 7) (i32.const 0))
    (array.fill $ai32 (local.get $a) (i32.const 0) (i32.const 7) (i32.const 8))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 7)) (i32.const 7))
    ;; And the wide value path.
    (local.set $v (array.new_default $av (i32.const 3)))
    (array.fill $av (local.get $v) (i32.const 1)
      (v128.const i64x2 0x1111111111111111 0x2222222222222222) (i32.const 2))
    (call $eq64 (i64x2.extract_lane 1 (array.get $av (local.get $v) (i32.const 2)))
                (i64.const 0x2222222222222222))
    (call $eq64 (i64x2.extract_lane 0 (array.get $av (local.get $v) (i32.const 0)))
                (i64.const 0)))

  ;; ---- copy -----------------------------------------------------------

  (func $mkseq (param $n i32) (result (ref $ai32))
    (local $a (ref $ai32)) (local $i i32)
    (local.set $a (array.new_default $ai32 (local.get $n)))
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
      (array.set $ai32 (local.get $a) (local.get $i) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $a))

  ;; Overlapping ranges within one array, both directions.  The spec says the
  ;; result is as if the source were read first, so a forward loop is wrong
  ;; when the destination is above the source and a backward one is wrong the
  ;; other way -- exactly the rule `memory.copy` already has.
  (func (export "array_copy_overlap")
    (local $a (ref $ai32)) (local $b (ref $ai32))
    (local.set $a (call $mkseq (i32.const 8)))
    ;; dst above src: 2..6 <- 0..4, so the answer is 0 1 0 1 2 3 4 7.
    (array.copy $ai32 $ai32 (local.get $a) (i32.const 2)
                            (local.get $a) (i32.const 0) (i32.const 5))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 2)) (i32.const 0))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 6)) (i32.const 4))
    (call $eq32 (array.get $ai32 (local.get $a) (i32.const 7)) (i32.const 7))
    ;; dst below src: 0..4 <- 3..7 of a fresh sequence.
    (local.set $b (call $mkseq (i32.const 8)))
    (array.copy $ai32 $ai32 (local.get $b) (i32.const 0)
                            (local.get $b) (i32.const 3) (i32.const 5))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 0)) (i32.const 3))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 4)) (i32.const 7))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 5)) (i32.const 5)))

  (func (export "array_copy_between")
    (local $a (ref $ai32)) (local $b (ref $ai32))
    (local.set $a (call $mkseq (i32.const 6)))
    (local.set $b (array.new $ai32 (i32.const -1) (i32.const 6)))
    (array.copy $ai32 $ai32 (local.get $b) (i32.const 1)
                            (local.get $a) (i32.const 2) (i32.const 3))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 0)) (i32.const -1))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 1)) (i32.const 2))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 3)) (i32.const 4))
    (call $eq32 (array.get $ai32 (local.get $b) (i32.const 4)) (i32.const -1))
    ;; A zero-length copy with both indices at the end is in range.
    (array.copy $ai32 $ai32 (local.get $b) (i32.const 6)
                            (local.get $a) (i32.const 6) (i32.const 0)))

  ;; ---- segments -------------------------------------------------------

  ;; A data segment is bytes, so the element width decides how many elements
  ;; a run of them makes and where each one starts.
  (func (export "array_new_data")
    (local $b (ref $ai8)) (local $i (ref $ai32))
    (local.set $b (array.new_data $ai8 $d (i32.const 0) (i32.const 4)))
    (call $eq32 (array.len (local.get $b)) (i32.const 4))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 0)) (i32.const 1))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 3)) (i32.const 4))
    ;; The same bytes read as i32s: little-endian, so 0x04030201.
    (local.set $i (array.new_data $ai32 $d (i32.const 0) (i32.const 2)))
    (call $eq32 (array.get $ai32 (local.get $i) (i32.const 0)) (i32.const 0x04030201))
    (call $eq32 (array.get $ai32 (local.get $i) (i32.const 1)) (i32.const 0x08070605))
    ;; An offset into the segment, and an empty read at its very end.
    (local.set $b (array.new_data $ai8 $d (i32.const 12) (i32.const 4)))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 0)) (i32.const 13))
    (call $eq32 (array.len (array.new_data $ai8 $d (i32.const 16) (i32.const 0)))
                (i32.const 0)))

  (func (export "array_init_data")
    (local $b (ref $ai8))
    (local.set $b (array.new_default $ai8 (i32.const 8)))
    (array.init_data $ai8 $d (local.get $b) (i32.const 2) (i32.const 4) (i32.const 3))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 1)) (i32.const 0))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 2)) (i32.const 5))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 4)) (i32.const 7))
    (call $eq32 (array.get_u $ai8 (local.get $b) (i32.const 5)) (i32.const 0))
    (array.init_data $ai8 $d (local.get $b) (i32.const 8) (i32.const 16) (i32.const 0)))

  (func (export "array_new_elem")
    (local $a (ref $ffn))
    (local.set $a (array.new_elem $ffn $e (i32.const 0) (i32.const 2)))
    (call $eq32 (array.len (local.get $a)) (i32.const 2))
    (if (ref.is_null (array.get $ffn (local.get $a) (i32.const 0))) (then (call $fail)))
    (if (ref.is_null (array.get $ffn (local.get $a) (i32.const 1))) (then (call $fail)))
    ;; Which item landed where.  `ref.eq` cannot say -- a funcref is not in
    ;; the `eq` hierarchy -- so the two items are functions with the same
    ;; signature and opposite meanings, and calling one is the test.  Item 0
    ;; returns when its arguments agree; item 1 returns when they differ.
    (call_ref $cmp (i32.const 5) (i32.const 5)
      (ref.as_non_null (array.get $ffn (local.get $a) (i32.const 0))))
    (call_ref $cmp (i32.const 5) (i32.const 6)
      (ref.as_non_null (array.get $ffn (local.get $a) (i32.const 1))))
    ;; And with an offset into the segment, where item 1 comes first.
    (call_ref $cmp (i32.const 5) (i32.const 6)
      (ref.as_non_null (array.get $ffn
        (array.new_elem $ffn $e (i32.const 1) (i32.const 1)) (i32.const 0)))))

  (func (export "array_init_elem")
    (local $a (ref $ffn))
    (local.set $a (array.new_default $ffn (i32.const 4)))
    (array.init_elem $ffn $e (local.get $a) (i32.const 1) (i32.const 1) (i32.const 1))
    (if (i32.eqz (ref.is_null (array.get $ffn (local.get $a) (i32.const 0))))
      (then (call $fail)))
    (if (ref.is_null (array.get $ffn (local.get $a) (i32.const 1))) (then (call $fail)))
    (if (i32.eqz (ref.is_null (array.get $ffn (local.get $a) (i32.const 2))))
      (then (call $fail)))
    ;; Index 1 got segment item 1, which is the one that wants its arguments
    ;; to differ.
    (call_ref $cmp (i32.const 5) (i32.const 6)
      (ref.as_non_null (array.get $ffn (local.get $a) (i32.const 1)))))

  ;; An element segment's expressions are evaluated *once*, at instantiation.
  ;; Invisible while an item could only be `ref.func` or `ref.null`, and
  ;; observable the moment one can allocate: two reads of one item have to be
  ;; the same object.
  ;; `eqref`, not `anyref`: `ref.eq` is what asks whether two references are
  ;; the same object, and it is defined on the `eq` hierarchy alone.  A struct
  ;; is under `eq`, so nothing is lost.
  (elem $ealloc eqref (item (struct.new_default $imm)))
  (type $aany (array (mut eqref)))

  (func (export "elem_evaluated_once")
    (local $a (ref $aany)) (local $b (ref $aany))
    (local.set $a (array.new_elem $aany $ealloc (i32.const 0) (i32.const 1)))
    (local.set $b (array.new_elem $aany $ealloc (i32.const 0) (i32.const 1)))
    (if (i32.eqz (ref.eq (array.get $aany (local.get $a) (i32.const 0))
                         (array.get $aany (local.get $b) (i32.const 0))))
      (then (call $fail))))

  ;; A large array, so the allocation is more than a page and the length is
  ;; well past anything a 16-bit field would hold.
  (func (export "array_large")
    (local $a (ref $ai64))
    (local.set $a (array.new $ai64 (i64.const 0x5555) (i32.const 100000)))
    (call $eq32 (array.len (local.get $a)) (i32.const 100000))
    (call $eq64 (array.get $ai64 (local.get $a) (i32.const 99999)) (i64.const 0x5555))
    (array.set $ai64 (local.get $a) (i32.const 99999) (i64.const 7))
    (call $eq64 (array.get $ai64 (local.get $a) (i32.const 99999)) (i64.const 7))
    (call $eq64 (array.get $ai64 (local.get $a) (i32.const 99998)) (i64.const 0x5555)))

  ;; ---- the traps every accessor owes ----------------------------------

  (func (export "arr_get_null") (result i32)
    (array.get $ai32 (ref.null $ai32) (i32.const 0)))
  (func (export "arr_set_null")
    (array.set $ai32 (ref.null $ai32) (i32.const 0) (i32.const 1)))
  (func (export "arr_len_null") (result i32)
    (array.len (ref.null array)))
  (func (export "arr_fill_null")
    (array.fill $ai32 (ref.null $ai32) (i32.const 0) (i32.const 0) (i32.const 0)))
  (func (export "arr_copy_null_dst")
    (array.copy $ai32 $ai32 (ref.null $ai32) (i32.const 0)
                            (array.new_default $ai32 (i32.const 1)) (i32.const 0)
                            (i32.const 0)))
  (func (export "arr_copy_null_src")
    (array.copy $ai32 $ai32 (array.new_default $ai32 (i32.const 1)) (i32.const 0)
                            (ref.null $ai32) (i32.const 0) (i32.const 0)))
  (func (export "arr_init_data_null")
    (array.init_data $ai8 $d (ref.null $ai8) (i32.const 0) (i32.const 0) (i32.const 0)))

  ;; One past the end, on each accessor.  The index is unsigned, so a check
  ;; written as a signed compare lets -1 through.
  ;; ---- what pass_array_bounds may and may not remove ------------------
  ;;
  ;; The pass drops an array's bounds check when the allocation's length is a
  ;; constant and `ir_value_max` says the index cannot reach it.  Both halves
  ;; are claims, and a wrong one is a write past the end of an object rather
  ;; than a trap -- so these come in pairs: one where the check must go and the
  ;; answer has to stay right, one where the same shape is one element too
  ;; small and the trap has to survive.
  ;;
  ;; The constant cases below (`arr_get_oob` and its neighbours) already pin
  ;; the literal off-by-one.  What they cannot reach is the *range* analysis:
  ;; `i & 63` never exceeds 63, which is in bounds for a 64-element array and
  ;; out of bounds for a 63-element one, and nothing else in this file asks a
  ;; question `ir_value_max` has to answer.

  (func (export "arr_mask_fits") (param $n i32) (result i32)
    (local $a (ref $ai32)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $ai32 (i32.const 64)))
    (loop $l
      (array.set $ai32 (local.get $a)
        (i32.and (local.get $i) (i32.const 63)) (local.get $i))
      (local.set $s (i32.add (local.get $s)
        (array.get $ai32 (local.get $a)
          (i32.and (local.get $i) (i32.const 63)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; The same mask against an array one element shorter: 63 is now the length,
  ;; so the check has to stay and the sixty-fourth turn has to trap.
  (func (export "arr_mask_overruns") (param $n i32) (result i32)
    (local $a (ref $ai32)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $ai32 (i32.const 63)))
    (loop $l
      (array.set $ai32 (local.get $a)
        (i32.and (local.get $i) (i32.const 63)) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; `array.new_data` carries its element count as the *second* operand and a
  ;; byte offset as the first, and reading the wrong one is a length too large:
  ;; a check removed where it was needed.  The three numbers are chosen so the
  ;; two readings disagree about *this* index -- two elements from byte twelve,
  ;; indexed by a mask that reaches three.  Read right, the length is two, the
  ;; check stays and the third turn traps; read as the offset, the length looks
  ;; like twelve, the mask fits inside it and nothing traps at all.  With the
  ;; offset and the count equal, as the first version of this case had them,
  ;; the mistake is invisible.
  (func (export "arr_seg_len") (param $n i32) (result i32)
    (local $a (ref $ai8)) (local $i i32) (local $s i32)
    (local.set $a (array.new_data $ai8 $d (i32.const 12) (i32.const 2)))
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (array.get_u $ai8 (local.get $a)
          (i32.and (local.get $i) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; A length the pass cannot see, so nothing may be removed however tame the
  ;; index looks: the array reaching the loop is one of two allocations and the
  ;; merge is what hides it.
  ;;
  ;; The *big* one is the fall-through and the small one is the branch taken,
  ;; which is not decoration.  A version of this that looked at any single
  ;; predecessor would read the first, and with the arms the other way round it
  ;; reads the eight and is accidentally right -- the case passes and the rule
  ;; it is meant to hold is untested.  This way round, a length guessed from one
  ;; predecessor is sixty-four, the mask fits inside it, and the trap is lost.
  (func (export "arr_len_unknown") (param $n i32) (result i32)
    (local $a (ref $ai32)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $ai32 (i32.const 64)))
    (if (i32.le_u (local.get $n) (i32.const 100))
      (then (local.set $a (array.new_default $ai32 (i32.const 8)))))
    (loop $l
      (array.set $ai32 (local.get $a)
        (i32.and (local.get $i) (i32.const 63)) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  (func (export "arr_get_oob") (result i32)
    (array.get $ai32 (array.new_default $ai32 (i32.const 3)) (i32.const 3)))
  (func (export "arr_get_oob_neg") (result i32)
    (array.get $ai32 (array.new_default $ai32 (i32.const 3)) (i32.const -1)))
  (func (export "arr_get_oob_empty") (result i32)
    (array.get $ai32 (array.new_default $ai32 (i32.const 0)) (i32.const 0)))
  (func (export "arr_set_oob")
    (array.set $ai32 (array.new_default $ai32 (i32.const 3)) (i32.const 3) (i32.const 1)))
  (func (export "arr_fill_oob")
    (array.fill $ai32 (array.new_default $ai32 (i32.const 3))
                (i32.const 1) (i32.const 0) (i32.const 3)))
  (func (export "arr_fill_oob_wrap")
    ;; idx + n overflows 32 bits; a range check that adds them in 32 bits and
    ;; compares against the length says this one is fine.
    (array.fill $ai32 (array.new_default $ai32 (i32.const 3))
                (i32.const -1) (i32.const 0) (i32.const 2)))
  (func (export "arr_copy_oob_dst")
    (array.copy $ai32 $ai32 (array.new_default $ai32 (i32.const 3)) (i32.const 1)
                            (array.new_default $ai32 (i32.const 8)) (i32.const 0)
                            (i32.const 3)))
  (func (export "arr_copy_oob_src")
    (array.copy $ai32 $ai32 (array.new_default $ai32 (i32.const 8)) (i32.const 0)
                            (array.new_default $ai32 (i32.const 3)) (i32.const 1)
                            (i32.const 3)))
  (func (export "arr_init_data_oob_seg")
    (array.init_data $ai8 $d (array.new_default $ai8 (i32.const 8))
                     (i32.const 0) (i32.const 14) (i32.const 4)))
  (func (export "arr_init_data_oob_arr")
    (array.init_data $ai8 $d (array.new_default $ai8 (i32.const 2))
                     (i32.const 0) (i32.const 0) (i32.const 4)))
  (func (export "arr_init_elem_oob_seg")
    (array.init_elem $ffn $e (array.new_default $ffn (i32.const 8))
                     (i32.const 0) (i32.const 1) (i32.const 3)))
  (func (export "arr_new_data_oob")
    (drop (array.new_data $ai32 $d (i32.const 0) (i32.const 8))))

  ;; Constant arrays, evaluated by the runtime rather than compiled.
  (global $ga (ref $ai32) (array.new $ai32 (i32.const 42) (i32.const 3)))
  (global $gad (ref $ai64) (array.new_default $ai64 (i32.const 2)))
  (global $gaf (ref $afix) (array.new_fixed $afix 2 (i32.const 8) (i32.const 9)))

  (func (export "const_array")
    (call $eq32 (array.len (global.get $ga)) (i32.const 3))
    (call $eq32 (array.get $ai32 (global.get $ga) (i32.const 2)) (i32.const 42))
    (call $eq32 (array.len (global.get $gad)) (i32.const 2))
    (call $eq64 (array.get $ai64 (global.get $gad) (i32.const 1)) (i64.const 0))
    (call $eq32 (array.len (global.get $gaf)) (i32.const 2))
    (call $eq32 (array.get $afix (global.get $gaf) (i32.const 0)) (i32.const 8))
    (call $eq32 (array.get $afix (global.get $gaf) (i32.const 1)) (i32.const 9)))

  ;; ---- casts -----------------------------------------------------------

  (type $sub_pt (sub $pt (struct (field $x (mut i32)) (field $y (mut i32))
                                 (field $w (mut i64)))))

  ;; Three of the four shapes a reference can be -- null, a tagged i31 and a
  ;; heap object -- against the abstract targets.
  ;;
  ;; The fourth, a *host* value, cannot be reached from here: it has to come
  ;; from outside and these modules are linked without a host.  It is covered
  ;; upstream instead, by `ref_test.wast`, which internalises one and requires
  ;; it to answer `any` and nothing below it -- the assertion that found the
  ;; harness handing out host reference 0 as the value 1, which is the i31
  ;; tag.  See GC_HOST_TAG in rt/warm_rt.h.
  (func $test_all (param $r anyref) (result i32)
    (i32.or
      (i32.or (i32.shl (ref.test anyref (local.get $r)) (i32.const 0))
              (i32.shl (ref.test eqref (local.get $r)) (i32.const 1)))
      (i32.or (i32.shl (ref.test i31ref (local.get $r)) (i32.const 2))
              (i32.or (i32.shl (ref.test structref (local.get $r)) (i32.const 3))
                      (i32.shl (ref.test arrayref (local.get $r)) (i32.const 4))))))

  (func (export "cast_shapes")
    ;; null: nullable targets all say yes, and that is the only thing null is.
    (call $eq32 (call $test_all (ref.null any)) (i32.const 0x1f))
    ;; an i31: any, eq, i31.
    (call $eq32 (call $test_all (ref.i31 (i32.const 3))) (i32.const 0x07))
    ;; a struct: any, eq, struct.
    (call $eq32 (call $test_all (struct.new_default $imm)) (i32.const 0x0b))
    ;; an array: any, eq, array.
    (call $eq32 (call $test_all (array.new_default $ai32 (i32.const 1)))
                (i32.const 0x13)))

  ;; The non-null forms differ from the nullable ones on exactly one value.
  (func (export "cast_nullability")
    (call $eq32 (ref.test (ref any) (ref.null any)) (i32.const 0))
    (call $eq32 (ref.test anyref (ref.null any)) (i32.const 1))
    (call $eq32 (ref.test (ref i31) (ref.i31 (i32.const 1))) (i32.const 1))
    (call $eq32 (ref.test (ref null none) (ref.null any)) (i32.const 1))
    (call $eq32 (ref.test (ref null none) (ref.i31 (i32.const 1))) (i32.const 0))
    (call $eq32 (ref.test (ref none) (ref.null any)) (i32.const 0)))

  ;; A named type reads its supertype display, so a subtype answers yes to its
  ;; supertype and a supertype answers no to its subtype.
  (func (export "cast_named")
    (local $s (ref $sub_pt))
    (local.set $s (struct.new $sub_pt (i32.const 1) (i32.const 2) (i64.const 3)))
    (call $eq32 (ref.test (ref $sub_pt) (local.get $s)) (i32.const 1))
    (call $eq32 (ref.test (ref $pt) (local.get $s)) (i32.const 1))
    (call $eq32 (ref.test (ref $pt3) (local.get $s)) (i32.const 0))
    (call $eq32 (ref.test (ref $imm) (local.get $s)) (i32.const 0))
    ;; ... and the other way: a $pt3 is not a $sub_pt, though both extend $pt.
    (call $eq32 (ref.test (ref $sub_pt)
      (struct.new $pt3 (i32.const 1) (i32.const 2) (i32.const 3))) (i32.const 0))
    ;; An i31 is in the same hierarchy and is not a struct of any name.
    (call $eq32 (ref.test (ref $pt) (ref.i31 (i32.const 1))) (i32.const 0))
    ;; A cast that succeeds hands the value back, narrowed enough to read.
    (call $eq64 (struct.get $sub_pt $w (ref.cast (ref $sub_pt) (local.get $s)))
                (i64.const 3)))

  ;; A funcref carries the same display in a different place, so the func
  ;; hierarchy has to be told apart at compile time and not at run time.
  (func (export "cast_func")
    (local $f funcref)
    (local.set $f (ref.func $eq32c))
    (call $eq32 (ref.test (ref func) (local.get $f)) (i32.const 1))
    (call $eq32 (ref.test (ref $cmp) (local.get $f)) (i32.const 1))
    (call $eq32 (ref.test (ref $ffn2) (local.get $f)) (i32.const 0))
    (call $eq32 (ref.test (ref null nofunc) (local.get $f)) (i32.const 0))
    (call $eq32 (ref.test (ref null nofunc) (ref.null func)) (i32.const 1))
    ;; And it survives a cast and a call.
    (call_ref $cmp (i32.const 4) (i32.const 4)
      (ref.cast (ref $cmp) (local.get $f))))
  (type $ffn2 (func (param i64)))
  (func $unused_ffn2 (type $ffn2))
  (elem declare func $unused_ffn2)

  ;; The two conversions are the identity, and the round trip is too -- but an
  ;; *internalised host value* is in `any` and below nothing in it, which is
  ;; the whole reason a host reference carries a tag.
  (func (export "cast_extern")
    (local $s (ref $imm))
    (local.set $s (struct.new_default $imm))
    ;; A struct externalised and internalised again is the same object.
    (if (i32.eqz (ref.eq (local.get $s)
                    (ref.cast (ref $imm)
                      (any.convert_extern (extern.convert_any (local.get $s))))))
      (then (call $fail)))
    ;; null survives both directions.
    (if (i32.eqz (ref.is_null (any.convert_extern (ref.null extern))))
      (then (call $fail)))
    (if (i32.eqz (ref.is_null (extern.convert_any (ref.null any))))
      (then (call $fail))))

  ;; ref.cast traps, and traps on exactly the values ref.test rejects.
  (func (export "cast_fail_i31") (result i32)
    (i31.get_s (ref.cast (ref i31) (struct.new_default $imm))))
  (func (export "cast_fail_struct")
    (drop (ref.cast (ref $imm) (ref.i31 (i32.const 1)))))
  (func (export "cast_fail_named")
    (drop (ref.cast (ref $sub_pt) (struct.new $pt (i32.const 1) (i32.const 2)))))
  (func (export "cast_fail_null")
    (drop (ref.cast (ref any) (ref.null any))))
  (func (export "cast_null_ok")
    (drop (ref.cast (ref null any) (ref.null any))))

  ;; ---- br_on_cast ------------------------------------------------------

  ;; Which way each value goes, and that the *fall-through* keeps it too.  A
  ;; branch that took every value, or none, passes a test that only looks at
  ;; one of the two paths -- so both are checked for each shape.
  (func $which (param $r anyref) (result i32)
    (block $isi31 (result (ref i31))
      (drop (br_on_cast $isi31 anyref (ref i31) (local.get $r)))
      (return (i32.const 2)))
    (drop)
    (i32.const 1))

  (func (export "br_on_cast_paths")
    (call $eq32 (call $which (ref.i31 (i32.const 7))) (i32.const 1))
    (call $eq32 (call $which (struct.new_default $imm)) (i32.const 2))
    (call $eq32 (call $which (ref.null any)) (i32.const 2)))

  (func $which_fail (param $r anyref) (result i32)
    (block $notimm (result anyref)
      (drop (br_on_cast_fail $notimm anyref (ref $imm) (local.get $r)))
      (return (i32.const 1)))
    (drop)
    (i32.const 2))

  (func (export "br_on_cast_fail_paths")
    (call $eq32 (call $which_fail (struct.new_default $imm)) (i32.const 1))
    (call $eq32 (call $which_fail (ref.i31 (i32.const 7))) (i32.const 2))
    (call $eq32 (call $which_fail (ref.null any)) (i32.const 2)))

  ;; The value the branch carries is the one that was tested, not a rebuilt
  ;; one: reading a field through the narrowed reference on the taken edge is
  ;; what says so.
  (func $carry (param $r anyref) (result i64)
    (block $ok (result (ref $imm))
      (drop (br_on_cast $ok anyref (ref $imm) (local.get $r)))
      (return (i64.const -1)))
    (struct.get $imm $s))

  (func (export "br_on_cast_carries")
    (call $eq64 (call $carry (struct.new $imm (i32.const 0) (i64.const 77)))
                (i64.const 77))
    (call $eq64 (call $carry (ref.i31 (i32.const 1))) (i64.const -1)))

  ;; A null takes exactly one of the two paths, and which one is the whole
  ;; difference between `(ref null $t)` and `(ref $t)` as the cast target.
  (func $null_goes (param $r anyref) (result i32)
    (block $ok (result (ref null $imm))
      (drop (br_on_cast $ok anyref (ref null $imm) (local.get $r)))
      (return (i32.const 2)))
    (drop)
    (i32.const 1))

  (func (export "br_on_cast_null_edge")
    (call $eq32 (call $null_goes (ref.null any)) (i32.const 1))
    (call $eq32 (call $null_goes (ref.i31 (i32.const 1))) (i32.const 2))
    (call $eq32 (call $which (ref.null any)) (i32.const 2)))

  ;; What `rt1 \ rt2` leaves is a *type*, and the only thing that reads it is
  ;; whether a later access emits a null check -- so making it observable
  ;; needs an access on the path a null actually takes.  A null satisfies
  ;; `(ref null $imm)`, so it falls *through* `br_on_cast_fail`, and the field
  ;; read there has to trap rather than fault.  Get the nullability wrong in
  ;; the unsafe direction and this dereferences address zero.
  (func (export "br_on_cast_fail_null_deref") (result i64)
    (local $r anyref)
    (local.set $r (ref.null any))
    (block $notimm (result anyref)
      (return (struct.get $imm $s
        (br_on_cast_fail $notimm anyref (ref null $imm) (local.get $r)))))
    (drop)
    (i64.const 0))

  ;; And the mirror: with a *non-null* target a null falls through
  ;; `br_on_cast`, keeping rt1's null, so the same read traps there too.
  (func (export "br_on_cast_null_deref") (result i64)
    (local $r anyref)
    (local.set $r (ref.null any))
    (block $ok (result (ref $imm))
      ;; The *fall-through value*, not the local it came from -- reading the
      ;; local instead asks about a type nothing here computed, and the
      ;; ablation that drops rt1's null from the fall-through then passes.
      (return (struct.get $imm $s
        (ref.cast (ref null $imm)
          (br_on_cast $ok anyref (ref $imm) (local.get $r))))))
    (struct.get $imm $s))

  ;; Constant casts: the conversions are constant instructions, so a global
  ;; can be initialised through one, and the runtime's evaluator is where that
  ;; happens.
  (global $gx externref (extern.convert_any (ref.i31 (i32.const 21))))

  (func (export "const_convert")
    (call $eq32 (i31.get_s (ref.cast (ref i31) (any.convert_extern (global.get $gx))))
                (i32.const 21)))

  (func (export "const_widths")
    (call $eq32 (struct.get_s $cw $p (global.get $gw)) (i32.const 0x7e))
    (call $eq32 (struct.get_u $cw $q (global.get $gw)) (i32.const 0x7ffe))
    (call $eq32 (struct.get $cw $r (global.get $gw)) (i32.const 0x11223344))
    (call $eq64 (struct.get $cw $t (global.get $gw)) (i64.const 0x5566778899aabbcc))
    (call $eq64 (i64x2.extract_lane 0 (struct.get $cw $u (global.get $gw)))
                (i64.const 0xdeadbeefcafef00d))
    (call $eq64 (i64x2.extract_lane 1 (struct.get $cw $u (global.get $gw)))
                (i64.const 0x0102030405060708)))

  ;; ---- loop-invariant reads behind a guard ------------------------------
  ;;
  ;; A read whose null or bounds check a dominating test answered carries no
  ;; check any more, and LICM took that as leave to lift it out of the loop --
  ;; above the test, when the test was inside the loop too.  Each loop here
  ;; reads an object only where a test says it may, and the object is null,
  ;; so the read never happens; lifted, it loaded from null and faulted:
  ;; Kotlin's joinToString reading a vtable.  Called with a trip count.
  (type $lg_box (struct (field i32)))
  (type $lg_vt (struct (field i32)))
  (type $lg_obj (struct (field (ref $lg_vt))))
  (type $lg_arr (array i32))
  (global $lg_gbox (mut (ref null $lg_box)) (ref.null $lg_box))
  (global $lg_gany (mut anyref) (ref.null any))
  (global $lg_gobj (mut (ref null $lg_obj)) (ref.null $lg_obj))
  (global $lg_garr (mut (ref null $lg_arr)) (ref.null $lg_arr))

  (func (export "licm_guard_null") (param $n i32)
    (local $x (ref null $lg_box)) (local $i i32) (local $s i32)
    (local.set $x (global.get $lg_gbox))
    (loop $l
      (if (i32.eqz (ref.is_null (local.get $x)))
        (then (local.set $s (i32.add (local.get $s)
                                     (struct.get $lg_box 0 (local.get $x))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $eq32 (local.get $s) (i32.const 0)))

  ;; through the cast the test already answered: its convert is pure and
  ;; lifts on its own, typed non-null
  (func (export "licm_guard_test") (param $n i32)
    (local $x anyref) (local $i i32) (local $s i32)
    (local.set $x (global.get $lg_gany))
    (loop $l
      (if (ref.test (ref $lg_box) (local.get $x))
        (then (local.set $s (i32.add (local.get $s)
                (struct.get $lg_box 0 (ref.cast (ref $lg_box) (local.get $x)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $eq32 (local.get $s) (i32.const 0)))

  ;; obj.vtable.slot: the outer read is of a non-null field, but its object
  ;; is not
  (func (export "licm_guard_vtable") (param $n i32)
    (local $x (ref null $lg_obj)) (local $i i32) (local $s i32)
    (local.set $x (global.get $lg_gobj))
    (loop $l
      (if (i32.eqz (ref.is_null (local.get $x)))
        (then (local.set $s (i32.add (local.get $s)
                (struct.get $lg_vt 0 (struct.get $lg_obj 0 (local.get $x)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $eq32 (local.get $s) (i32.const 0)))

  ;; array.len and an element behind a null test and a bounds test
  (func (export "licm_guard_array") (param $n i32)
    (local $x (ref null $lg_arr)) (local $i i32) (local $s i32)
    (local.set $x (global.get $lg_garr))
    (loop $l
      (block $skip
        (br_if $skip (ref.is_null (local.get $x)))
        (br_if $skip (i32.ge_u (local.get $i) (array.len (local.get $x))))
        (local.set $s (i32.add (local.get $s)
                               (array.get $lg_arr (local.get $x) (i32.const 0)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $eq32 (local.get $s) (i32.const 0)))

  ;; ---- escape analysis --------------------------------------------------
  ;;
  ;; The pass itself, from both sides.  The ea_* objects below stay inside
  ;; their functions on purpose: at -O1 and above none of them is really
  ;; allocated, and the answers have to come out the same as at -O0, where
  ;; every one of them is.  `ea_elided` is also the harness's proof that the
  ;; pass fires at all -- run_gc.sh runs it under WARMC_GC_STATS and insists
  ;; the collector never woke.  The escaping twin is the other side: an
  ;; object that does leave -- through a global here -- must keep existing.

  (type $ebox (struct (field $ea (mut i32)) (field $eb (mut i64))))
  (type $ebb  (struct (field $ein (mut (ref null $ebox))) (field $ek (mut i32))))
  (type $epk  (struct (field $ec (mut i8)) (field $ed (mut i32))))

  (func (export "ea_fwd")
    (local $o (ref $ebox))
    (local.set $o (struct.new $ebox (i32.const 5) (i64.const 700)))
    (call $eq32 (i32.add (struct.get $ebox $ea (local.get $o))
                         (i32.wrap_i64 (struct.get $ebox $eb (local.get $o))))
                (i32.const 705)))

  (func (export "ea_chain")
    (local $w (ref $ebb))
    (local.set $w (struct.new $ebb
      (struct.new $ebox (i32.const 11) (i64.const 0))
      (i32.const 31)))
    (call $eq32
      (i32.add (struct.get $ebox $ea
                 (ref.as_non_null (struct.get $ebb $ein (local.get $w))))
               (struct.get $ebb $ek (local.get $w)))
      (i32.const 42)))

  (func (export "ea_default")
    (local $o (ref $ebox)) (local $q (ref $ebb))
    (local.set $o (struct.new_default $ebox))
    (local.set $q (struct.new_default $ebb))
    (call $eq32 (struct.get $ebox $ea (local.get $o)) (i32.const 0))
    (call $eq32 (i32.wrap_i64 (struct.get $ebox $eb (local.get $o))) (i32.const 0))
    (call $eq32 (struct.get $ebb $ek (local.get $q)) (i32.const 0))
    (if (i32.eqz (ref.is_null (struct.get $ebb $ein (local.get $q))))
      (then (call $fail))))

  ;; Packed fields hold the truncated value; the initialiser held the whole
  ;; one.  Forwarding would skip the round trip, so the pass keeps the
  ;; object -- and the answer says the round trip really ran.
  (func (export "ea_packed")
    (local $o (ref $epk))
    (local.set $o (struct.new $epk (i32.const 0x1ff) (i32.const 3)))
    (call $eq32 (i32.add (struct.get_u $epk $ec (local.get $o))
                         (struct.get $epk $ed (local.get $o)))
                (i32.const 258)))

  ;; Mutated and read: kept, and the mutation visible.
  (func (export "ea_mut")
    (local $o (ref $ebox))
    (local.set $o (struct.new $ebox (i32.const 1) (i64.const 2)))
    (struct.set $ebox $ea (local.get $o) (i32.const 40))
    (call $eq32 (struct.get $ebox $ea (local.get $o)) (i32.const 40)))

  ;; Escaped through the sink and read back out of it: the object must be as
  ;; real as any other.
  (func (export "ea_escaped")
    (local $o (ref $ebox))
    (local.set $o (struct.new $ebox (i32.const 21) (i64.const 9)))
    (global.set $sink (local.get $o))
    (call $eq32 (struct.get $ebox $ea
                  (ref.cast (ref $ebox) (global.get $sink)))
                (i32.const 21))
    (global.set $sink (ref.null any)))

  ;; A loop of boxes that are never allocated at all: the collector must not
  ;; wake once, which the harness checks by the stats line.
  (func (export "ea_elided") (param $n i32)
    (local $i i32) (local $s i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $s (i32.add (local.get $s)
          (struct.get $ebox $ea
            (struct.new $ebox (local.get $i) (i64.const 0)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (local.get $s)
      (i32.wrap_i64 (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $n))
                 (i64.extend_i32_u (i32.sub (local.get $n) (i32.const 1))))
        (i64.const 2)))))

  ;; ---- flow-sensitive null checks --------------------------------------
  ;; pass_gc_nullchk clears a GCF_NULLCHK when a dominating check on the
  ;; same object already proved it non-null.  These hold the sound side:
  ;; the first check must stay a trap, a merge must not inherit a
  ;; one-armed check, and the edge fact from br_on_null must only reach
  ;; its own arm.  The selector param keeps the compiler from knowing.

  ;; two reads through the same nullable local: the second check is the
  ;; redundant one; with 999 the first must still trap.
  (func (export "nc_second_read") (param $s i32)
    (local $o (ref null $imm))
    (local.set $o (struct.new $imm (i32.const 5) (i64.const 9)))
    (if (i32.eq (local.get $s) (i32.const 999))
      (then (local.set $o (ref.null $imm))))
    (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 5))
    (call $eq32 (i32.wrap_i64 (struct.get $imm $s (local.get $o)))
          (i32.const 9)))

  ;; a check inside one arm proves nothing at the merge.
  (func (export "nc_diamond") (param $s i32)
    (local $o (ref null $imm))
    (if (i32.eqz (local.get $s))
      (then (local.set $o (struct.new $imm (i32.const 3) (i64.const 0)))
            (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 3))))
    (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 3)))

  ;; ref.as_non_null carries the proof; the read behind it sheds its check
  ;; but a null still traps at the assertion itself.
  (func (export "nc_as_non_null") (param $s i32)
    (local $o (ref null $imm))
    (if (i32.eqz (local.get $s))
      (then (local.set $o (struct.new $imm (i32.const 4) (i64.const 0)))))
    (drop (ref.as_non_null (local.get $o)))
    (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 4)))

  ;; the non-null arm of br_on_null reads check-free; the null arm must
  ;; still take the branch.
  (func (export "nc_br_on_null") (param $s i32)
    (local $o (ref null $imm))
    (if (i32.eqz (local.get $s))
      (then (local.set $o (struct.new $imm (i32.const 6) (i64.const 0)))))
    (block $null
      (drop (br_on_null $null (local.get $o)))
      (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 6))))

  ;; a cast is a retype, not a new object: the read through the cast rides
  ;; the original's check, and a null must trap at that first read.
  (func (export "nc_cast_alias") (param $s i32)
    (local $o (ref null $imm)) (local $c (ref null $imm))
    (if (i32.eqz (local.get $s))
      (then (local.set $o (struct.new $imm (i32.const 8) (i64.const 1)))))
    (call $eq32 (struct.get $imm $k (local.get $o)) (i32.const 8))
    (local.set $c (ref.cast (ref null $imm) (local.get $o)))
    (call $eq32 (struct.get $imm $k (local.get $c)) (i32.const 8)))

  ;; ---- field store-to-load forwarding -----------------------------------
  ;; the value a set just wrote is the value the get returns, so the get
  ;; and its checks disappear.  These hold the boundaries: a call between
  ;; them reloads (the callee mutates through an alias), a possibly-null
  ;; set still traps, and a cast is the same object.
  (global $gbox (mut (ref null $box)) (ref.null $box))
  (func $poke (param $v i32)
    (struct.set $box $n (ref.as_non_null (global.get $gbox)) (local.get $v)))

  (func (export "fwd_basic") (param $x i32)
    (local $b (ref null $box))
    (local.set $b (struct.new $box (ref.null $box) (local.get $x)))
    (call $eq32 (struct.get $box $n (local.get $b)) (local.get $x))
    (struct.set $box $n (local.get $b) (i32.add (local.get $x) (i32.const 5)))
    (call $eq32 (struct.get $box $n (local.get $b))
          (i32.add (local.get $x) (i32.const 5))))

  (func (export "fwd_call_reload") (param $x i32)
    (local $b (ref null $box))
    (local.set $b (struct.new $box (ref.null $box) (local.get $x)))
    (global.set $gbox (local.get $b))
    (struct.set $box $n (local.get $b) (i32.const 1))
    (call $poke (i32.const 42))
    (call $eq32 (struct.get $box $n (local.get $b)) (i32.const 42)))

  (func (export "fwd_cast") (param $x i32)
    (local $b (ref null $box)) (local $c (ref null $box))
    (local.set $b (struct.new $box (ref.null $box) (i32.const 0)))
    (local.set $c (ref.cast (ref null $box) (local.get $b)))
    (struct.set $box $n (local.get $c) (local.get $x))
    (call $eq32 (struct.get $box $n (local.get $b)) (local.get $x)))

  (func (export "fwd_null") (param $s i32)
    (local $b (ref null $box))
    (if (i32.eqz (local.get $s))
      (then (local.set $b (struct.new $box (ref.null $box) (i32.const 0)))))
    (struct.set $box $n (local.get $b) (i32.const 9))
    (call $eq32 (struct.get $box $n (local.get $b)) (i32.const 9)))

  ;; two objects, one offset: the second set clobbers conservatively and
  ;; the first get reloads -- the values must hold either way.
  (func (export "fwd_two") (param $x i32)
    (local $a (ref null $box)) (local $b (ref null $box))
    (local.set $a (struct.new $box (ref.null $box) (i32.const 0)))
    (local.set $b (struct.new $box (ref.null $box) (i32.const 0)))
    (struct.set $box $n (local.get $a) (local.get $x))
    (struct.set $box $n (local.get $b) (i32.add (local.get $x) (i32.const 1)))
    (call $eq32 (struct.get $box $n (local.get $a)) (local.get $x))
    (call $eq32 (struct.get $box $n (local.get $b))
          (i32.add (local.get $x) (i32.const 1))))

  ;; ---- bounds a dominating test already proved -------------------------
  ;; the in-bounds arm of i <u len(a) reads unchecked; the same access with
  ;; no test, or with the wrong index under the test, must still trap.

  (func (export "bce_guarded") (param $i i32)
    (local $a (ref null $ai32))
    (local.set $a (array.new $ai32 (i32.const 7) (i32.const 5)))
    (if (i32.lt_u (local.get $i) (array.len (local.get $a)))
      (then (call $eq32 (array.get $ai32 (local.get $a) (local.get $i))
                  (i32.const 7)))))

  (func (export "bce_unguarded") (param $i i32)
    (local $a (ref null $ai32))
    (local.set $a (array.new $ai32 (i32.const 7) (i32.const 5)))
    (call $eq32 (array.get $ai32 (local.get $a) (local.get $i))
          (i32.const 7)))

  ;; the fact names one index value: i <u len says nothing about a[i+1].
  (func (export "bce_wrong_idx") (param $i i32)
    (local $a (ref null $ai32))
    (local.set $a (array.new $ai32 (i32.const 7) (i32.const 5)))
    (if (i32.lt_u (local.get $i) (array.len (local.get $a)))
      (then (call $eq32 (array.get $ai32 (local.get $a)
                          (i32.add (local.get $i) (i32.const 1)))
                  (i32.const 7)))))

  ;; a top-tested loop over exactly the length: every element access rides
  ;; the loop's own test.  n from the command line keeps it symbolic.
  (func (export "bce_loop") (param $n i32)
    (local $a (ref null $ai32)) (local $i i32) (local $acc i32)
    (local.set $a (array.new $ai32 (i32.const 4) (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (array.len (local.get $a))))
        (local.set $acc (i32.add (local.get $acc)
                                 (array.get $ai32 (local.get $a)
                                            (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $eq32 (local.get $acc) (i32.mul (local.get $n) (i32.const 4))))

  ;; the counted form: i walks 0,1,2,... toward n, which also sized the
  ;; allocation, so i != n on the continue side means i <u n.
  (func (export "bce_counted") (param $n i32)
    (local $a (ref null $ai32)) (local $i i32) (local $acc i32)
    (local.set $a (array.new $ai32 (i32.const 3) (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.eq (local.get $i) (local.get $n)))
        (local.set $acc (i32.add (local.get $acc)
                                 (array.get $ai32 (local.get $a)
                                            (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $eq32 (local.get $acc) (i32.mul (local.get $n) (i32.const 3))))

  ;; a step of two can hop over the bound: the check must stay, and with an
  ;; odd n it fires.
  (func (export "bce_step2") (param $n i32)
    (local $a (ref null $ai32)) (local $i i32) (local $acc i32)
    (local.set $a (array.new $ai32 (i32.const 3) (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.eq (local.get $i) (local.get $n)))
        (local.set $acc (i32.add (local.get $acc)
                                 (array.get $ai32 (local.get $a)
                                            (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 2)))
        (br $l)))
    (drop (local.get $acc)))

  ;; array.len re-read at every test hoists to the preheader; a null array
  ;; must trap exactly as before, since the read was always reached.
  (func (export "len_licm") (param $s i32)
    (local $a (ref null $ai32)) (local $i i32) (local $acc i32)
    (if (i32.eqz (local.get $s))
      (then (local.set $a (array.new $ai32 (i32.const 3) (i32.const 6)))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (array.len (local.get $a))))
        (local.set $acc (i32.add (local.get $acc)
                                 (array.get $ai32 (local.get $a)
                                            (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $eq32 (local.get $acc) (i32.const 18)))

  ;; the allocation itself is the proof: every read in the loop sheds its
  ;; check because the object's definition dominates them all.  n = 4.
  (func (export "nc_loop") (param $n i32)
    (local $o (ref null $imm)) (local $i i32) (local $acc i32)
    (local.set $o (struct.new $imm (i32.const 5) (i64.const 0)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $acc (i32.add (local.get $acc)
                                 (struct.get $imm $k (local.get $o))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (local.get $acc) (i32.const 20)))
)
