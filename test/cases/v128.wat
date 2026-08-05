;; v128 as a value type: parameters, results, locals, globals and slots.
;;
;; There are no operations on a vector yet -- those arrive with the instruction
;; set -- so what is exercised here is every place a 128-bit value has to be
;; moved, held or laid out: the argument registers and the outgoing stack area,
;; the return path including the multi-value area, a frame slot wide enough and
;; aligned for `str q`, and a global cell that is no longer eight bytes.
;;
;; Every export takes no arguments, so `wasm-interp --run-all-exports` and the
;; compiled binary can be compared directly; the shapes that need parameters
;; are reached through helpers.
(module
  (memory 1)

  (global $gm (mut v128) (v128.const i32x4 1 2 3 4))
  (global $gi v128 (v128.const i64x2 0x0011223344556677 0x8899aabbccddeeff))
  (global $gz (mut v128) (v128.const i8x16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0))
  (global $ctr (mut i32) (i32.const 0))
  ;; A scalar global on either side of the vector ones: their offsets move when
  ;; the slot widens, and reading the wrong one is the failure this catches.
  (global $before (mut i64) (i64.const 0x0123456789abcdef))
  (global $after (mut i32) (i32.const 12345))

  ;; ---- constants -------------------------------------------------------
  ;; The three shapes the materialiser distinguishes, plus the two edge
  ;; patterns that `movi` has to get right.
  (func (export "c_zero") (result v128) (v128.const i8x16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0))
  (func (export "c_ones") (result v128)
    (v128.const i8x16 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff
                      0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff))
  (func (export "c_bytesplat") (result v128)
    (v128.const i8x16 0x5a 0x5a 0x5a 0x5a 0x5a 0x5a 0x5a 0x5a
                      0x5a 0x5a 0x5a 0x5a 0x5a 0x5a 0x5a 0x5a))
  (func (export "c_halfsplat") (result v128)
    (v128.const i64x2 0x0123456789abcdef 0x0123456789abcdef))
  (func (export "c_general") (result v128)
    (v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00))
  ;; A pattern that repeats every eight bytes but not every one, so it takes
  ;; the `dup` path rather than `movi`.
  (func (export "c_dup") (result v128) (v128.const i32x4 0 1 0 1))

  ;; ---- parameters and results -----------------------------------------
  (func $id (param $v v128) (result v128) (local.get $v))
  (func (export "param_reg") (result v128)
    (call $id (v128.const i32x4 9 8 7 6)))

  ;; Nine vector parameters: only eight fit in q0-q7, so the ninth travels in
  ;; the caller's outgoing area and is read back by the prologue.
  (func $ninth (param v128 v128 v128 v128 v128 v128 v128 v128 v128) (result v128)
    (local.get 8))
  (func (export "param_stack") (result v128)
    (call $ninth
      (v128.const i32x4 1 1 1 1) (v128.const i32x4 2 2 2 2)
      (v128.const i32x4 3 3 3 3) (v128.const i32x4 4 4 4 4)
      (v128.const i32x4 5 5 5 5) (v128.const i32x4 6 6 6 6)
      (v128.const i32x4 7 7 7 7) (v128.const i32x4 8 8 8 8)
      (v128.const i32x4 0xcafe 0xbabe 0xf00d 0xfeed)))

  ;; The eighth argument still arrives in a register; check the boundary from
  ;; the other side.
  (func $eighth (param v128 v128 v128 v128 v128 v128 v128 v128) (result v128)
    (local.get 7))
  (func (export "param_last_reg") (result v128)
    (call $eighth
      (v128.const i32x4 1 1 1 1) (v128.const i32x4 2 2 2 2)
      (v128.const i32x4 3 3 3 3) (v128.const i32x4 4 4 4 4)
      (v128.const i32x4 5 5 5 5) (v128.const i32x4 6 6 6 6)
      (v128.const i32x4 7 7 7 7) (v128.const i32x4 0xdead 0xbeef 0 0)))

  ;; A vector between scalars, so the two argument sequences advance
  ;; independently and neither steals the other's register.
  (func $mixed (param i32 v128 i64 v128 f64 i32) (result v128) (local.get 3))
  (func (export "param_mixed") (result v128)
    (call $mixed (i32.const 1) (v128.const i32x4 10 20 30 40) (i64.const 2)
                 (v128.const i32x4 50 60 70 80) (f64.const 3.5) (i32.const 4)))

  ;; ---- locals ----------------------------------------------------------
  ;; A declared local starts as all zeros, like every other type.
  (func (export "local_zero") (result v128) (local $v v128) (local.get $v))

  (func (export "local_tee") (result v128)
    (local $a v128) (local $b v128)
    (local.set $b (local.tee $a (v128.const i32x4 0x101 0x202 0x303 0x404)))
    (local.get $b))

  ;; A vector local that must survive a call has no callee-saved home -- AAPCS64
  ;; preserves only the low half of v8-v15 -- so the allocator is obliged to
  ;; give it a frame slot.  The scalar local alongside it makes the slot area
  ;; an odd number of eight-byte slots, which is what the even-index rule for a
  ;; vector slot exists for.
  (func $bump (param i32) (result i32)
    (global.set $ctr (i32.add (global.get $ctr) (local.get 0)))
    (global.get $ctr))
  (func (export "across_call") (result v128)
    (local $v v128) (local $n i32)
    (local.set $v (v128.const i32x4 0xaaaa 0xbbbb 0xcccc 0xdddd))
    (local.set $n (call $bump (i32.const 3)))
    (drop (call $bump (local.get $n)))
    (local.get $v))

  ;; Several at once, so more than one vector slot is handed out and the
  ;; alignment has to hold for each of them.
  (func (export "across_call_many") (result v128)
    (local $a v128) (local $b v128) (local $c v128) (local $n i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (local.set $b (v128.const i32x4 5 6 7 8))
    (local.set $c (v128.const i32x4 9 10 11 12))
    (local.set $n (call $bump (i32.const 1)))
    (local.set $a (select (local.get $b) (local.get $a) (local.get $n)))
    (drop (call $bump (i32.const 1)))
    (select (local.get $c) (local.get $a) (i32.eqz (local.get $n))))

  ;; ---- globals ---------------------------------------------------------
  (func (export "global_get") (result v128) (global.get $gm))
  (func (export "global_imm") (result v128) (global.get $gi))
  (func (export "global_set") (result v128)
    (global.set $gm (v128.const i32x4 0x1111 0x2222 0x3333 0x4444))
    (global.get $gm))
  (func (export "global_zero") (result v128) (global.get $gz))
  ;; The scalars around them still read back what they were given.
  (func (export "global_before") (result i64) (global.get $before))
  (func (export "global_after") (result i32) (global.get $after))
  (func (export "global_scalar_rw") (result i64)
    (global.set $before (i64.const -1))
    (global.set $after (i32.const 999))
    (i64.add (global.get $before) (i64.extend_i32_u (global.get $after))))

  ;; ---- select ----------------------------------------------------------
  ;; `select` on a vector cannot be `fcsel`: that would move the low lane and
  ;; leave the other twelve bytes of whichever arm the destination came from.
  (func (export "select_true") (result v128)
    (select (v128.const i32x4 1 2 3 4) (v128.const i32x4 5 6 7 8) (i32.const 1)))
  (func (export "select_false") (result v128)
    (select (v128.const i32x4 1 2 3 4) (v128.const i32x4 5 6 7 8) (i32.const 0)))
  (func (export "select_typed") (result v128)
    (select (result v128)
      (v128.const i64x2 0x1111111111111111 0x2222222222222222)
      (v128.const i64x2 0x3333333333333333 0x4444444444444444)
      (i32.const 0)))
  ;; A condition that is a comparison, so it is folded into the flags rather
  ;; than materialised as a boolean first.
  (func (export "select_cmp") (result v128)
    (select (v128.const i32x4 0xf0f0 0 0 1) (v128.const i32x4 0x0f0f 0 0 2)
            (i32.lt_s (global.get $ctr) (i32.const 1000))))

  ;; ---- multi-value -----------------------------------------------------
  ;; Every result slot is sixteen bytes now, so a vector among scalars checks
  ;; that both the writer and the readers agree on the stride.
  (func $three (result i32 v128 i64)
    (i32.const 0x2a) (v128.const i32x4 0x77 0x88 0x99 0xaa) (i64.const -7))
  (func (export "multi_vec") (result v128)
    (local $x i32) (local $v v128) (local $y i64)
    (call $three)
    (local.set $y)
    (local.set $v)
    (local.set $x)
    (local.get $v))
  (func (export "multi_scalars") (result i64)
    (local $x i32) (local $v v128) (local $y i64)
    (call $three)
    (local.set $y)
    (local.set $v)
    (local.set $x)
    (i64.add (local.get $y) (i64.extend_i32_s (local.get $x))))

  ;; ---- branches --------------------------------------------------------
  ;; A vector carried across a merge is a block parameter, so it goes through
  ;; the parallel copy that sequences edge moves.
  (func (export "branch_merge") (result v128)
    (if (result v128) (i32.eqz (global.get $ctr))
      (then (v128.const i32x4 0x1a 0x2a 0x3a 0x4a))
      (else (v128.const i32x4 0x1b 0x2b 0x3b 0x4b))))

  (func (export "loop_carried") (result v128)
    (local $i i32) (local $v v128)
    (local.set $v (v128.const i32x4 0 0 0 0))
    (block $done (loop $L
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (local.set $v (select (v128.const i32x4 1 1 1 1) (local.get $v)
                            (i32.eq (local.get $i) (i32.const 2))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $L)))
    (local.get $v))
)
