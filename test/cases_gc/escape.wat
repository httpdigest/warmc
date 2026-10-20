;; Escape analysis and scalar replacement (pass_escape in src/opt.c), in the
;; shapes it replaces and the ones it has to refuse.
;;
;; Every export checks its own answer and traps when it is wrong, as in
;; gc.wat.  The `sr_` ones are also read by test/run_gc.sh, which compiles
;; this module with WARMC_ESCAPE_DEBUG and fails if any allocation in an
;; `sr_` function is left standing -- an answer that is right because the
;; object was kept is not what those cases are for.  The `keep_` ones are the
;; refusals: aliasing, escapes and landing pads, where replacing the object
;; would change the answer, and the answer is what they check.
;;
;; Assembled with wasm-tools, not wat2wasm -- see test/run_gc.sh.

(module
  (type $box (struct (field $v (mut i64))))
  (type $pt (struct (field $x (mut i64)) (field $y (mut i64))))
  (type $base (sub (struct (field $a (mut i32)))))
  (type $derived (sub $base (struct (field $a (mut i32)) (field $b (mut i32)))))
  (type $other (struct (field $a (mut i32))))
  (type $packed (struct (field $b (mut i8)) (field $h (mut i16))))
  (type $vec (struct (field $v (mut v128))))
  (type $holder (struct (field $r (mut anyref))))
  (type $ai64 (array (mut i64)))
  (type $af64 (array (mut f64)))
  (type $ai8 (array (mut i8)))
  (type $av (array (mut v128)))
  (type $take_t (func (param (ref $box)) (result i64)))

  (global $sink (mut anyref) (ref.null any))
  (table $fns 1 funcref)
  (elem (table $fns) (i32.const 0) func $take)
  (tag $boom)
  (tag $report (param i64))

  (func $want (param $got i64) (param $exp i64)
    (if (i64.ne (local.get $got) (local.get $exp)) (then unreachable)))

  ;; reached through call_indirect, so the inliner cannot bring the box home
  (func $take (type $take_t) (param $b (ref $box)) (result i64)
    (struct.get $box $v (local.get $b)))

  (func $thrower (param $x i32)
    (if (local.get $x) (then (throw $boom))))

  ;; ---- replaced ----------------------------------------------------------

  ;; A counter box updated round a loop: what a Kotlin `var` captured by an
  ;; inlined lambda becomes.  Its field is a loop-carried value.
  (func (export "sr_box_loop") (param $n i32) (param $exp i64)
    (local $b (ref $box)) (local $i i32)
    (local.set $b (struct.new $box (i64.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (struct.set $box $v (local.get $b)
          (i64.add (struct.get $box $v (local.get $b)) (i64.extend_i32_s (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (struct.get $box $v (local.get $b)) (local.get $exp)))

  ;; The same box, written on one arm or the other of a branch in the loop.
  (func (export "sr_box_cond") (param $n i32) (param $exp i64)
    (local $b (ref $box)) (local $i i32)
    (local.set $b (struct.new $box (i64.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (struct.set $box $v (local.get $b)
                  (i64.add (struct.get $box $v (local.get $b)) (i64.extend_i32_s (local.get $i)))))
          (else (struct.set $box $v (local.get $b)
                  (i64.sub (struct.get $box $v (local.get $b)) (i64.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (struct.get $box $v (local.get $b)) (local.get $exp)))

  ;; Same box carried through two nested loops: one origin, so an alias.
  (func (export "sr_nested") (param $n i32) (param $exp i64)
    (local $b (ref $box)) (local $i i32) (local $j i32)
    (local.set $b (struct.new $box (i64.const 0)))
    (block $outer_done
      (loop $outer
        (br_if $outer_done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $j (i32.const 0))
        (block $inner_done
          (loop $inner
            (br_if $inner_done (i32.ge_s (local.get $j) (local.get $n)))
            (struct.set $box $v (local.get $b)
              (i64.add (struct.get $box $v (local.get $b)) (i64.const 1)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $inner)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (call $want (struct.get $box $v (local.get $b)) (local.get $exp)))

  ;; Fields written on either arm, read after the merge.
  (func (export "sr_branches") (param $c i32) (param $exp i64)
    (local $p (ref $pt))
    (local.set $p (struct.new $pt (i64.const 1) (i64.const 2)))
    (if (local.get $c)
      (then (struct.set $pt $x (local.get $p) (i64.const 10)))
      (else (struct.set $pt $y (local.get $p) (i64.const 20))))
    (call $want (i64.add (i64.mul (struct.get $pt $x (local.get $p)) (i64.const 100))
                         (struct.get $pt $y (local.get $p)))
                (local.get $exp)))

  ;; A merge of two different fresh objects: `c ? new P(3, 4) : new P(5, 6)`.
  (func (export "sr_merge") (param $c i32) (param $exp i64)
    (local $p (ref $pt))
    (local.set $p (if (result (ref $pt)) (local.get $c)
      (then (struct.new $pt (i64.const 3) (i64.const 4)))
      (else (struct.new $pt (i64.const 5) (i64.const 6)))))
    (call $want (i64.add (i64.mul (struct.get $pt $x (local.get $p)) (i64.const 10))
                         (struct.get $pt $y (local.get $p)))
                (local.get $exp)))

  ;; A loop variable given a fresh object every trip -- an immutable value
  ;; type updated in a loop.  Fibonacci, so every field matters.
  (func (export "sr_loop_fresh") (param $n i32) (param $exp i64)
    (local $p (ref $pt)) (local $i i32)
    (local.set $p (struct.new $pt (i64.const 0) (i64.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $p (struct.new $pt
          (struct.get $pt $y (local.get $p))
          (i64.add (struct.get $pt $x (local.get $p)) (struct.get $pt $y (local.get $p)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (struct.get $pt $x (local.get $p)) (local.get $exp)))

  ;; Two objects swapped every trip: two merges in one block, each taking
  ;; the other.
  (func (export "sr_swap") (param $n i32) (param $exp i64)
    (local $a (ref $box)) (local $b (ref $box)) (local $t (ref $box)) (local $i i32)
    (local.set $a (struct.new $box (i64.const 1)))
    (local.set $b (struct.new $box (i64.const 2)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $t (local.get $a))
        (local.set $a (local.get $b))
        (local.set $b (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (i64.add (i64.mul (struct.get $box $v (local.get $a)) (i64.const 10))
                         (struct.get $box $v (local.get $b)))
                (local.get $exp)))

  ;; Casts, tests and null checks on an object whose exact type is known:
  ;; every one decided at compile time.
  (func (export "sr_cast") (param $k i32) (param $exp i64)
    (local $d (ref $derived)) (local $b (ref $base)) (local $any anyref) (local $r i64)
    (local.set $d (struct.new $derived (local.get $k) (i32.const 7)))
    (local.set $b (local.get $d))
    (local.set $any (local.get $d))
    (local.set $r (i64.extend_i32_s
      (struct.get $derived $b (ref.cast (ref $derived) (local.get $any)))))
    (local.set $r (i64.add (local.get $r)
      (i64.extend_i32_s (struct.get $base $a (local.get $b)))))
    (if (i32.eqz (ref.test (ref $base) (local.get $any))) (then unreachable))
    (if (ref.test (ref $other) (local.get $any)) (then unreachable))
    (if (ref.test (ref i31) (local.get $any)) (then unreachable))
    (if (i32.eqz (ref.test (ref struct) (local.get $any))) (then unreachable))
    (if (ref.test (ref array) (local.get $any)) (then unreachable))
    (if (ref.is_null (local.get $any)) (then unreachable))
    (if (i32.eqz (ref.eq (local.get $d) (local.get $b))) (then unreachable))
    (drop (block $nonnull (result (ref any))
      (br_on_non_null $nonnull (local.get $any))
      (unreachable)))
    (drop (ref.as_non_null (local.get $any)))
    (call $want (local.get $r) (local.get $exp)))

  ;; ref.eq between two fresh objects, and each with itself.
  (func (export "sr_eq") (param $exp i64)
    (local $a (ref $box)) (local $b (ref $box))
    (local.set $a (struct.new $box (i64.const 1)))
    (local.set $b (struct.new $box (i64.const 2)))
    (call $want (i64.extend_i32_u (i32.add
                  (i32.mul (ref.eq (local.get $a) (local.get $b)) (i32.const 10))
                  (i32.add (ref.eq (local.get $a) (local.get $a))
                           (ref.eq (local.get $b) (local.get $b)))))
                (local.get $exp)))

  ;; array.new_fixed: constant indices and array.len.
  (func (export "sr_array_fixed") (param $k i64) (param $exp i64)
    (local $a (ref $ai64))
    (local.set $a (array.new_fixed $ai64 4 (i64.const 1) (i64.const 2) (i64.const 3) (local.get $k)))
    (array.set $ai64 (local.get $a) (i32.const 1)
      (i64.mul (array.get $ai64 (local.get $a) (i32.const 1)) (i64.const 10)))
    (call $want (i64.add (i64.add (i64.add (array.get $ai64 (local.get $a) (i32.const 0))
                                           (array.get $ai64 (local.get $a) (i32.const 1)))
                                  (i64.add (array.get $ai64 (local.get $a) (i32.const 2))
                                           (array.get $ai64 (local.get $a) (i32.const 3))))
                         (i64.mul (i64.extend_i32_u (array.len (local.get $a))) (i64.const 1000)))
                (local.get $exp)))

  ;; array.new_default, written on one arm of a branch.
  (func (export "sr_array_default") (param $c i32) (param $exp i64)
    (local $a (ref $ai64))
    (local.set $a (array.new_default $ai64 (i32.const 3)))
    (if (local.get $c)
      (then (array.set $ai64 (local.get $a) (i32.const 0) (i64.const 5)))
      (else (array.set $ai64 (local.get $a) (i32.const 2) (i64.const 7))))
    (call $want (i64.add (i64.add (i64.mul (array.get $ai64 (local.get $a) (i32.const 0)) (i64.const 100))
                                  (i64.mul (array.get $ai64 (local.get $a) (i32.const 1)) (i64.const 10)))
                         (array.get $ai64 (local.get $a) (i32.const 2)))
                (local.get $exp)))

  ;; array.new with a float fill value, one the caller passes in, so ir_build's
  ;; reinterpret into the runtime's integer word has to be undone.
  (func (export "sr_array_fill") (param $x f64) (param $exp i64)
    (local $a (ref $af64))
    (local.set $a (array.new $af64 (local.get $x) (i32.const 5)))
    (array.set $af64 (local.get $a) (i32.const 4) (f64.const 2.5))
    (call $want (i64.trunc_f64_s (f64.mul (f64.const 2)
                  (f64.add (f64.add (f64.add (array.get $af64 (local.get $a) (i32.const 0))
                                             (array.get $af64 (local.get $a) (i32.const 1)))
                                    (f64.add (array.get $af64 (local.get $a) (i32.const 2))
                                             (array.get $af64 (local.get $a) (i32.const 3))))
                           (array.get $af64 (local.get $a) (i32.const 4)))))
                (local.get $exp)))

  ;; A v128 fill and a v128 field, default and written.
  (func (export "sr_vectors") (param $exp i64)
    (local $a (ref $av)) (local $s (ref $vec)) (local $z (ref $vec))
    (local.set $a (array.new $av (v128.const i64x2 3 4) (i32.const 2)))
    (local.set $s (struct.new_default $vec))
    (local.set $z (struct.new_default $vec))
    (struct.set $vec $v (local.get $s) (v128.const i32x4 10 20 30 40))
    (call $want (i64.add (i64.add
                  (i64x2.extract_lane 1 (array.get $av (local.get $a) (i32.const 1)))
                  (i64.extend_i32_u (i32x4.extract_lane 2 (struct.get $vec $v (local.get $s)))))
                  (i64x2.extract_lane 0 (struct.get $vec $v (local.get $z))))
                (local.get $exp)))

  ;; Packed fields keep what a store truncated them to, signed or not on the
  ;; way out.
  (func (export "sr_packed") (param $exp i64)
    (local $p (ref $packed)) (local $r i64)
    (local.set $p (struct.new $packed (i32.const 300) (i32.const 70000)))
    (call $want (i64.extend_i32_s (struct.get_s $packed $b (local.get $p))) (i64.const 44))
    (call $want (i64.extend_i32_s (struct.get_u $packed $h (local.get $p))) (i64.const 4464))
    (struct.set $packed $b (local.get $p) (i32.const 200))
    (struct.set $packed $h (local.get $p) (i32.const 40000))
    (local.set $r (i64.extend_i32_s (struct.get_s $packed $b (local.get $p))))
    (local.set $r (i64.add (i64.mul (local.get $r) (i64.const 1000))
                           (i64.extend_i32_s (struct.get_u $packed $b (local.get $p)))))
    (local.set $r (i64.add (i64.mul (local.get $r) (i64.const 100000))
                           (i64.extend_i32_s (struct.get_s $packed $h (local.get $p)))))
    (call $want (local.get $r) (local.get $exp))
    (call $want (i64.extend_i32_s (struct.get_u $packed $h (local.get $p))) (i64.const 40000)))

  (func (export "sr_packed_array") (param $exp i64)
    (local $a (ref $ai8))
    (local.set $a (array.new_fixed $ai8 2 (i32.const 255) (i32.const 128)))
    (call $want (i64.add (i64.add
                  (i64.mul (i64.extend_i32_s (array.get_s $ai8 (local.get $a) (i32.const 0))) (i64.const 1000))
                  (i64.mul (i64.extend_i32_s (array.get_u $ai8 (local.get $a) (i32.const 0))) (i64.const 10)))
                  (i64.extend_i32_s (array.get_s $ai8 (local.get $a) (i32.const 1))))
                (local.get $exp)))

  ;; ---- freed by inlining (inline.c's escape evidence) ------------------
  ;;
  ;; The object leaves only through a call, into a callee that keeps it to
  ;; itself.  The inliner would refuse both callees on its own terms -- one
  ;; makes calls of its own, the other is reached from a cold block and is
  ;; not tiny -- and takes them because the inline frees the allocation.

  (func $ei_sq (param $v i64) (result i64) (i64.mul (local.get $v) (local.get $v)))

  (func $ei_dist2 (param $p (ref $pt)) (result i64)
    (i64.add (call $ei_sq (struct.get $pt $x (local.get $p)))
             (call $ei_sq (struct.get $pt $y (local.get $p)))))

  (func (export "sr_helper_loop") (param $n i32) (param $exp i64)
    (local $i i32) (local $s i64)
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i64.add (local.get $s)
          (call $ei_dist2 (struct.new $pt (i64.extend_i32_s (local.get $i))
                                          (i64.extend_i32_s (i32.add (local.get $i) (i32.const 1)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (local.get $s) (local.get $exp)))

  ;; a failure report with enough in it not to count as tiny
  (func $ei_fail (param $p (ref $pt))
    (local $a i64) (local $b i64)
    (local.set $a (i64.mul (struct.get $pt $x (local.get $p)) (i64.const 31)))
    (local.set $b (i64.xor (struct.get $pt $y (local.get $p)) (i64.const 0x5bd1e995)))
    (local.set $a (i64.add (i64.rotl (local.get $a) (i64.const 13)) (local.get $b)))
    (local.set $b (i64.mul (i64.shr_u (local.get $b) (i64.const 7)) (local.get $a)))
    (throw $report (i64.add (local.get $a) (local.get $b))))

  (func (export "sr_cold_report") (param $n i32) (param $exp i64)
    (local $i i32) (local $s i64) (local $p (ref $pt))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $p (struct.new $pt (i64.extend_i32_s (local.get $i))
                                      (i64.mul (i64.extend_i32_s (local.get $i)) (i64.const 3))))
        (if (i64.gt_u (struct.get $pt $x (local.get $p)) (i64.const 0xffffffffff))
          (then (call $ei_fail (local.get $p)) (unreachable)))
        (local.set $s (i64.add (local.get $s)
          (i64.add (struct.get $pt $x (local.get $p)) (struct.get $pt $y (local.get $p)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $want (local.get $s) (local.get $exp)))

  ;; ---- refused, and still right -----------------------------------------

  ;; A box stored in a holder's field.  The holder goes first, and with it
  ;; the store -- after which the box is local too, and goes the next round.
  (func (export "sr_ref_field") (param $exp i64)
    (local $h (ref $holder))
    (local.set $h (struct.new $holder (struct.new $box (i64.const 9))))
    (call $want (struct.get $box $v (ref.cast (ref $box) (struct.get $holder $r (local.get $h))))
                (local.get $exp)))

  ;; Passed to a function the inliner cannot see: the table slot is the
  ;; caller's, so devirtualisation has nothing to go on.
  (func (export "keep_call") (param $slot i32) (param $exp i64)
    (local $b (ref $box))
    (local.set $b (struct.new $box (i64.const 4)))
    (struct.set $box $v (local.get $b) (i64.const 6))
    (call $want (call_indirect $fns (type $take_t) (local.get $b) (local.get $slot))
                (local.get $exp)))

  ;; Stored in a global, then written: the write has to be seen through it.
  (func (export "keep_global") (param $exp i64)
    (local $b (ref $box))
    (local.set $b (struct.new $box (i64.const 1)))
    (global.set $sink (local.get $b))
    (struct.set $box $v (local.get $b) (i64.const 8))
    (call $want (struct.get $box $v (ref.cast (ref $box) (global.get $sink))) (local.get $exp)))

  ;; select is a merge this does not follow.
  (func (export "keep_select") (param $c i32) (param $exp i64)
    (local $p (ref $box))
    (local.set $p (select (result (ref $box)) (struct.new $box (i64.const 11))
                          (struct.new $box (i64.const 22)) (local.get $c)))
    (call $want (struct.get $box $v (local.get $p)) (local.get $exp)))

  ;; A constant index out of range traps, so the array has to stay.
  (func (export "keep_oob") (param $exp i64)
    (local $a (ref $ai64))
    (local.set $a (array.new_fixed $ai64 2 (i64.const 1) (i64.const 2)))
    (call $want (array.get $ai64 (local.get $a) (i32.const 5)) (local.get $exp)))

  ;; Live into a catch: an exception edge carries no fields.
  (func (export "keep_eh") (param $c i32) (param $exp i64)
    (local $b (ref $box))
    (local.set $b (struct.new $box (i64.const 1)))
    (block $caught
      (try_table (catch $boom $caught)
        (struct.set $box $v (local.get $b) (i64.const 2))
        (call $thrower (local.get $c))
        (struct.set $box $v (local.get $b) (i64.const 3))))
    (call $want (struct.get $box $v (local.get $b)) (local.get $exp)))

  ;; A fresh object used again after flowing into a merge: the merge may be
  ;; that object, so they cannot have fields of their own.
  (func (export "keep_reused") (param $c i32) (param $exp i64)
    (local $a (ref $box)) (local $p (ref $box))
    (local.set $a (struct.new $box (i64.const 1)))
    (local.set $p (if (result (ref $box)) (local.get $c)
      (then (local.get $a))
      (else (struct.new $box (i64.const 2)))))
    (struct.set $box $v (local.get $a) (i64.const 5))
    (call $want (struct.get $box $v (local.get $p)) (local.get $exp)))

  ;; One object handed to two merges on the same edge: afterwards the two
  ;; are the same object, and a write through one shows through the other.
  (func (export "keep_shared_edge") (param $n i32) (param $exp i64)
    (local $a (ref $box)) (local $b (ref $box)) (local $x (ref $box)) (local $i i32)
    (local.set $a (struct.new $box (i64.const 1)))
    (local.set $b (struct.new $box (i64.const 2)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $x (struct.new $box (i64.extend_i32_s (local.get $i))))
        (local.set $a (local.get $x))
        (local.set $b (local.get $x))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.set $box $v (local.get $a) (i64.const 100))
    (call $want (struct.get $box $v (local.get $b)) (local.get $exp)))
)
