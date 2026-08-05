;; The vector instruction set, in the shapes the upstream corpus cannot reach.
;;
;; `simd_*.wast` is written one operation per function: two parameters in, one
;; result out, nothing live, no loop, no call.  That proves each operation's
;; arithmetic and proves nothing about the compiler around it.  What is here is
;; the other half -- what happens when a vector has to be held, spilled,
;; carried across a call or a merge, reasoned about by the alias analysis, or
;; sit in a module the vectoriser is also looking at.
;;
;; Every export takes no arguments, so `wasm-interp --run-all-exports` and the
;; compiled binary compare directly; anything that must not be constant-folded
;; is reached through a mutable global instead of a literal.
(module
  (memory 1)
  (global $op (mut i32) (i32.const 0))       ;; opaque to the folder
  (global $ctr (mut i32) (i32.const 0))
  (global $gv (mut v128) (v128.const i32x4 0 0 0 0))

  (func $bump (param i32) (result i32)
    (global.set $ctr (i32.add (global.get $ctr) (local.get 0)))
    (global.get $ctr))

  ;; A callee that fills the callee-saved vector registers with whole 128-bit
  ;; values.  AAPCS64 promises its caller only the *low half* of v8-v15 back,
  ;; and that is all warmc's prologue saves -- so a vector the caller left in
  ;; one of them loses its top eight bytes across this call.  It takes more
  ;; than eighteen live vectors to reach v8 at all, because the allocator
  ;; hands out the caller-saved ones first; a callee that stays below that
  ;; cannot show the bug, which is why this one is the size it is.
  (func $vchurn (param i32) (result i32)
    (local $v00 v128) (local $v01 v128) (local $v02 v128) (local $v03 v128)
    (local $v04 v128) (local $v05 v128) (local $v06 v128) (local $v07 v128)
    (local $v08 v128) (local $v09 v128) (local $v10 v128) (local $v11 v128)
    (local $v12 v128) (local $v13 v128) (local $v14 v128) (local $v15 v128)
    (local $v16 v128) (local $v17 v128) (local $v18 v128) (local $v19 v128)
    (local $v20 v128) (local $v21 v128)
    (local.set $v00 (v128.const i64x2 0x1111111111111111 0x0123456789abcdef))
    (local.set $v01 (v128.const i64x2 0x2222222222222222 0x1032547698badcfe))
    (local.set $v02 (v128.const i64x2 0x3333333333333333 0x23016745ab89efcd))
    (local.set $v03 (v128.const i64x2 0x4444444444444444 0x32107654ba98fedc))
    (local.set $v04 (v128.const i64x2 0x5555555555555555 0x45670123cdef89ab))
    (local.set $v05 (v128.const i64x2 0x6666666666666666 0x54761032dcfe98ba))
    (local.set $v06 (v128.const i64x2 0x7777777777777777 0x67452301efcdab89))
    (local.set $v07 (v128.const i64x2 0x8888888888888888 0x76543210fedcba98))
    (local.set $v08 (v128.const i64x2 0x9999999999999999 0x89abcdef01234567))
    (local.set $v09 (v128.const i64x2 0xaaaaaaaaaaaaaaaa 0x98badcfe10325476))
    (local.set $v10 (v128.const i64x2 0xbbbbbbbbbbbbbbbb 0xab89efcd23016745))
    (local.set $v11 (v128.const i64x2 0xcccccccccccccccc 0xba98fedc32107654))
    (local.set $v12 (v128.const i64x2 0xdddddddddddddddd 0xcdef89ab45670123))
    (local.set $v13 (v128.const i64x2 0xeeeeeeeeeeeeeeee 0xdcfe98ba54761032))
    (local.set $v14 (v128.const i64x2 0xffffffffffffffff 0xefcdab8967452301))
    (local.set $v15 (v128.const i64x2 0x1111111111111110 0xfedcba9876543210))
    (local.set $v16 (v128.const i64x2 0x2222222222222221 0x1032547698badcff))
    (local.set $v17 (v128.const i64x2 0x3333333333333332 0x23016745ab89efce))
    (local.set $v18 (v128.const i64x2 0x4444444444444443 0x32107654ba98fedd))
    (local.set $v19 (v128.const i64x2 0x5555555555555554 0x45670123cdef89ac))
    (local.set $v20 (v128.const i64x2 0x6666666666666665 0x54761032dcfe98bb))
    (local.set $v21 (v128.const i64x2 0x7777777777777776 0x67452301efcdab8a))
    (global.set $ctr (i32.add (global.get $ctr) (local.get 0)))
    (i32x4.extract_lane 3
      (v128.xor (local.get $v00)
      (v128.xor (local.get $v01)
      (v128.xor (local.get $v02)
      (v128.xor (local.get $v03)
      (v128.xor (local.get $v04)
      (v128.xor (local.get $v05)
      (v128.xor (local.get $v06)
      (v128.xor (local.get $v07)
      (v128.xor (local.get $v08)
      (v128.xor (local.get $v09)
      (v128.xor (local.get $v10)
      (v128.xor (local.get $v11)
      (v128.xor (local.get $v12)
      (v128.xor (local.get $v13)
      (v128.xor (local.get $v14)
      (v128.xor (local.get $v15)
      (v128.xor (local.get $v16)
      (v128.xor (local.get $v17)
      (v128.xor (local.get $v18)
      (v128.xor (local.get $v19)
      (v128.xor (local.get $v20)
      (local.get $v21)))))))))))))))))))))))
    )

  ;; One vector held across a call to it, which is the whole shape.
  (func (export "one_across_call") (result v128)
    (local $v v128)
    (local.set $v (v128.const i64x2 0x0123456789abcdef 0xfedcba9876543210))
    (drop (call $vchurn (i32.const 1)))
    (local.get $v))

  ;; ---- the width of a vector access, as the alias analysis sees it -------
  ;;
  ;; A v128 access is sixteen bytes, and the field that says so had two bits
  ;; until this instruction set arrived.  Two bits read a size of four as a
  ;; size of zero, so a vector store would claim to touch one byte: every one
  ;; of these turns into a wrong answer rather than a slow one, and none of
  ;; them is expressible in a file that never mixes access widths.

  ;; A scalar store inside the sixteen bytes has to invalidate them.
  (func (export "alias_inside") (result v128)
    (v128.store (i32.const 64) (v128.const i32x4 1 2 3 4))
    (i32.store (i32.const 68) (i32.const 0x999))
    (v128.load (i32.const 64)))

  ;; A scalar store at the last byte of them, too.
  (func (export "alias_edge") (result v128)
    (v128.store (i32.const 96) (v128.const i32x4 1 2 3 4))
    (i32.store8 (i32.const 111) (i32.const 0x5a))
    (v128.load (i32.const 96)))

  ;; And one just past them must not.
  (func (export "alias_past") (result v128)
    (v128.store (i32.const 128) (v128.const i32x4 5 6 7 8))
    (i32.store (i32.const 144) (i32.const 0x777))
    (v128.load (i32.const 128)))

  ;; A vector store over a scalar one: the scalar read afterwards sees the
  ;; vector's bytes, and the vector store is not dead.
  (func (export "alias_vec_over_scalar") (result i32)
    (i32.store (i32.const 160) (i32.const 0x11111111))
    (v128.store (i32.const 160) (v128.const i32x4 0xaaaa 0xbbbb 0xcccc 0xdddd))
    (i32.add (i32.load (i32.const 160)) (i32.load (i32.const 172))))

  ;; A scalar store over part of a vector one: the vector store still has to
  ;; happen, because it is only partly overwritten.
  (func (export "alias_scalar_over_vec") (result i32)
    (v128.store (i32.const 192) (v128.const i32x4 0x1010 0x1010 0x1010 0x1010))
    (i32.store (i32.const 192) (i32.const 42))
    (i32.add (i32.load (i32.const 192)) (i32.load (i32.const 204))))

  ;; What forwarding is allowed to do: the same location, the same width.
  (func (export "forward_vec") (result v128)
    (v128.store (i32.const 224) (v128.const i32x4 0x1234 0x5678 0x9abc 0xdef0))
    (v128.load (i32.const 224)))

  ;; Two vector accesses sixteen bytes apart are distinct locations.
  (func (export "forward_two") (result v128)
    (v128.store (i32.const 256) (v128.const i32x4 1 1 1 1))
    (v128.store (i32.const 272) (v128.const i32x4 2 2 2 2))
    (v128.xor (v128.load (i32.const 256)) (v128.load (i32.const 272))))

  ;; A narrow vector load is the width it really reads, not sixteen: a byte
  ;; store one byte later must not disturb `load8_splat`, and one at the same
  ;; byte must.
  (func (export "alias_splat_same") (result v128)
    (i32.store8 (i32.const 300) (i32.const 0x11))
    (v128.load8_splat (i32.const 300)))
  (func (export "alias_splat_next") (result v128)
    (i32.store8 (i32.const 320) (i32.const 0x22))
    (v128.store (i32.const 336) (v128.const i32x4 -1 -1 -1 -1))
    (v128.load8_splat (i32.const 320)))

  ;; ---- addressing modes --------------------------------------------------
  ;;
  ;; `ldr q` scales its immediate by sixteen, so a displacement that is not a
  ;; multiple of sixteen has to reach memory a different way, and a constant
  ;; address has to be rejected as an immediate rather than truncated.
  (func (export "load_off_aligned") (result v128)
    (v128.store offset=32 (i32.const 384) (v128.const i32x4 9 8 7 6))
    (v128.load offset=32 (i32.const 384)))
  (func (export "load_off_unaligned") (result v128)
    (v128.store offset=1 (i32.const 448) (v128.const i32x4 0x4a 0x4b 0x4c 0x4d))
    (v128.load offset=1 (i32.const 448)))
  (func (export "load_off_large") (result v128)
    (v128.store offset=60000 (i32.const 0) (v128.const i32x4 0x71 0x72 0x73 0x74))
    (v128.load offset=60000 (i32.const 0)))
  ;; A dynamic address, so nothing folds into an immediate at all.
  (func (export "load_dynamic") (result v128)
    (global.set $op (i32.const 512))
    (v128.store (global.get $op) (v128.const i32x4 0x81 0x82 0x83 0x84))
    (v128.load (global.get $op)))
  ;; Several displacements off one address, which is what makes the back end
  ;; name a shared pointer -- and a vector access can only take a displacement
  ;; that is a multiple of sixteen.
  (func (export "load_shared_ptr") (result v128)
    (global.set $op (i32.const 544))
    (v128.store offset=0 (global.get $op) (v128.const i32x4 1 2 3 4))
    (v128.store offset=16 (global.get $op) (v128.const i32x4 5 6 7 8))
    (v128.store offset=32 (global.get $op) (v128.const i32x4 9 10 11 12))
    (v128.xor (v128.load offset=0 (global.get $op))
              (v128.xor (v128.load offset=16 (global.get $op))
                        (v128.load offset=32 (global.get $op)))))

  ;; ---- register pressure -------------------------------------------------
  ;;
  ;; Twenty-eight vectors all defined before any of them is read, so more are
  ;; live at once than there are registers to hold them and the rest go to
  ;; sixteen-byte frame slots.  Nothing in the corpus has ever needed two.
  (func (export "pressure") (result v128)
    (local $a v128) (local $b v128) (local $c v128) (local $d v128)
    (local $e v128) (local $f v128) (local $g v128) (local $h v128)
    (local $i v128) (local $j v128) (local $k v128) (local $l v128)
    (local $m v128) (local $n v128) (local $o v128) (local $p v128)
    (local $q v128) (local $r v128) (local $s v128) (local $t v128)
    (local $u v128) (local $v v128) (local $w v128) (local $x v128)
    (local $y v128) (local $z v128) (local $aa v128) (local $ab v128)
    (local.set $a (v128.const i32x4 0x01 0x02 0x03 0x04))
    (local.set $b (v128.const i32x4 0x05 0x06 0x07 0x08))
    (local.set $c (v128.const i32x4 0x09 0x0a 0x0b 0x0c))
    (local.set $d (v128.const i32x4 0x0d 0x0e 0x0f 0x10))
    (local.set $e (v128.const i32x4 0x11 0x12 0x13 0x14))
    (local.set $f (v128.const i32x4 0x15 0x16 0x17 0x18))
    (local.set $g (v128.const i32x4 0x19 0x1a 0x1b 0x1c))
    (local.set $h (v128.const i32x4 0x1d 0x1e 0x1f 0x20))
    (local.set $i (v128.const i32x4 0x21 0x22 0x23 0x24))
    (local.set $j (v128.const i32x4 0x25 0x26 0x27 0x28))
    (local.set $k (v128.const i32x4 0x29 0x2a 0x2b 0x2c))
    (local.set $l (v128.const i32x4 0x2d 0x2e 0x2f 0x30))
    (local.set $m (v128.const i32x4 0x31 0x32 0x33 0x34))
    (local.set $n (v128.const i32x4 0x35 0x36 0x37 0x38))
    (local.set $o (v128.const i32x4 0x39 0x3a 0x3b 0x3c))
    (local.set $p (v128.const i32x4 0x3d 0x3e 0x3f 0x40))
    (local.set $q (v128.const i32x4 0x41 0x42 0x43 0x44))
    (local.set $r (v128.const i32x4 0x45 0x46 0x47 0x48))
    (local.set $s (v128.const i32x4 0x49 0x4a 0x4b 0x4c))
    (local.set $t (v128.const i32x4 0x4d 0x4e 0x4f 0x50))
    (local.set $u (v128.const i32x4 0x51 0x52 0x53 0x54))
    (local.set $v (v128.const i32x4 0x55 0x56 0x57 0x58))
    (local.set $w (v128.const i32x4 0x59 0x5a 0x5b 0x5c))
    (local.set $x (v128.const i32x4 0x5d 0x5e 0x5f 0x60))
    (local.set $y (v128.const i32x4 0x61 0x62 0x63 0x64))
    (local.set $z (v128.const i32x4 0x65 0x66 0x67 0x68))
    (local.set $aa (v128.const i32x4 0x69 0x6a 0x6b 0x6c))
    (local.set $ab (v128.const i32x4 0x6d 0x6e 0x6f 0x70))
    (i32x4.add (local.get $a)
    (i32x4.add (local.get $b)
    (i32x4.add (local.get $c)
    (i32x4.add (local.get $d)
    (i32x4.add (local.get $e)
    (i32x4.add (local.get $f)
    (i32x4.add (local.get $g)
    (i32x4.add (local.get $h)
    (i32x4.add (local.get $i)
    (i32x4.add (local.get $j)
    (i32x4.add (local.get $k)
    (i32x4.add (local.get $l)
    (i32x4.add (local.get $m)
    (i32x4.add (local.get $n)
    (i32x4.add (local.get $o)
    (i32x4.add (local.get $p)
    (i32x4.add (local.get $q)
    (i32x4.add (local.get $r)
    (i32x4.add (local.get $s)
    (i32x4.add (local.get $t)
    (i32x4.add (local.get $u)
    (i32x4.add (local.get $v)
    (i32x4.add (local.get $w)
    (i32x4.add (local.get $x)
    (i32x4.add (local.get $y)
    (i32x4.add (local.get $z)
    (i32x4.add (local.get $aa)
    (local.get $ab)))))))))))))))))))))))))))))

  ;; The same, with a call in the middle: AAPCS64 preserves only the low half
  ;; of v8-v15, so no vector live across the call may be in a register at all.
  (func (export "pressure_call") (result v128)
    (local $a v128) (local $b v128) (local $c v128) (local $d v128)
    (local $e v128) (local $f v128) (local $g v128) (local $h v128)
    (local $i v128) (local $j v128)
    (local.set $a (v128.const i64x2 0x0101010102020202 0x0303030304040404))
    (local.set $b (v128.const i64x2 0x0505050506060606 0x0707070708080808))
    (local.set $c (v128.const i64x2 0x0909090910101010 0x1111111112121212))
    (local.set $d (v128.const i64x2 0x1313131314141414 0x1515151516161616))
    (local.set $e (v128.const i64x2 0x1717171718181818 0x1919191920202020))
    (local.set $f (v128.const i64x2 0x2121212122222222 0x2323232324242424))
    (local.set $g (v128.const i64x2 0x2525252526262626 0x2727272728282828))
    (local.set $h (v128.const i64x2 0x2929292930303030 0x3131313132323232))
    (local.set $i (v128.const i64x2 0x3333333334343434 0x3535353536363636))
    (drop (call $vchurn (i32.const 1)))
    (local.set $j (v128.const i64x2 0x3737373738383838 0x3939393940404040))
    (drop (call $vchurn (i32.const 2)))
    (v128.xor (local.get $a)
    (v128.xor (local.get $b)
    (v128.xor (local.get $c)
    (v128.xor (local.get $d)
    (v128.xor (local.get $e)
    (v128.xor (local.get $f)
    (v128.xor (local.get $g)
    (v128.xor (local.get $h)
    (v128.xor (local.get $i)
    (local.get $j)))))))))))

  ;; ---- calls -------------------------------------------------------------
  ;; A vector computed by a callee and used by the caller, both ways round.
  (func $vadd (param v128 v128) (result v128)
    (i16x8.add (local.get 0) (local.get 1)))
  (func (export "call_vec") (result v128)
    (call $vadd (v128.const i16x8 1 2 3 4 5 6 7 8)
                (v128.const i16x8 10 20 30 40 50 60 70 80)))
  (func $vsum (param v128) (result i32) (i32x4.extract_lane 2 (local.get 0)))
  (func (export "call_vec_scalar") (result i32)
    (call $vsum (v128.const i32x4 0x11 0x22 0x33 0x44)))

  ;; ---- merges ------------------------------------------------------------
  ;; A vector carried on a block parameter through the parallel copy, with a
  ;; real operation on each arm so both are live.
  (func (export "merge_vec") (result v128)
    (global.set $op (i32.const 1))
    (if (result v128) (global.get $op)
      (then (i8x16.add (v128.const i8x16 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1)
                       (v128.const i8x16 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2)))
      (else (i8x16.sub (v128.const i8x16 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9)
                       (v128.const i8x16 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3)))))

  ;; A loop that accumulates in a vector, so the accumulator is a loop-carried
  ;; block parameter and the vector has to survive the back edge.
  (func (export "loop_accum") (result v128)
    (local $i i32) (local $acc v128)
    (local.set $acc (v128.const i32x4 0 0 0 0))
    (block $done (loop $L
      (br_if $done (i32.ge_u (local.get $i) (i32.const 10)))
      (local.set $acc (i32x4.add (local.get $acc)
                                 (i32x4.splat (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $L)))
    (local.get $acc))

  ;; A vector value carried through a loop the automatic vectoriser is
  ;; otherwise interested in: the i32 array traffic below is exactly the shape
  ;; it widens, and it must leave the whole loop alone rather than widen half
  ;; of it.  A new pass does not only add its own bugs -- it changes which
  ;; existing ones fire.
  (func (export "loop_mixed") (result v128)
    (local $i i32) (local $v v128)
    (local.set $v (v128.const i32x4 1 2 3 4))
    (block $done (loop $L
      (br_if $done (i32.ge_u (local.get $i) (i32.const 16)))
      (i32.store (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 4)))
                 (i32.mul (local.get $i) (i32.const 3)))
      (local.set $v (i32x4.add (local.get $v) (v128.const i32x4 1 1 1 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $L)))
    (i32x4.add (local.get $v) (v128.load (i32.const 1024))))

  ;; ---- shifts by an amount that is not a literal -------------------------
  ;; wasm reduces the amount modulo the lane width, and the register form of
  ;; the instruction takes a signed per-lane shift, so a right shift is a
  ;; negative left one.  A literal takes a different path entirely.
  (func (export "shl_dyn_small") (result v128)
    (global.set $op (i32.const 3))
    (i8x16.shl (v128.const i8x16 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
               (global.get $op)))
  (func (export "shl_dyn_wrap") (result v128)
    (global.set $op (i32.const 9))          ;; nine on a byte lane is one
    (i8x16.shl (v128.const i8x16 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
               (global.get $op)))
  (func (export "shr_s_dyn") (result v128)
    (global.set $op (i32.const 33))         ;; and one on a word lane
    (i32x4.shr_s (v128.const i32x4 0x80000000 -1 0x7fffffff 12345)
                 (global.get $op)))
  (func (export "shr_u_dyn_neg") (result v128)
    (global.set $op (i32.const -1))         ;; 0xffffffff, so 63 on i64x2
    (i64x2.shr_u (v128.const i64x2 0x8000000000000000 -1) (global.get $op)))
  (func (export "shl_dyn_zero") (result v128)
    (global.set $op (i32.const 64))         ;; zero on i64x2: the identity
    (i64x2.shl (v128.const i64x2 0x123456789abcdef0 -1) (global.get $op)))

  ;; ---- the operations with no single instruction behind them -------------
  ;; A 64-bit lane multiply is built out of 32-bit pieces, so it needs values
  ;; where both cross products and the carry out of the low product matter.
  (func (export "mul64") (result v128)
    (i64x2.mul (v128.const i64x2 0x0123456789abcdef 0xffffffffffffffff)
               (v128.const i64x2 0x1122334455667788 0x0000000000000002)))
  (func (export "mul64_hi") (result v128)
    (i64x2.mul (v128.const i64x2 0x0000000100000000 0xdeadbeefcafebabe)
               (v128.const i64x2 0x0000000100000000 0x0000000100000001)))

  ;; A shuffle that names only the first table, only the second, and both --
  ;; three different lowerings, and only the third needs two lookups.
  (func (export "shuffle_lo") (result v128)
    (i8x16.shuffle 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
      (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
      (v128.const i8x16 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31)))
  (func (export "shuffle_hi") (result v128)
    (i8x16.shuffle 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
      (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
      (v128.const i8x16 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31)))
  (func (export "shuffle_both") (result v128)
    (i8x16.shuffle 0 16 1 17 2 18 3 19 4 20 5 21 6 22 7 23
      (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
      (v128.const i8x16 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31)))
  ;; A swizzle whose indices run past the table, which answers zero.
  (func (export "swizzle_oob") (result v128)
    (i8x16.swizzle (v128.const i8x16 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25)
                   (v128.const i8x16 0 15 16 255 1 2 3 4 5 6 7 8 9 10 11 12)))

  ;; The bit reductions, on patterns where each lane matters.
  (func (export "bitmask8") (result i32)
    (i8x16.bitmask (v128.const i8x16 -1 0 -1 0 0 0 0 -128 127 -1 0 0 0 0 0 -1)))
  (func (export "bitmask16") (result i32)
    (i16x8.bitmask (v128.const i16x8 -1 0 -32768 32767 0 -1 0 -1)))
  (func (export "bitmask32") (result i32)
    (i32x4.bitmask (v128.const i32x4 -1 0 0x7fffffff 0x80000000)))
  (func (export "bitmask64") (result i32)
    (i64x2.bitmask (v128.const i64x2 -1 0x7fffffffffffffff)))
  (func (export "alltrue_mixed") (result i32)
    (i32.add (i32x4.all_true (v128.const i32x4 1 2 3 4))
             (i32.mul (i32.const 2)
                      (i32x4.all_true (v128.const i32x4 1 0 3 4)))))
  (func (export "alltrue64") (result i32)
    (i32.add (i64x2.all_true (v128.const i64x2 1 -1))
             (i32.mul (i32.const 2)
                      (i64x2.all_true (v128.const i64x2 0 -1)))))
  (func (export "anytrue") (result i32)
    (i32.add (v128.any_true (v128.const i32x4 0 0 0 0))
             (i32.mul (i32.const 2)
                      (v128.any_true (v128.const i8x16 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0)))))

  ;; ---- lanes -------------------------------------------------------------
  ;; Every lane index of the narrow shapes, with values whose sign matters.
  (func (export "extract_s8") (result i32)
    (i32.add (i8x16.extract_lane_s 0 (v128.const i8x16 -1 -2 0 0 0 0 0 0 0 0 0 0 0 0 0 127))
             (i32.mul (i32.const 1000)
                      (i8x16.extract_lane_s 15 (v128.const i8x16 -1 -2 0 0 0 0 0 0 0 0 0 0 0 0 0 127)))))
  (func (export "extract_u8") (result i32)
    (i32.add (i8x16.extract_lane_u 0 (v128.const i8x16 -1 -2 0 0 0 0 0 0 0 0 0 0 0 0 0 127))
             (i8x16.extract_lane_u 1 (v128.const i8x16 -1 -2 0 0 0 0 0 0 0 0 0 0 0 0 0 127))))
  (func (export "extract_s16") (result i32)
    (i16x8.extract_lane_s 7 (v128.const i16x8 0 0 0 0 0 0 0 -32768)))
  (func (export "extract_f") (result i64)
    (i64.add (i64.extend_i32_u (i32.reinterpret_f32 (f32x4.extract_lane 3 (v128.const f32x4 1 2 3 -4.5))))
             (i64.reinterpret_f64 (f64x2.extract_lane 1 (v128.const f64x2 1.5 -2.75)))))
  ;; Replacing a lane must leave every other lane exactly as it was.
  (func (export "replace_lanes") (result v128)
    (i16x8.replace_lane 7
      (i16x8.replace_lane 0
        (v128.const i16x8 1 2 3 4 5 6 7 8) (i32.const 0x1234))
      (i32.const -1)))
  (func (export "replace_f64") (result v128)
    (f64x2.replace_lane 0 (v128.const f64x2 1.5 -2.75) (f64.const 1e300)))
  ;; The scalar being inserted comes from the same register file as the
  ;; vector, which is where a copy can land on top of it.
  (func (export "replace_f32_self") (result v128)
    (local $v v128)
    (local.set $v (v128.const f32x4 1 2 3 4))
    (f32x4.replace_lane 2 (local.get $v) (f32x4.extract_lane 0 (local.get $v))))

  ;; ---- narrow loads and stores -------------------------------------------
  ;; Every lane of the lane forms, and the splat/extend/zero loads, reached
  ;; through memory that a previous store put there.
  (func (export "lane_roundtrip") (result v128)
    (v128.store (i32.const 640) (v128.const i8x16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0))
    (i32.store (i32.const 656) (i32.const 0xaabbccdd))
    (v128.store8_lane 3 (i32.const 660)
      (v128.load32_lane 1 (i32.const 656) (v128.const i32x4 -1 -1 -1 -1)))
    (v128.load (i32.const 656)))
  (func (export "load_splat_all") (result v128)
    (i64.store (i32.const 704) (i64.const 0x0102030405060708))
    (v128.xor (v128.load8_splat (i32.const 704))
              (v128.xor (v128.load16_splat (i32.const 704))
                        (v128.xor (v128.load32_splat (i32.const 704))
                                  (v128.load64_splat (i32.const 704))))))
  (func (export "load_extend_all") (result v128)
    (i64.store (i32.const 736) (i64.const 0x80ff017fc0004001))
    (i16x8.add (v128.load8x8_s (i32.const 736)) (v128.load8x8_u (i32.const 736))))
  (func (export "load_zero_both") (result v128)
    (i64.store (i32.const 768) (i64.const 0xdeadbeefcafebabe))
    (v128.or (v128.load32_zero (i32.const 768))
             (v128.load64_zero (i32.const 768))))

  ;; ---- globals -----------------------------------------------------------
  ;; A vector global written and read back with an operation in between, so
  ;; the sixteen-byte cell is reached from both directions.
  (func (export "global_rw") (result v128)
    (global.set $gv (i32x4.mul (v128.const i32x4 3 5 7 11)
                               (v128.const i32x4 13 17 19 23)))
    (i32x4.sub (global.get $gv) (v128.const i32x4 1 1 1 1)))

  ;; ---- floating point, where a NaN or a signed zero decides the answer ----
  (func (export "fmin_nan") (result v128)
    (f32x4.min (v128.const f32x4 nan 1 -0.0 3)
               (v128.const f32x4 2 nan 0.0 4)))
  (func (export "fmax_zero") (result v128)
    (f32x4.max (v128.const f32x4 -0.0 0.0 -1 nan)
               (v128.const f32x4 0.0 -0.0 1 5)))
  (func (export "pmin_nan") (result v128)
    (f64x2.pmin (v128.const f64x2 nan 3.5) (v128.const f64x2 1.5 nan)))
  (func (export "pmax_nan") (result v128)
    (f64x2.pmax (v128.const f64x2 nan 3.5) (v128.const f64x2 1.5 nan)))
  (func (export "trunc_sat_edges") (result v128)
    (i32x4.trunc_sat_f32x4_s (v128.const f32x4 nan inf -inf 3.9)))
  (func (export "trunc_sat_zero_edges") (result v128)
    (i32x4.trunc_sat_f64x2_u_zero (v128.const f64x2 -1.5 1e30)))
)
