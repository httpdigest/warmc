(module
  (memory 1 1)
  ;; A second memory, four pages larger than the first.  What a bounds check
  ;; compares against has to be the size of the memory the access names, and
  ;; the two ways of getting that wrong are opposite: an access to the large
  ;; one checked against the small one traps where it should not, and one to
  ;; the small one checked against the large one does not trap where it must.
  (memory $big 5 5)
  ;; A 64-bit memory.  The guard reservation is eight gigabytes, which covers
  ;; a 32-bit index space and nothing beyond it, so an address past 2^32 has
  ;; to be *checked* rather than faulted on -- at every level, including the
  ;; one whose whole point is that it emits no checks.
  (memory $w i64 1 1)
  ;; Thirty-four megabytes, which is the only reason this file needs a memory
  ;; larger than a few pages: the static-offset cap sits at thirty-two, and an
  ;; access *in bounds* on the far side of it has to have somewhere to land.
  (memory $huge 520 520)
  (table 2 funcref)
  ;; And a 64-bit table, after the one every existing case addresses, so that
  ;; table 0 is still the 32-bit one.
  (table $tw i64 2 2 funcref)
  (elem (i32.const 0) $one)
  (data (i32.const 0) "\08\00\00\00\ff\ff\ff\ff\00\00\00\00")
  (data (i32.const 16) "\40\00\00\00\00\00\00\00")
  ;; popcount 31, and popcount 63 -- one short of their own bound.
  (data (i32.const 32) "\ff\ff\ff\7f")
  (data (i32.const 40) "\ff\ff\ff\ff\ff\ff\ff\7f")
  ;; Negative as a signed 64-bit number and enormous as an unsigned one, which
  ;; is what tells a signed comparison from an unsigned one.
  (data (i32.const 48) "\00\00\00\00\00\00\00\80")
  ;; 16384 and 16383: the first word index the page does not hold, and the
  ;; last one it does.
  (data (i32.const 56) "\00\40\00\00\ff\3f\00\00")
  (type $void_i32 (func (result i32)))
  (type $unary (func (param i32) (result i32)))
  (type $binary (func (param i32 i32) (result i32)))
  (func $one (param i32) (result i32) (local.get 0))

  ;; Three bytes below the top of the 32-bit index space, held in a mutable
  ;; global so that nothing folds the address arithmetic that wraps off it.
  (global $top (mut i32) (i32.const 0xFFFFFFFD))
  ;; And address zero, for the group whose wrap is in its middle.
  (global $zero (mut i32) (i32.const 0))

  (func (export "ok") (result i32) (i32.const 1))

  (func (export "unreachable") (result i32) (unreachable))

  (func (export "load_oob") (result i32) (i32.load (i32.const 65533)))
  (func (export "load_oob_dyn") (result i32)
    (local $p i32)
    (local.set $p (i32.const 100000))
    (i32.load (local.get $p)))
  (func (export "store_oob") (i32.store (i32.const 65536) (i32.const 1)))
  (func (export "load_oob_off") (result i32) (i32.load offset=65535 (i32.const 8)))

  ;; Four byte loads shifted together become one word load, so the wide load
  ;; has to trap exactly where the four did.  The last four bytes of the page
  ;; are in bounds and the group starting one byte later is not.
  (func (export "ldcomb_last_ok") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.const 65532)) (i32.const 24))
      (i32.shl (i32.load8_u (i32.const 65533)) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.const 65534)) (i32.const 8)))
               (i32.load8_u (i32.const 65535))))
  (func (export "ldcomb_oob") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.const 65533)) (i32.const 24))
      (i32.shl (i32.load8_u (i32.const 65534)) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.const 65535)) (i32.const 8)))
               (i32.load8_u (i32.const 65536))))

  ;; The whole argument for combining rests on this one.  The base is
  ;; 0xFFFFFFFD and the lanes are at it plus 0, 1, 2 and 3 -- the program's own
  ;; 32-bit address arithmetic wraps, so the fourth is address 0 and they are
  ;; *not* four contiguous bytes, while one wide load reads four contiguous
  ;; bytes.  The two agree only because a wasm32 memory here stops one page
  ;; short of the whole index space, which puts 0xFFFFFFFD out of bounds and
  ;; makes both spellings trap.  Remove that cap in `make_memory` and this is
  ;; where it shows.  The base goes through a mutable global so the addresses
  ;; stay 32-bit adds: written as constants they are folded to four 64-bit
  ;; effective addresses, and then no wrap is left for the pass to see.
  (func (export "ldcomb_wrap") (result i32)
    (i32.or (i32.or (i32.or
      (i32.shl (i32.load8_u (i32.add (global.get $top) (i32.const 0))) (i32.const 24))
      (i32.shl (i32.load8_u (i32.add (global.get $top) (i32.const 1))) (i32.const 16)))
      (i32.shl (i32.load8_u (i32.add (global.get $top) (i32.const 2))) (i32.const 8)))
               (i32.load8_u (i32.add (global.get $top) (i32.const 3)))))

  ;; Two lanes one byte apart on paper and not in fact.  The first is at
  ;; address 0 with offset 0; the second at address 0xFFFFFFFF with offset 2,
  ;; whose effective address is 2^32 + 1 and out of bounds however large the
  ;; memory is -- a wasm offset is added in 33 bits and does not wrap.  Their
  ;; displacements are 0 and 1 all the same, so a rule that only added the two
  ;; up would combine them into a load of bytes 0 and 1 and return a number
  ;; where this has to trap.  Unlike `ldcomb_wrap`, no cap on the memory can
  ;; rescue this one: the wrap is in the middle of the group rather than off
  ;; its end, which is why the pass compares the 32-bit part separately.
  (func (export "ldcomb_mixed") (result i32)
    (i32.or
      (i32.shl (i32.load8_u offset=0 (global.get $zero)) (i32.const 8))
               (i32.load8_u offset=2 (i32.add (global.get $zero) (i32.const -1)))))

  ;; Store-to-load forwarding where the load's i32 add is the smaller
  ;; constant (test/cases/fwdwrap.wat): the store at sp + 8 and the load at
  ;; (sp - 8) + 16 are one address unless sp - 8 wraps, and at sp = 4 it
  ;; does.  With an access at sp - 8 first, that one traps; without one, or
  ;; with one at sp, the load must stay and trap instead of reading the
  ;; stored value.
  (global $four (mut i32) (i32.const 4))
  (func (export "fwd_wrap_witness") (result i32)
    (local $sp i32) (local $c i32)
    (local.set $sp (global.get $four))
    (i32.store offset=8 (local.get $sp) (i32.const 42))
    (local.set $c (i32.sub (local.get $sp) (i32.const 8)))
    (i64.store (local.get $c) (i64.const 7))
    (i32.load offset=16 (local.get $c)))
  (func (export "fwd_wrap_no_witness") (result i32)
    (local $sp i32)
    (local.set $sp (global.get $four))
    (i32.store offset=8 (local.get $sp) (i32.const 42))
    (i32.load offset=16 (i32.sub (local.get $sp) (i32.const 8))))
  (func (export "fwd_wrap_other_witness") (result i32)
    (local $sp i32)
    (local.set $sp (global.get $four))
    (i32.store offset=8 (local.get $sp) (i32.const 42))
    (i64.store (local.get $sp) (i64.const 7))
    (i32.load offset=16 (i32.sub (local.get $sp) (i32.const 8))))

  ;; A vector access is sixteen bytes wide, so it reaches past the end from
  ;; fifteen bytes short of it -- a width the bounds check has to be told
  ;; about, and one no scalar access can produce.
  (func (export "v128_load_oob") (result v128) (v128.load (i32.const 65521)))
  (func (export "v128_store_oob") (v128.store (i32.const 65521) (v128.const i32x4 1 2 3 4)))
  (func (export "v128_load_last_ok") (result v128) (v128.load (i32.const 65520)))
  ;; The narrow vector loads are the width they really read, not sixteen.
  (func (export "v128_splat_oob") (result v128) (v128.load64_splat (i32.const 65529)))
  (func (export "v128_splat_last_ok") (result v128) (v128.load64_splat (i32.const 65528)))
  (func (export "v128_lane_oob") (result v128)
    (v128.load32_lane 0 (i32.const 65533) (v128.const i32x4 0 0 0 0)))
  ;; A narrow check that dominates a vector one does not cover it: four bytes
  ;; proven says nothing about the sixteen this reads.
  (func (export "v128_bce_oob") (result v128)
    (drop (i32.load (i32.const 65521)))
    (v128.load (i32.const 65521)))

  (func (export "div0")     (result i32) (i32.div_s (i32.const 1) (i32.const 0)))
  (func (export "div0_u")   (result i32) (i32.div_u (i32.const 1) (i32.const 0)))
  (func (export "rem0")     (result i32) (i32.rem_s (i32.const 1) (i32.const 0)))
  (func (export "div_ovf")  (result i32) (i32.div_s (i32.const -2147483648) (i32.const -1)))
  ;; A divisor of -1 is a negation, which has to trap on the minimum still,
  ;; with the dividend where nothing can fold it.
  (global $min32 (mut i32) (i32.const 0x80000000))
  (global $min64 (mut i64) (i64.const 0x8000000000000000))
  (func (export "div_ovf_var") (result i32) (i32.div_s (global.get $min32) (i32.const -1)))
  (func (export "div_ovf64_var") (result i64) (i64.div_s (global.get $min64) (i64.const -1)))
  (func (export "rem_m1_min_ok") (result i32)
    (if (i32.or (i32.wrap_i64 (i64.rem_s (global.get $min64) (i64.const -1)))
                (i32.rem_s (global.get $min32) (i32.const -1)))
      (then (unreachable)))
    (i32.const 0))
  (func (export "div0_64")  (result i64) (i64.div_s (i64.const 1) (i64.const 0)))
  (func (export "div_ovf64") (result i64)
    (i64.div_s (i64.const -9223372036854775808) (i64.const -1)))

  ;; Divisors the compiler cannot see through.  The values come from a data
  ;; segment rather than a store, so nothing forwards them into a constant and
  ;; the range analysis really is the thing being tested.
  (func (export "div0_dyn") (result i32)          ;; plain unknown value, is 0
    (i32.div_u (i32.const 1) (i32.load (i32.const 8))))
  (func (export "div0_masked") (result i32)       ;; 8 & 7 is 0
    (i32.div_u (i32.const 1) (i32.and (i32.load (i32.const 0)) (i32.const 7))))
  (func (export "div0_wrap") (result i32)         ;; 0xffffffff + 1 wraps to 0
    (i32.div_u (i32.const 1) (i32.add (i32.load (i32.const 4)) (i32.const 1))))
  (func (export "rem0_masked") (result i64)       ;; 64 & 63 is 0
    (i64.rem_u (i64.const 1) (i64.and (i64.load (i32.const 16)) (i64.const 63))))
  ;; The same zero divisor, in a loop that does not change it: the division
  ;; becomes a multiply by a reciprocal made before the loop, which must not
  ;; trap there (its divisor is replaced by 1) and must still trap here.
  (func (export "div0_loop_u") (result i32)
    (local $i i32) (local $s i32) (local $d i32)
    (local.set $d (i32.load (i32.const 8)))
    (loop $l
      (i32.store (i32.const 12) (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.div_u (local.get $i) (local.get $d))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 100))))
    (local.get $s))
  ;; A division and then the remainder of the same pair, which becomes a
  ;; multiply-subtract of the quotient: the division still traps first.
  (func (export "divrem_ovf") (result i32)
    (local $n i32) (local $d i32)
    (local.set $n (i32.load (i32.const 32)))
    (local.set $n (i32.add (local.get $n) (i32.const 1)))
    (local.set $d (i32.load (i32.const 4)))
    (i32.add (i32.div_s (local.get $n) (local.get $d))
             (i32.rem_s (local.get $n) (local.get $d))))
  (func (export "rem0_loop_u") (result i32)
    (local $i i32) (local $s i32) (local $d i32)
    (local.set $d (i32.load (i32.const 8)))
    (loop $l
      (local.set $s (i32.add (local.get $s) (i32.rem_u (i32.mul (local.get $i) (i32.const 7)) (local.get $d))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 100))))
    (local.get $s))

  ;; A loop guarded by a counter kept in a mutable global -- wasm-smith's fuel.
  ;; The guard reads a global the loop decrements, so it is not invariant and
  ;; must not be hoisted in front of the loop, where it holds once and the
  ;; loop then never stops.  The decrement is in the loop, in a callee, and in
  ;; a function that tail-calls itself, which becomes a loop.
  (global $fuel (mut i32) (i32.const 0))
  (func $burn (global.set $fuel (i32.sub (global.get $fuel) (i32.const 1))))
  (func $fuel_loop (param $n i32) (result i32)
    (local $i i32)
    (global.set $fuel (i32.const 100))
    (loop $l
      (if (i32.eqz (global.get $fuel)) (then (unreachable)))
      (global.set $fuel (i32.sub (global.get $fuel) (i32.const 1)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (local.get $i))
  (func (export "fuel_loop") (result i32) (call $fuel_loop (i32.const 1000)))
  (func (export "fuel_loop_ok") (result i32) (call $fuel_loop (i32.const 100)))
  (func (export "fuel_loop_call") (result i32)
    (local $i i32)
    (global.set $fuel (i32.const 100))
    (loop $l
      (if (i32.eqz (global.get $fuel)) (then (unreachable)))
      (call $burn)
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 1000))))
    (local.get $i))
  (func $spin (param $n i32) (result i32)
    (if (i32.eqz (global.get $fuel)) (then (unreachable)))
    (global.set $fuel (i32.sub (global.get $fuel) (i32.const 1)))
    (return_call $spin (i32.add (local.get $n) (i32.const 1))))
  (func (export "fuel_tail") (result i32)
    (global.set $fuel (i32.const 100))
    (call $spin (i32.const 0)))

  ;; A bound is only carried through a left shift while nothing leaves the
  ;; top.  popcount(0x7fffffff) is 31 and 31 << 27 is 0xf8000000, so the sum
  ;; wraps to zero -- but the *bound* on a popcount is 32, and 32 << 27 is
  ;; zero in 32 bits, so a rule that shifts the bound concludes the value is
  ;; at most 0 and that the add cannot reach zero.  AArch64 `udiv` by zero
  ;; answers 0 rather than faulting, so dropping the check is silent.
  ;; ---- bounds checks removed from a *bound*, not from a constant ----
  ;;
  ;; At -O1 there are no guard pages, so a check deleted on a bound that is
  ;; not really an upper bound is a silent out-of-bounds access.  These are
  ;; the addresses whose bound lands exactly on the edge of the one page this
  ;; module declares.

  ;; 0xffff is the last byte, so a one-byte read there is the last legal
  ;; access and a four-byte read starting there is not.  A rule that adds the
  ;; access width wrongly deletes this check.
  (func (export "bce_and_edge") (result i32)
    (i32.load (i32.and (i32.load (i32.const 4)) (i32.const 0xffff))))
  ;; The same address, one byte wide: this one must still work, so a fix that
  ;; merely stops removing checks does not pass either.
  (func (export "bce_and_edge_ok") (result i32)
    (i32.load8_u (i32.and (i32.load (i32.const 4)) (i32.const 0xffff))))
  ;; A bound carried through a left shift that overflows.  popcount is at most
  ;; 32 and 32 << 27 is zero, so a rule that shifts the bound thinks this
  ;; address is 0 and drops the check; the real address is 0xf8000000.
  (func (export "bce_shl") (result i32)
    (i32.load (i32.shl (i32.popcnt (i32.load (i32.const 32))) (i32.const 27))))
  ;; A remainder bounds the address below the page, so this one is provably
  ;; in range and must not trap.
  (func (export "bce_rem_ok") (result i32)
    (i32.load (i32.rem_u (i32.load (i32.const 4)) (i32.const 1000))))

  (func (export "div0_shl") (result i32)
    (i32.div_u (i32.const 100)
      (i32.add (i32.shl (i32.popcnt (i32.load (i32.const 32))) (i32.const 27))
               (i32.const 0x08000000))))
  ;; The 64-bit form of the same shape: popcount is at most 64, 64 << 58 is
  ;; zero, and 63 << 58 is not.
  (func (export "div0_shl64") (result i64)
    (i64.div_u (i64.const 100)
      (i64.add (i64.shl (i64.popcnt (i64.load (i32.const 40))) (i64.const 58))
               (i64.const 0x0400000000000000))))
  ;; And the shape that must still fold: a shift that stays inside its width
  ;; really does bound the result, so this divisor cannot be zero and the
  ;; check must still go.  Answers 100 / 0x40000000 = 0.
  (func (export "shl_in_range_ok") (result i32)
    (i32.div_u (i32.const 100)
      (i32.add (i32.shl (i32.and (i32.load (i32.const 4)) (i32.const 3))
                        (i32.const 2))
               (i32.const 0x40000000))))

  (func (export "trunc_nan")  (result i32) (i32.trunc_f64_s (f64.const nan)))
  (func (export "trunc_big")  (result i32) (i32.trunc_f64_s (f64.const 3e9)))
  (func (export "trunc_neg")  (result i32) (i32.trunc_f64_u (f64.const -1.5)))
  (func (export "trunc_inf")  (result i64) (i64.trunc_f32_s (f32.const inf)))
  (func (export "trunc_big64") (result i64) (i64.trunc_f64_s (f64.const 1e30)))

  (func (export "ind_oob")  (result i32) (call_indirect (type $unary) (i32.const 0) (i32.const 5)))
  (func (export "ind_null") (result i32) (call_indirect (type $unary) (i32.const 0) (i32.const 1)))
  (func (export "ind_type") (result i32)
    (call_indirect (type $binary) (i32.const 0) (i32.const 0) (i32.const 0)))

  ;; The same three through a callee's parameter, at sites that pass the
  ;; index as a constant: the inliner takes a callee's indirect calls on a
  ;; parameter as direct where the constant resolves (param_indirect_calls
  ;; in src/inline.c), and none of these does, so each still traps as the
  ;; call_indirect does.  test/cases/inline_icall.wat has the ones that
  ;; return.
  (func $ind_param (param $f i32) (result i32)
    (i32.add (call_indirect (type $binary) (i32.const 1) (i32.const 2) (local.get $f))
             (call_indirect (type $binary) (i32.const 3) (i32.const 4) (local.get $f))))
  (func (export "ind_param_type") (result i32) (call $ind_param (i32.const 0)))
  (func (export "ind_param_null") (result i32) (call $ind_param (i32.const 1)))
  (func (export "ind_param_oob") (result i32) (call $ind_param (i32.const 2)))

  (func $recurse (param i32) (result i32)
    (i32.add (i32.const 1) (call $recurse (local.get 0))))
  (func (export "stack_overflow") (result i32) (call $recurse (i32.const 1)))

  ;; Hoisting out of a loop may not move a faulting instruction above anything
  ;; in the header that can stop the block first.  $boom is too big to inline
  ;; and writes nothing, so it is not a barrier for aliasing -- but it is one
  ;; for reachability, and the invariant out-of-bounds load below it must stay
  ;; where it is or this reports the wrong trap.
  (func $boom (param $n i32)
    (local $x i32)
    (local.set $x (local.get $n))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (local.set $x (i32.add (local.get $x) (i32.const 3)))
    (if (i32.gt_u (local.get $x) (i32.const 0)) (then (unreachable))))

  (func (export "hoist_past_call") (result i32)
    (local $i i32) (local $acc i32)
    (block $done (loop $again
      (call $boom (i32.const 1))
      (local.set $acc (i32.add (local.get $acc) (i32.load (i32.const 100000))))
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))

  ;; The same rule for a divide, which reports a different code than a fault.
  (func (export "hoist_past_div") (result i32)
    (local $i i32) (local $acc i32)
    (block $done (loop $again
      (local.set $acc (i32.div_u (i32.const 1) (i32.load (i32.const 8))))
      (local.set $acc (i32.add (local.get $acc) (i32.load (i32.const 100000))))
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))

  ;; A trap *guard* in the loop header, with a faulting load ahead of it.
  ;;
  ;; `pass_loop_hoist` moves an invariant guard whose taken side is a trap out to
  ;; the preheader, because it traps on the first iteration or never.  What it must
  ;; not do is move it in front of something that would have raised first.  Here
  ;; the load is out of bounds and the guard's condition is true, so the program
  ;; reports the fault; hoisting the guard reports its `unreachable` instead.
  ;;
  ;; The condition is read into a local *before* the loop, so it is invariant and
  ;; the guard is a candidate.  Written with the load inline it is not: the
  ;; chain-collector will not move a load, so the guard is never considered, and a
  ;; first attempt at this case tested nothing for that reason.
  (func (export "guard_past_fault") (result i32)
    (local $i i32) (local $acc i32) (local $c i32)
    (local.set $c (i32.load (i32.const 4)))       ;; nonzero: the guard fires
    (block $done (loop $again
      (local.set $acc (i32.load (i32.const 100000)))   ;; and this faults first
      (if (local.get $c) (then (unreachable)))
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))

  ;; Inside the second memory, past the end of the first.
  (func (export "mem1_high_ok") (result i32) (i32.load $big (i32.const 200000)))
  ;; Past the end of the second, which is the only one that can say so.
  (func (export "mem1_oob") (result i32) (i32.load $big (i32.const 400000)))
  ;; The same address in the first memory, which is a page long.
  (func (export "mem0_oob_vs_mem1") (result i32) (i32.load (i32.const 200000)))
  (func (export "mem1_store_oob") (i32.store $big (i32.const 400000) (i32.const 1)))
  (func (export "mem1_fill_oob") (memory.fill $big (i32.const 327000) (i32.const 0) (i32.const 1000)))
  (func (export "mem1_copy_oob") (memory.copy $big 0 (i32.const 0) (i32.const 60000) (i32.const 10000)))
  ;; A dynamic address, so the check cannot be folded away.
  (func (export "mem1_oob_dyn") (param) (result i32)
    (i32.load $big (i32.mul (memory.size $big) (i32.const 100000))))

  ;; Inside the 64-bit memory.
  (func (export "m64_ok") (result i32) (i32.load $w (i64.const 65532)))
  ;; Just past its one page, which a 32-bit check would also catch.
  (func (export "m64_oob_small") (result i32) (i32.load $w (i64.const 65536)))
  ;; Past 2^32: only the top half of the address says so, and only an explicit
  ;; check reads it.  Truncating to 32 bits makes every one of these read
  ;; byte 0 and return a value.
  (func (export "m64_oob_2p32") (result i32) (i32.load $w (i64.const 0x100000000)))
  (func (export "m64_oob_high") (result i32) (i32.load $w (i64.const 0x123400000000)))
  (func (export "m64_oob_top") (result i32) (i32.load $w (i64.const 0x8000000000000000)))
  (func (export "m64_oob_neg") (result i32) (i32.load $w (i64.const -1)))
  ;; The address is in range and the *offset* takes it out, wrapping the sum
  ;; back to a small number on the way.
  (func (export "m64_off_wrap") (result i32)
    (i32.load $w offset=0xfffffffffffffffc (i64.const 8)))
  (func (export "m64_store_oob") (i32.store $w (i64.const 0x100000000) (i32.const 1)))
  ;; A dynamic address, so nothing can fold the check away.
  (func (export "m64_oob_dyn") (result i32)
    (i32.load $w (i64.mul (memory.size $w) (i64.const 0x100000000))))
  ;; ---- what a clamp and a loop counter are worth to a bounds check ----
  ;;
  ;; A producer's `if (n > K) n = K;` leaves a merge whose two limbs are the
  ;; constant K and n itself, and the second one is bounded only by reading
  ;; the edge that carries it as the side on which the comparison was false.
  ;; These are the ways of reading it wrongly.  A 64-bit memory is what they
  ;; are addressed through because that is the one whose checks survive -O2.

  ;; Signed, and the value is negative: `-1 > 4` is false, so the clamp does
  ;; not fire and the address is 0xfffffffc.  Reading the relation as unsigned
  ;; would bound n at 4 and delete the check.
  (func (export "m64_clamp_signed_oob") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 4)))
    (if (i32.gt_s (local.get $n) (i32.const 4))
      (then (local.set $n (i32.const 4))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  ;; The same, written with the constant on the left, so the mirrored form of
  ;; the relation is read too.
  (func (export "m64_clamp_mirror_oob") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 4)))
    (if (i32.lt_s (i32.const 4) (local.get $n))
      (then (local.set $n (i32.const 4))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  ;; Unsigned and exact: 16384 words is 65536 bytes, the first the page does
  ;; not have.  An off-by-one in either direction shows up here.
  (func (export "m64_clamp_edge_oob") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 4)))
    (if (i32.gt_u (local.get $n) (i32.const 16384))
      (then (local.set $n (i32.const 16384))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  ;; One word lower is the last word the page holds, so this must succeed --
  ;; a rule that merely stopped deleting checks would not pass this one.
  (func (export "m64_clamp_edge_ok") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 4)))
    (if (i32.gt_u (local.get $n) (i32.const 16383))
      (then (local.set $n (i32.const 16383))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  ;; The same edge, with the limb the comparison bounds as the larger of the
  ;; two: `n > K` false gives `n <= K` and not `n < K`, and only here does the
  ;; difference decide anything -- above, the constant limb is K itself and
  ;; pins the answer whichever way the relation is read.
  (func (export "m64_clamp_else_oob") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 56)))          ;; 16384
    (if (i32.gt_u (local.get $n) (i32.const 16384))
      (then (local.set $n (i32.const 0))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  (func (export "m64_clamp_else_ok") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 60)))          ;; 16383
    (if (i32.gt_u (local.get $n) (i32.const 16383))
      (then (local.set $n (i32.const 0))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  (func (export "m64_clamp_mirror_ok") (result i32)
    (local $n i32)
    (local.set $n (i32.load (i32.const 4)))
    (if (i32.lt_u (i32.const 16383) (local.get $n))
      (then (local.set $n (i32.const 16383))))
    (i32.load $w (i64.extend_i32_u (i32.shl (local.get $n) (i32.const 2)))))
  ;; The same misreading on an i64 index, and out of range by more than a
  ;; guard region is wide.  Below 2^32 a check deleted in error still faults
  ;; into the reservation at -O2 and reads as a trap, so the case that tells a
  ;; deleted check from a kept one has to reach past it.
  (func (export "m64_clamp64_oob") (result i32)
    (local $n i64)
    (local.set $n (i64.load (i32.const 48)))
    (if (i64.gt_s (local.get $n) (i64.const 4))
      (then (local.set $n (i64.const 4))))
    (i32.load $w (local.get $n)))
  ;; Unsigned, so the clamp does fire, and the access is the last word again.
  (func (export "m64_clamp64_ok") (result i32)
    (local $n i64)
    (local.set $n (i64.load (i32.const 48)))
    (if (i64.gt_u (local.get $n) (i64.const 16383))
      (then (local.set $n (i64.const 16383))))
    (i32.load $w (i64.shl (local.get $n) (i64.const 2))))
  ;; A loop counter is bounded by its exit test *and* by what it entered with,
  ;; and here only the second one is large.  Taking the back edge alone would
  ;; say this index is under eight; it is 0xffffffff on the first iteration.
  (func (export "m64_iv_entry_oob") (result i32)
    (local $i i64) (local $acc i32)
    (local.set $i (i64.extend_i32_u (i32.load (i32.const 4))))
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (i32.load8_u $w (local.get $i))))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $i) (i64.const 8))))
    (local.get $acc))
  ;; The counter's own bound is fine and the *access* runs past it: the last
  ;; iteration reads four bytes from the last byte of the page.
  (func (export "m64_iv_span_oob") (result i32)
    (local $i i64) (local $acc i32)
    (local.set $i (i64.const 65533))
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (i32.load $w (local.get $i))))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $i) (i64.const 65536))))
    (local.get $acc))
  ;; And the counted loop that is entirely in range, which is the shape the
  ;; whole rule exists for: no check survives it and it must not trap.
  (func (export "m64_iv_ok") (result i32)
    (local $i i64) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (i32.load $w (i64.shl (local.get $i) (i64.const 2)))))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $i) (i64.const 16384))))
    (local.get $acc))
  (func (export "m64_fill_oob")
    (memory.fill $w (i64.const 0) (i32.const 0) (i64.const 0x100000000)))
  (func (export "m64_copy_oob")
    (memory.copy $w $w (i64.const 0x100000000) (i64.const 0) (i64.const 4)))
  ;; A 64-bit table index past the end, and past 2^32.
  (func (export "t64_oob") (result i32) (call_indirect $tw (type $void_i32) (i64.const 2)))
  (func (export "t64_oob_high") (result i32)
    (call_indirect $tw (type $void_i32) (i64.const 0x100000002)))
  (func (export "t64_get_oob") (result i32)
    (ref.is_null (table.get $tw (i64.const 0x100000000))))

  ;; The largest effective address a 32-bit access can form.  `addr + offset`
  ;; is computed in sixty-four bits and the validator lets both reach 2^32-1,
  ;; so it runs two bytes past eight gigabytes before the access has any width
  ;; -- and the guarded region has to be large enough that the fault still
  ;; lands inside it, or the handler does not recognise it as a trap and the
  ;; process dies instead.  A sixteen-byte access needs fourteen more.
  ;;
  ;; What these pin is the observable: the access traps, with the right code,
  ;; at every level.  They used to reach the far end of an eight-gigabyte
  ;; reservation and depend on it being sized for the whole sum; they do not
  ;; any more.  An offset this large is past WASM_GUARD_OFFSET_MAX, so the
  ;; access carries an ordinary bounds check and the trap is decided before
  ;; any address is formed -- which is what makes these deterministic rather
  ;; than a question about where the kernel placed the mapping.
  (func (export "mem_far_offset") (result i32)
    (i32.load offset=0xffffffff (i32.const 0xffffffff)))
  (func (export "mem_far_offset_v128") (result i32)
    (i32x4.extract_lane 0
      (v128.load offset=0xffffffff (i32.const 0xffffffff))))
  (func (export "mem_far_store")
    (i32.store offset=0xffffffff (i32.const 0xffffffff) (i32.const 1)))
  (func (export "mem_far_store_v128")
    (v128.store offset=0xffffffff (i32.const 0xffffffff)
      (v128.const i32x4 1 2 3 4)))

  ;; ---- the static-offset cap ----
  ;;
  ;; Guard pages answer for the address, which can be any 32-bit value, but
  ;; only for a bounded static offset on top of it: covering every offset the
  ;; validator permits would need a reservation twice the index space, so an
  ;; access whose `offset + width` exceeds WASM_GUARD_OFFSET_MAX (32 MiB)
  ;; carries an explicit bounds check instead, and the reservation is sized
  ;; for the cap rather than for the whole sum.
  ;;
  ;; What these pin is that the seam is invisible.  The same in-bounds access
  ;; answers and the same out-of-bounds one traps on either side of it -- and
  ;; `guard_far_addr` is the one that cannot work without the check at all: a
  ;; near-maximal address plus an offset past the cap lands beyond the end of
  ;; the reservation, where a fault is not recognised as a trap and kills the
  ;; process rather than reporting one.
  (func (export "guard_under_ok") (result i32)
    (i32.store $huge offset=0x1fffff0 (i32.const 0) (i32.const 0x11223344))
    (i32.load $huge offset=0x1fffff0 (i32.const 0)))
  (func (export "guard_over_ok") (result i32)
    (i32.store $huge offset=0x2000010 (i32.const 0) (i32.const 0x55667788))
    (i32.load $huge offset=0x2000010 (i32.const 0)))
  ;; The same two offsets with an address that puts them past the end.
  (func (export "guard_under_oob") (result i32)
    (i32.load $huge offset=0x1fffff0 (i32.const 0x2000000)))
  (func (export "guard_over_oob") (result i32)
    (i32.load $huge offset=0x2000010 (i32.const 0x2000000)))
  ;; Past the end of the reservation itself, which is what the check is for.
  (func (export "guard_far_addr") (result i32)
    (i32.load $huge offset=0x4000000 (i32.const 0xfffffff0)))
  (func (export "guard_far_addr_store") (result i32)
    (i32.store $huge offset=0x4000000 (i32.const 0xfffffff0) (i32.const 1))
    (i32.const 0))
  ;; The widest access, whose sixteen bytes are part of the comparison.
  (func (export "guard_far_v128") (result i32)
    (i32x4.extract_lane 0
      (v128.load $huge offset=0x4000000 (i32.const 0xfffffff0))))
  ;; And an address the compiler cannot see, so nothing folds the check away.
  (func (export "guard_far_dyn") (result i32)
    (i32.load $huge offset=0x4000000
      (i32.mul (memory.size $huge) (i32.const 0x100000))))

  ;; An exception with nowhere left to go.  It is not a wasm trap -- nothing
  ;; the module writes can catch a trap, and a `catch_all` catches every
  ;; exception -- but it leaves compiled code the same way one does, and the
  ;; harness has to be able to tell the two apart.
  (tag $exn (param i32))
  (func $raise (i32.const 5) (throw $exn))
  (func (export "exn_uncaught") (result i32) (call $raise) (i32.const 0))
  ;; Past a frame that catches something else, so the walk has to step over a
  ;; handler list rather than over a frame with none.
  (func $shielded (result i32)
    (block $h
      (try_table (catch $exn2 $h) (call $raise))
      (return (i32.const 1)))
    (i32.const 2))
  (tag $exn2)
  (func (export "exn_uncaught_past_handler") (result i32) (call $shielded))
  ;; A trap raised *inside* a protected region stays a trap: `catch_all` is
  ;; not a trap handler, and the region must not swallow it.
  (func (export "exn_trap_not_caught") (result i32)
    (block $h
      (try_table (catch_all $h) (unreachable))
      (return (i32.const 1)))
    (i32.const 2))
  (func (export "exn_trap_not_caught_deep") (result i32)
    (block $h
      (try_table (catch_all $h) (drop (call $div0)))
      (return (i32.const 1)))
    (i32.const 2))
  (func $div0 (result i32) (i32.div_s (i32.const 1) (i32.const 0)))
  ;; A null exception reference has nothing to raise.
  (func (export "exn_throw_null") (result i32)
    (throw_ref (ref.null exn)))
  ;; And one the compiler cannot see is null: a literal `ref.null` is a
  ;; constant the check can be folded against, a global or a table slot is
  ;; not, so these are the only ones that reach the check the back end emits.
  (global $nullexn (mut exnref) (ref.null exn))
  (table $xt 2 exnref)
  (func (export "exn_throw_null_global") (result i32)
    (throw_ref (global.get $nullexn)))
  (func (export "exn_throw_null_table") (result i32)
    (throw_ref (table.get $xt (i32.const 1))))

  ;; Uncaught from a long way down, so the walk runs off the top of the stack
  ;; rather than out of one frame.  What must not happen is that it stops at
  ;; some frame in the middle and resumes into it.
  (func $raise_n (param i32) (local.get 0) (throw $exn))
  (func $deep_raise (param $n i32)
    (if (i32.eqz (local.get $n)) (then (call $raise_n (i32.const 5))))
    (call $deep_raise (i32.sub (local.get $n) (i32.const 1))))
  (func (export "exn_uncaught_deep") (result i32)
    (call $deep_raise (i32.const 60)) (i32.const 0))
  ;; The same with nothing above it but an indirect call, whose site the
  ;; metadata records without knowing the callee.
  (type $nullary (func))
  (table $xf 2 funcref)
  (elem (table $xf) (i32.const 0) func $raise)
  (func (export "exn_uncaught_indirect") (result i32)
    (call_indirect $xf (type $nullary) (i32.const 0)) (i32.const 0))
  ;; A call made *just before* a `try_table` is not inside it.  Entering a
  ;; region opens no new block, so this call sits in the same block as the one
  ;; that is protected -- and a block's handler list belongs to its last
  ;; instruction alone.  Attaching it to every call in the block instead put
  ;; this raise under a region it was never in, and the wrong answer was an
  ;; ordinary number rather than a crash: 3005 instead of a walk that ends
  ;; outside the module.  Indirect, so that inlining cannot dissolve the shape
  ;; at the levels that have it.
  ;; Reached only through the table, so that neither call below is one the
  ;; inliner can dissolve: inlined, the protected call takes its handler list
  ;; with it and the shape under test is gone.
  (func $noraise (result i32) (i32.const 7))
  (elem (table $xf) (i32.const 1) func $noraise)
  (func $before_region (result i32)
    (call_indirect $xf (type $nullary) (i32.const 0))
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h)
        (call_indirect $xf (type $void_i32) (i32.const 1)))
      (return))
    (i32.add (i32.const 3000)))
  (func (export "exn_uncaught_before_region") (result i32) (call $before_region))
  ;; And the same shape with a *direct* call in front of the region, which is
  ;; the one the inliner also has to keep straight: inlining the unprotected
  ;; call must leave the handler list on the protected one behind it, where it
  ;; is still the block's last instruction.
  (func $before_region_direct (result i32)
    (call $raise)
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h)
        (call_indirect $xf (type $void_i32) (i32.const 1)))
      (return))
    (i32.add (i32.const 4000)))
  (func (export "exn_uncaught_before_region_direct") (result i32)
    (call $before_region_direct))
  ;; Raised from inside a handler: that code is no longer in the region that
  ;; reached it, so its own clause must not take it a second time.
  (func $raise2 (throw $exn2))
  (func (export "exn_uncaught_from_handler") (result i32)
    (block $h
      (try_table (catch $exn2 $h) (call $raise2))
      (return (i32.const 1)))
    (call $raise)
    (i32.const 2))
  ;; An object thrown once the frame that caught it has returned.  Nothing is
  ;; left to catch it, and the object still has to be readable.
  (func $catch_one (result exnref)
    (block $h (result exnref)
      (try_table (result exnref) (catch_all_ref $h) (call $raise) (unreachable))))
  (func (export "exn_stale_rethrow") (result i32)
    (throw_ref (call $catch_one)))
  ;; A tail call out of a protected region leaves the frame the region belongs
  ;; to, so the region no longer applies to what the callee raises.
  (func $tail_thrower (result i32) (call $raise) (i32.const 0))
  (func $tail_out (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h) (return_call $tail_thrower))
      (return)))
  (func (export "exn_tail_out_uncaught") (result i32) (call $tail_out))
  (func (export "exn_uncaught_in_loop") (result i32) (local $i i32)
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.eq (local.get $i) (i32.const 3)) (then (call $raise)))
      (br_if $l (i32.lt_s (local.get $i) (i32.const 10))))
    (i32.const 0))

  ;; Every kind of trap, raised inside a `catch_all` region.  A trap is not an
  ;; exception: it does not go through the unwinder at all, and a region that
  ;; swallowed one would turn a program that stops into a program that
  ;; returns.  The memory one matters most at -O2, where the check is a guard
  ;; page and the trap arrives as a signal rather than as a branch.
  (func (export "exn_trap_oob_in_region") (result i32)
    (block $h
      (try_table (catch_all $h) (drop (i32.load (i32.const 0x10000))))
      (return (i32.const 1)))
    (i32.const 2))
  (func (export "exn_trap_ind_type_in_region") (result i32)
    (block $h
      (try_table (catch_all $h)
        (drop (call_indirect (type $binary) (i32.const 0) (i32.const 0)
                             (i32.const 0))))
      (return (i32.const 1)))
    (i32.const 2))
  (func (export "exn_trap_ind_null_in_region") (result i32)
    (block $h
      (try_table (catch_all $h)
        (drop (call_indirect (type $unary) (i32.const 0) (i32.const 1))))
      (return (i32.const 1)))
    (i32.const 2))
  (func (export "exn_trap_conv_in_region") (result i32)
    (block $h
      (try_table (catch_all $h)
        (drop (i32.trunc_f32_s (f32.load (i32.const 4)))))
      (return (i32.const 1)))
    (i32.const 2))
  (func (export "exn_trap_overflow_in_region") (result i32)
    (block $h
      (try_table (catch_all $h) (drop (call $recurse (i32.const 1))))
      (return (i32.const 1)))
    (i32.const 2))
  ;; And a trap *after* a handler has run, which is the only check that the
  ;; frame a resume rebuilt is one ordinary code can go on using.  The divisor
  ;; is read from memory so nothing can fold it.
  (func (export "exn_trap_after_catch") (result i32)
    (block $h
      (try_table (catch $exn2 $h) (call $raise2))
      (return (i32.const 1)))
    (i32.div_s (i32.const 1) (i32.load (i32.const 8))))

  ;; Three regions, one inside another, none of which names this tag.  The
  ;; walk has to reject all three within one frame and leave it.
  (func (export "exn_uncaught_nested_regions") (result i32)
    (block $h1
      (try_table (catch $exn2 $h1)
        (block $h2
          (try_table (catch $exn2 $h2)
            (block $h3
              (try_table (catch $exn2 $h3) (call $raise))
              (return (i32.const 1))))
          (return (i32.const 2))))
      (return (i32.const 3)))
    (i32.const 4))
  ;; The wrong tag in the *same* frame as the throw, rather than in a callee.
  (func (export "exn_uncaught_other_tag") (result i32)
    (block $h
      (try_table (catch $exn2 $h) (call $raise))
      (return (i32.const 1)))
    (i32.const 2))
  ;; Forty frames each holding a handler list, none of them matching.
  (func $shielded_deep (param $n i32)
    (block $h
      (try_table (catch $exn2 $h)
        (if (i32.eqz (local.get $n)) (then (call $raise)))
        (call $shielded_deep (i32.sub (local.get $n) (i32.const 1))))
      (return))
    (unreachable))
  (func (export "exn_uncaught_deep_pads") (result i32)
    (call $shielded_deep (i32.const 40)) (i32.const 0))
  ;; A chain of tail calls ending in a throw: none of those frames is there
  ;; to be walked, and the one that is belongs to the caller of all of them.
  (func $tc3 (result i32) (call $raise) (i32.const 0))
  (func $tc2 (result i32) (return_call $tc3))
  (func $tc1 (result i32) (return_call $tc2))
  (func (export "exn_uncaught_tail_chain") (result i32) (return_call $tc1))
  ;; A null the compiler cannot fold: `select` picks it at run time.
  (func (export "exn_throw_null_select") (result i32)
    (throw_ref (select (result exnref)
                       (ref.null exn) (call $catch_one) (i32.const 1))))
  ;; A trap raised by the handler itself, rather than after it returned.
  (func (export "exn_trap_in_handler") (result i32)
    (block $h
      (try_table (catch $exn2 $h) (call $raise2))
      (return (i32.const 1)))
    (i32.load (i32.const 0x10000)))

  ;; A `table.get` whose result nobody wants still owes its trap.  It is the
  ;; same instruction as a load in that respect, and it was not treated as
  ;; one: at -O2 this returned 1.
  (table $dead 2 funcref)
  (func (export "dead_table_get") (result i32)
    (drop (table.get $dead (i32.const 99)))
    (i32.const 1))

  ;; And the ones that must *not* trap, so the ones above are not passing
  ;; because everything here does.  Each checks its own answer and executes
  ;; `unreachable` if it is wrong, this harness reading an exit status rather
  ;; than a value.
  ;;
  ;; `tail_into_catcher` is here rather than in the differential cases because
  ;; `wasm-interp` 1.0.41 aborts on a `return_call` into a function that
  ;; catches, so there is no reference to compare against.
  (func $tc_catcher (param $n i32) (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h) (call $raise_n (local.get $n))
        (i32.const -1))
      (return))
    (i32.add (i32.const 2000)))
  (func (export "tail_into_catcher_ok") (result i32)
    (if (i32.ne (call $return_call_helper) (i32.const 2013)) (then (unreachable)))
    (i32.const 0))
  (func $return_call_helper (result i32) (return_call $tc_catcher (i32.const 13)))
  (func (export "exn_caught_past_pads_ok") (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h)
        (call $shielded_deep (i32.const 40)) (i32.const -1))
      (return))
    (if (i32.ne (i32.const 5)) (then (unreachable)))
    (i32.const 0))
  (func (export "exn_caught_deep_ok") (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $exn $h)
        (call $deep_raise (i32.const 60)) (i32.const -1))
      (return))
    (if (i32.ne (i32.const 5)) (then (unreachable)))
    (i32.const 0))
)
