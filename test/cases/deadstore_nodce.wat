;; A store left dead by unrolling, through an address with its memory base
;; folded in, compiled with -fno-dce (run_all.sh's cases-no-dce row).
;;
;; addr_offsets gives p + 4 a base of its own when every user is a load or a
;; store at displacement 0, and instruction selection then fuses that pointer
;; into each of them, checking that nothing else reads it.  Unrolling makes
;; one copy's second store dead under the next copy's first, with nothing
;; reading p between them, and memopt turns it into a nop.  A nop keeps its
;; operands, and only dce takes it out of its block; without dce the check
;; met it as a user of the pointer and stopped the compile ("mem_base pointer
;; used outside an access").  The load through q may alias p, which keeps
;; the stores of one copy alive until the loop is unrolled; here it does, so
;; the sum reads each value the first store left.
(module
  (memory 1)
  (global $g (mut i32) (i32.const 1024))
  (global $gq (mut i32) (i32.const 1028))

  (func (export "overwritten") (result i32)
    (local $p i32) (local $q i32) (local $i i32) (local $s i32)
    (local.set $p (global.get $g))
    (local.set $q (global.get $gq))
    (loop $l
      (i32.store offset=4 (local.get $p) (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load (local.get $q))))
      (i32.store offset=4 (local.get $p) (i32.mul (local.get $i) (i32.const 3)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    (i32.add (local.get $s) (i32.load offset=4 (local.get $p)))))
