;; LICM (opt.c) takes a load its loop does not change to the preheader even
;; past a store that stays, when the two cannot alias: on its first trip
;; the loop reaches the load whatever happens.  Where the load can trap,
;; that trap then comes before the store.  f stores i + 7 at p and sums two
;; words from 8 past p; for p = 65532 the store lands and the load traps,
;; so 65532 holds 7.  The store is a byte's: one wider has its own check on
;; AArch64 with -fexact-trap, which the load does not go above; on x64 none.
;; run: f:65532,1 r => trap 7
;; run: f:16,3 q => 0 9
(module
  (memory (export "memory") 1 1)
  (func (export "f") (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (i32.store8 (local.get $p) (i32.add (local.get $i) (i32.const 7)))
      (local.set $s (i32.add (local.get $s)
        (i32.add (i32.load offset=8 (local.get $p)) (i32.load offset=12 (local.get $p)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func (export "r") (result i32) (i32.load (i32.const 65532)))
  (func (export "q") (result i32) (i32.load (i32.const 16)))
)
