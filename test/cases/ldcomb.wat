;; Combining adjacent narrow loads: the shapes pass_load_combine rewrites, and
;; the near misses it has to leave alone.  Every export takes no arguments so
;; wasm-interp --run-all-exports decides the answers; what this file has to get
;; right is that the *near misses* are spelled so that a rule one condition
;; looser would rewrite them and get a different number.
;;
;; The base and the index come through mutable globals rather than constants,
;; because that is the shape a producer emits and because a constant address
;; would answer the question about constant folding instead.
(module
  (memory 1)
  (memory 1)
  (data (memory 0) (i32.const 0) "\01\23\45\67\89\ab\cd\ef\fe\dc\ba\98\76\54\32\10")
  (data (memory 1) (i32.const 0) "\a0\a1\a2\a3\a4\a5\a6\a7\a8\a9\aa\ab\ac\ad\ae\af")

  (global $base (mut i32) (i32.const 0))
  (global $idx  (mut i32) (i32.const 1))
  (global $z    (mut i32) (i32.const 3))
  (global $st   (mut i32) (i32.const 0))

  (func $side (result i32) (i32.const 0))

  ;; ---- the four-byte word, both ways round -------------------------------
  (func (export "be32") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))

  (func (export "le32") (result i32)
    (i32.or (i32.or (i32.or
              (i32.load8_u (i32.add (global.get $base) (i32.const 0)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 8)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 3))) (i32.const 24))))

  ;; The lanes in source order but associated to the right, which is the other
  ;; way a producer can write the same sum.
  (func (export "be32_rassoc") (result i32)
    (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.or
        (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16))
        (i32.or
          (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8))
                   (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))))

  ;; The displacement on the access rather than in the address, and mixed with
  ;; one that is in the address: both reach the same byte.  The mixture is
  ;; allowed only in this direction -- every lane's 32-bit part at or above the
  ;; lowest lane's -- and the near miss for the other direction is
  ;; `ldcomb_mixed` in `test/cases_traps`, because what it gets wrong is a
  ;; trap rather than a number and this file cannot see one.
  (func (export "be32_offset") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u offset=0 (global.get $base)) (i32.const 24))
      (i32.shl (i32.load8_u offset=1 (global.get $base)) (i32.const 16)))
      (i32.shl (i32.load8_u offset=2 (global.get $base)) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))

  ;; SHA-256's shape exactly: base plus a scaled index plus the lane, with the
  ;; constant added to the *index* so no value in the function holds the
  ;; address of any lane but the first.
  (func (export "be32_indexed") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base)
                 (i32.shl (global.get $idx) (i32.const 2)))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base)
                 (i32.add (i32.shl (global.get $idx) (i32.const 2)) (i32.const 1))))
               (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base)
                 (i32.add (i32.shl (global.get $idx) (i32.const 2)) (i32.const 2))))
               (i32.const 8)))
               (i32.load8_u (i32.add (global.get $base)
                 (i32.add (i32.shl (global.get $idx) (i32.const 2)) (i32.const 3))))))

  ;; A displacement reached by subtracting: the sum has to read it as one.
  (func (export "be32_sub") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.sub (global.get $z) (i32.const 3))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.sub (global.get $z) (i32.const 2))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.sub (global.get $z) (i32.const 1))) (i32.const 8)))
              (i32.load8_u (global.get $z))))

  ;; ---- eight bytes -------------------------------------------------------
  (func (export "be64") (result i64)
    (i64.or (i64.or (i64.or (i64.or (i64.or (i64.or (i64.or
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 0))) (i64.const 56))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 1))) (i64.const 48)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 2))) (i64.const 40)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 3))) (i64.const 32)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 4))) (i64.const 24)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 5))) (i64.const 16)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 6))) (i64.const 8)))
               (i64.load8_u (i32.add (global.get $base) (i32.const 7)))))

  (func (export "le64") (result i64)
    (i64.or (i64.or (i64.or (i64.or (i64.or (i64.or (i64.or
               (i64.load8_u (i32.add (global.get $base) (i32.const 0)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 1))) (i64.const 8)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 2))) (i64.const 16)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 3))) (i64.const 24)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 4))) (i64.const 32)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 5))) (i64.const 40)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 6))) (i64.const 48)))
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 7))) (i64.const 56))))

  ;; ---- two bytes, which is a narrow load and a reversal that is not `rev` -
  (func (export "be16") (result i32)
    (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 8))
               (i32.load8_u (i32.add (global.get $base) (i32.const 1)))))

  (func (export "le16") (result i32)
    (i32.or
               (i32.load8_u (i32.add (global.get $base) (i32.const 0)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 8))))

  ;; Two bytes into an i64: reversed at 64 bits and shifted down 48.
  (func (export "be16_64") (result i64)
    (i64.or
      (i64.shl (i64.load8_u (i32.add (global.get $base) (i32.const 0))) (i64.const 8))
               (i64.load8_u (i32.add (global.get $base) (i32.const 1)))))

  ;; ---- halfword lanes, so the stride is two rather than one --------------
  (func (export "be_h32") (result i32)
    (i32.or
      (i32.shl (i32.load16_u (i32.add (global.get $base) (i32.const 0))) (i32.const 16))
               (i32.load16_u (i32.add (global.get $base) (i32.const 2)))))

  (func (export "le_h32") (result i32)
    (i32.or
               (i32.load16_u (i32.add (global.get $base) (i32.const 0)))
      (i32.shl (i32.load16_u (i32.add (global.get $base) (i32.const 2))) (i32.const 16))))

  ;; Four halfwords into an i64.
  (func (export "le_h64") (result i64)
    (i64.or (i64.or (i64.or
               (i64.load16_u (i32.add (global.get $base) (i32.const 0)))
      (i64.shl (i64.load16_u (i32.add (global.get $base) (i32.const 2))) (i64.const 16)))
      (i64.shl (i64.load16_u (i32.add (global.get $base) (i32.const 4))) (i64.const 32)))
      (i64.shl (i64.load16_u (i32.add (global.get $base) (i32.const 6))) (i64.const 48))))

  ;; ---- near misses -------------------------------------------------------
  ;; A gap: bytes 0,1,2,4 are not four consecutive bytes.
  (func (export "nm_gap") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 4)))))

  ;; The same byte twice, so one displacement is missing.
  (func (export "nm_dup") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))

  ;; Consecutive bytes and a complete set of shifts, paired by neither the
  ;; identity nor its reverse.  This is the one a rule that checked the two
  ;; sets separately would get wrong.
  (func (export "nm_perm") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 8)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 16)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))

  ;; A signed lane sets the bits above its byte, so it is not a lane.
  (func (export "nm_signed") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_s (i32.add (global.get $base) (i32.const 3)))))

  ;; A shift that is not a whole lane.
  (func (export "nm_shift") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 4)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))

  ;; Three lanes is not a load width.
  (func (export "nm_three") (result i32)
    (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 16))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 2)))))

  ;; A store between two lanes, to a byte an *earlier* lane has already read.
  ;; The two lanes before it see the old byte and the wide load would see the
  ;; new one, so this is the case where combining across a store answers with
  ;; a different number rather than merely a different schedule.  It runs in
  ;; memory 1 so that everything reading memory 0 is unaffected by it, and the
  ;; address goes through a global so that forwarding cannot answer the load
  ;; from the store and hide the question.
  (func (export "nm_store") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local.set $a (i32.load8_u 1 (i32.add (global.get $base) (i32.const 0))))
    (local.set $b (i32.load8_u 1 (i32.add (global.get $base) (i32.const 1))))
    (i32.store8 1 (global.get $st) (i32.const 0x99))
    (local.set $c (i32.load8_u 1 (i32.add (global.get $base) (i32.const 2))))
    (local.set $d (i32.load8_u 1 (i32.add (global.get $base) (i32.const 3))))
    (i32.or (i32.or (i32.or
      (i32.shl (local.get $a) (i32.const 24))
      (i32.shl (local.get $b) (i32.const 16)))
      (i32.shl (local.get $c) (i32.const 8)))
              (local.get $d)))

  ;; A call between two lanes.
  (func (export "nm_call") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local.set $a (i32.load8_u (i32.add (global.get $base) (i32.const 0))))
    (local.set $b (i32.load8_u (i32.add (global.get $base) (i32.const 1))))
    (drop (call $side))
    (local.set $c (i32.load8_u (i32.add (global.get $base) (i32.const 2))))
    (local.set $d (i32.load8_u (i32.add (global.get $base) (i32.const 3))))
    (i32.or (i32.or (i32.or
      (i32.shl (local.get $a) (i32.const 24))
      (i32.shl (local.get $b) (i32.const 16)))
      (i32.shl (local.get $c) (i32.const 8)))
              (local.get $d)))

  ;; A division between two lanes: it traps for a reason a load does not, so
  ;; which of the two comes first is observable.
  (func (export "nm_div") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $q i32)
    (local.set $a (i32.load8_u (i32.add (global.get $base) (i32.const 0))))
    (local.set $b (i32.load8_u (i32.add (global.get $base) (i32.const 1))))
    (local.set $q (i32.div_u (i32.const 100) (global.get $idx)))
    (local.set $c (i32.load8_u (i32.add (global.get $base) (i32.const 2))))
    (local.set $d (i32.load8_u (i32.add (global.get $base) (i32.const 3))))
    (i32.add (local.get $q)
      (i32.or (i32.or (i32.or
        (i32.shl (local.get $a) (i32.const 24))
        (i32.shl (local.get $b) (i32.const 16)))
        (i32.shl (local.get $c) (i32.const 8)))
                (local.get $d))))

  ;; A lane read twice: retiring the load would leave the second use without a
  ;; value, so the group is refused.
  (func (export "nm_two_uses") (result i32)
    (local $a i32)
    (local.set $a (i32.load8_u (i32.add (global.get $base) (i32.const 0))))
    (i32.add (local.get $a)
      (i32.or (i32.or (i32.or
        (i32.shl (local.get $a) (i32.const 24))
        (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
        (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
                 (i32.load8_u (i32.add (global.get $base) (i32.const 3))))))

  ;; Two memories share no byte, so four consecutive displacements in two of
  ;; them are not four consecutive bytes anywhere.
  (func (export "nm_mem") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u 0 (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u 1 (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u 0 (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_u 0 (i32.add (global.get $base) (i32.const 3)))))

  ;; The whole group in memory 1, which is a group -- the memory index has to
  ;; travel with the combined load rather than defaulting to zero.
  (func (export "be32_mem1") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u 1 (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u 1 (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u 1 (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_u 1 (i32.add (global.get $base) (i32.const 3)))))

  ;; An inner `or` that something else reads: folding it away would leave the
  ;; other user without its value, so only the maximal tree is a candidate and
  ;; this one has a use in the middle of it.
  (func (export "nm_inner_use") (result i32)
    (local $hi i32)
    (local.set $hi (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16))))
    (i32.add (local.get $hi)
      (i32.or (i32.or (local.get $hi)
        (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
                 (i32.load8_u (i32.add (global.get $base) (i32.const 3))))))

  ;; The base moves, so the same expressions read different bytes: this runs
  ;; after everything above and the answers below it are against base = 4.
  (func (export "move_base") (result i32)
    (global.set $base (i32.const 4))
    (global.get $base))

  (func (export "be32_moved") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $base) (i32.const 2))) (i32.const 8)))
              (i32.load8_u (i32.add (global.get $base) (i32.const 3)))))
)
