;; Vector rotates written out by hand: a left shift and a logical right shift
;; of one value, or-ed.  wasm has no vector rotate, so this is how BLAKE3's
;; and ChaCha's lanes spell every one of theirs, and the arm64 back end turns
;; the three instructions into a shift right and a shift-left-and-insert, or
;; into a single byte or halfword swap when the rotate is by half the lane.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What it is for:
;;
;;   * every lane width, each with a rotate by half the lane (REV16, REV32,
;;     REV64 .4s) and one by some other amount (USHR + SLI), and the amount at
;;     both ends, 1 and lane-1;
;;   * the two orders of the or's operands;
;;   * amounts past the lane width, which wasm takes modulo the lane: a shl by
;;     35 and a shr_u by 29 on i32 lanes is a rotate by 3;
;;   * look-alikes that are not rotates and must not become one -- amounts
;;     that do not add up to the lane width, an arithmetic right shift, two
;;     different sources, a shift that has another use, a shift amount that
;;     is not a constant -- each of which, rotated, gives a different answer.
;;
;; The inputs come from memory so nothing folds them, and every lane holds
;; a different value with bits set at both ends, so a lane swapped for its
;; neighbour or a shift of the wrong width shows.
(module
  (memory 1)
  (data (i32.const 0)
    "\01\23\45\67\89\ab\cd\ef\fe\dc\ba\98\76\54\32\10"
    "\80\00\00\01\ff\00\00\ff\5a\a5\3c\c3\00\80\01\7f")

  (func $a (result v128) (v128.load (i32.const 0)))
  (func $b (result v128) (v128.load (i32.const 16)))

  ;; ---- i32 lanes ---------------------------------------------------------
  (func (export "r32_16") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 16))
             (i32x4.shr_u (local.get $x) (i32.const 16))))
  (func (export "r32_12") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 20))
             (i32x4.shr_u (local.get $x) (i32.const 12))))
  (func (export "r32_8_swapped") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i32x4.shr_u (local.get $x) (i32.const 8))
             (i32x4.shl (local.get $x) (i32.const 24))))
  (func (export "r32_1") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i32x4.shl (local.get $x) (i32.const 1))
             (i32x4.shr_u (local.get $x) (i32.const 31))))
  (func (export "r32_31") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 31))
             (i32x4.shr_u (local.get $x) (i32.const 1))))
  ;; 35 is 3 and 29 is 29 modulo 32: a rotate by 3.
  (func (export "r32_mod") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i32x4.shl (local.get $x) (i32.const 35))
             (i32x4.shr_u (local.get $x) (i32.const 29))))

  ;; ---- i16, i64 and i8 lanes --------------------------------------------
  (func (export "r16_8") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i16x8.shl (local.get $x) (i32.const 8))
             (i16x8.shr_u (local.get $x) (i32.const 8))))
  (func (export "r16_3") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i16x8.shr_u (local.get $x) (i32.const 13))
             (i16x8.shl (local.get $x) (i32.const 3))))
  (func (export "r64_32") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i64x2.shl (local.get $x) (i32.const 32))
             (i64x2.shr_u (local.get $x) (i32.const 32))))
  (func (export "r64_13") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i64x2.shl (local.get $x) (i32.const 13))
             (i64x2.shr_u (local.get $x) (i32.const 51))))
  (func (export "r8_4") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i8x16.shl (local.get $x) (i32.const 4))
             (i8x16.shr_u (local.get $x) (i32.const 4))))
  (func (export "r8_1") (result v128)
    (local $x v128)
    (local.set $x (call $b))
    (v128.or (i8x16.shl (local.get $x) (i32.const 1))
             (i8x16.shr_u (local.get $x) (i32.const 7))))

  ;; A chain the way BLAKE3 has it, rotate after rotate, each one's result
  ;; the next one's source: in place, the two-instruction form would clobber
  ;; its source before reading it the second time.
  (func (export "r32_chain") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (local.set $x (v128.or (i32x4.shl (local.get $x) (i32.const 16))
                           (i32x4.shr_u (local.get $x) (i32.const 16))))
    (local.set $x (i32x4.add (local.get $x) (call $b)))
    (local.set $x (v128.or (i32x4.shl (local.get $x) (i32.const 20))
                           (i32x4.shr_u (local.get $x) (i32.const 12))))
    (local.set $x (v128.xor (local.get $x) (call $a)))
    (local.set $x (v128.or (i32x4.shl (local.get $x) (i32.const 24))
                           (i32x4.shr_u (local.get $x) (i32.const 8))))
    (local.set $x (i32x4.add (local.get $x) (call $b)))
    (v128.or (i32x4.shl (local.get $x) (i32.const 25))
             (i32x4.shr_u (local.get $x) (i32.const 7))))

  ;; ---- look-alikes that are not rotates ---------------------------------
  ;; 3 + 30 is not 32: two bits of each lane come from both shifts.
  (func (export "n_sum") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 3))
             (i32x4.shr_u (local.get $x) (i32.const 30))))
  ;; An arithmetic shift drags the sign in from the top.
  (func (export "n_signed") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 8))
             (i32x4.shr_s (local.get $x) (i32.const 24))))
  ;; Two sources.
  (func (export "n_two") (result v128)
    (v128.or (i32x4.shl (call $a) (i32.const 8))
             (i32x4.shr_u (call $b) (i32.const 24))))
  ;; The left shift is used again, after the or.
  (func (export "n_reused") (result v128)
    (local $x v128) (local $l v128)
    (local.set $x (call $b))
    (local.set $l (i32x4.shl (local.get $x) (i32.const 5)))
    (v128.xor (v128.or (local.get $l)
                       (i32x4.shr_u (local.get $x) (i32.const 27)))
              (local.get $l)))
  ;; Amounts read from memory, which the back end shifts by register.
  (func (export "n_varamt") (result v128)
    (local $x v128) (local $k i32)
    (local.set $x (call $a))
    (local.set $k (i32.load8_u (i32.const 1)))
    (v128.or (i32x4.shl (local.get $x) (local.get $k))
             (i32x4.shr_u (local.get $x) (i32.sub (i32.const 32) (local.get $k)))))
  ;; A shift by a whole lane is no shift at all in wasm, so this is x | x.
  (func (export "n_zero") (result v128)
    (local $x v128)
    (local.set $x (call $a))
    (v128.or (i32x4.shl (local.get $x) (i32.const 32))
             (i32x4.shr_u (local.get $x) (i32.const 0))))
)
