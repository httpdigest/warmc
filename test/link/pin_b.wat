;; Calls from one instance into another's code, with x19 and x20 pinned to
;; the instance and memory 0's base (isel.c): each crossing passes the
;; callee's entry check on to warmc_xenter, which copies the stack arguments
;; down and puts the caller's two back when the callee returns.  A and B each
;; have a memory and a global -- B reads 20 and 2000 where A reads 10 and
;; 1000 -- so a callee that ran with the caller's registers, or a caller left
;; with the callee's, gives the wrong sum.
(module
  (type $wide (func (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (type $cb (func (param i32) (result i32)))
  (import "A" "wide" (func $wide (type $wide)))
  (import "A" "fwide" (func $fwide (param f64 f64 f64 f64 f64 f64 f64 f64 f64 f64 i32 i32 i32 i32 i32 i32 i32 i32 i32) (result f64)))
  (import "A" "multi" (func $multi (param i32) (result i32 i64 f64 i32 i32 i32 i32 i32 i32 i32)))
  (import "A" "raise" (func $raise (param i32)))
  (import "A" "bounce" (func $bounce (param (ref $cb) i32) (result i32)))
  (import "A" "deep" (func $deep (param i32) (result i32)))
  (import "A" "t" (tag $t (param i32)))
  (import "A" "gref" (global $gref (ref $cb)))
  (import "A" "tref" (table $tref 1 1 (ref $cb)))
  (import "A" "give" (func $give (result (ref $cb))))
  (import "A" "tail_more" (func $tail_more (result i32)))
  (memory 1)
  (data (i32.const 0) "\14")
  (global $g (mut i32) (i32.const 2000))
  ;; A's function in B's own table: not one of B's, so no code table, and
  ;; the call reads the instance from the reference
  (table $tab 1 funcref)
  (elem (table $tab) (i32.const 0) func $wide)
  (elem declare func $wide $cb $cbt $cbm)

  ;; what B adds after each crossing: right only with its own x19 and x20
  (func $mine (result i32)
    (i32.add (i32.load8_u (i32.const 0)) (global.get $g)))

  (func (export "call_wide") (result i32)
    (i32.add (call $wide (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10)) (call $mine)))
  (func (export "ind_wide") (result i32)
    (i32.add (call_indirect $tab (type $wide) (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10) (i32.const 0)) (call $mine)))
  (func (export "ref_wide") (result i32)
    (i32.add (call_ref $wide (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10) (ref.func $wide)) (call $mine)))

  ;; The same by tail call: B's frame is gone, and A's result comes back
  ;; through warmc_xenter's frame to B's caller with B's registers.
  (func (export "tail_wide") (result i32)
    (return_call $wide (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10)))
  (func (export "tail_ind_wide") (result i32)
    (return_call_indirect $tab (type $wide) (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10) (i32.const 0)))
  (func (export "tail_ref_wide") (result i32)
    (return_call_ref $wide (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9) (i32.const 10) (ref.func $wide)))

  (func (export "call_fwide") (result i32)
    (i32.add (i32.trunc_f64_s (call $fwide (f64.const 1) (f64.const 2) (f64.const 3) (f64.const 4) (f64.const 5) (f64.const 6) (f64.const 7) (f64.const 8) (f64.const 9) (f64.const 10) (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8) (i32.const 9))) (call $mine)))

  (func (export "call_multi") (result i32)
    (local $s i32)
    (call $multi (i32.const 5))
    i32.add i32.add i32.add i32.add i32.add i32.add
    local.set $s
    i32.trunc_f64_s
    local.get $s
    i32.add
    local.set $s
    i32.wrap_i64
    local.get $s
    i32.add
    i32.add
    (call $mine)
    i32.add)

  ;; An exception A raises, caught here: the landing pad reads B's memory.
  (func (export "catch_mem") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $t $h) (call $raise (local.get 0)))
      (return (i32.const -1)))
    (call $mine)
    i32.add)

  ;; Down and up between the two instances, n times each way.
  (func $cb (type $cb)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.load8_u (i32.const 0)))
      (else (i32.add (call $bounce (ref.func $cb) (i32.sub (local.get 0) (i32.const 1)))
                     (call $mine)))))
  (func (export "nested") (param i32) (result i32)
    (call $bounce (ref.func $cb) (local.get 0)))

  ;; The same with A raising at the bottom: the unwinder walks a
  ;; warmc_xenter frame at every crossing, back to B's handler on top.
  (func $cbt (type $cb)
    (if (i32.eqz (local.get 0)) (then (call $raise (i32.const 100))))
    (i32.add (call $bounce (ref.func $cbt) (i32.sub (local.get 0) (i32.const 1)))
             (call $mine)))
  (func (export "nested_throw") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $t $h) (drop (call $bounce (ref.func $cbt) (local.get 0))))
      (return (i32.const -1)))
    (call $mine)
    i32.add)

  ;; And caught partway, in a frame of B's between two of A's, after which
  ;; the frames above it carry on.
  (func $cbm (type $cb)
    (if (i32.eqz (local.get 0)) (then (call $raise (i32.const 100))))
    (if (i32.eq (local.get 0) (i32.const 3))
      (then
        (block $h (result i32)
          (try_table (catch $t $h)
            (drop (call $bounce (ref.func $cbm) (i32.sub (local.get 0) (i32.const 1)))))
          (return (i32.const -1)))
        (return (i32.add (call $mine)))))
    (i32.add (call $bounce (ref.func $cbm) (i32.sub (local.get 0) (i32.const 1)))
             (call $mine)))
  (func (export "nested_mid") (param i32) (result i32)
    (call $bounce (ref.func $cbm) (local.get 0)))

  (func (export "by_global") (result i32)
    (i32.add (call_ref $cb (i32.const 1) (global.get $gref)) (call $mine)))
  (func (export "by_table") (result i32)
    (i32.add (call_indirect $tref (type $cb) (i32.const 2) (i32.const 0)) (call $mine)))
  (func (export "given") (result i32)
    (i32.add (call_ref $cb (i32.const 3) (call $give)) (call $mine)))
  (func (export "call_tail_more") (result i32)
    (i32.add (call $tail_more) (call $mine)))

  (func (export "call_deep") (result i32)
    (i32.add (call $deep (i32.const 1000000)) (call $mine)))
  (func (export "tail_deep") (result i32)
    (return_call $deep (i32.const 1000000)))
  (func (export "tail_bounce") (result i32)
    (return_call $bounce (ref.func $cb) (i32.const 3)))
)
