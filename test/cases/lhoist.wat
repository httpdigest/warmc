;; A loop header's invariant work, moved to where its first iteration began.
;;
;; `pass_loop_hoist` takes two things out of a loop header: a store of a fixed
;; value to a fixed address, because repeating one is the same as doing it once,
;; and a conditional branch to a trap on a condition the loop cannot change,
;; because it either traps on the first iteration or never.  Both rest on the
;; header running whenever the loop is entered, and the preheader running
;; whenever the header does.
;;
;; Four rules keep that honest, and every one of them was broken on purpose
;; against the whole suite -- 15,407 comparisons, three benchmark corpora, the GC
;; suite -- without a single failure.  This module is what stands behind them.
;;
;; Two things had to be arranged for it to test anything at all, and a first
;; version of this file tested nothing because neither was:
;;
;;   * **The addresses have to be opaque to `pass_memopt`.**  Written with plain
;;     constant addresses, memory forwarding answers the load from the store and
;;     then deletes the store as dead, and there is nothing left for this pass to
;;     decline.  So the second access goes through `64 + off` where `off` is
;;     loaded from memory and happens to be zero: the two really do alias, and
;;     nothing can prove it, which is exactly the case the rule is about.
;;   * **The branch has to be the header's own.**  With the trip test written
;;     first, the header ends in *that*, and an invariant early exit further down
;;     is never a candidate.  `param_*` puts the early exit first.
;;
;; Each export is a shape the pass must decline, written so that hoisting anyway
;; changes the answer:
;;
;;   alias_*   another store in the loop reaches the same bytes, with a load
;;             between the two.  Moving the header's store out stops it being
;;             re-established, and the load starts seeing the other store's value
;;             from the second iteration on.
;;   ahead_*   a load in the header ahead of the store, over the same bytes.  Its
;;             first execution has to see what was there before.
;;   param_*   the header's invariant branch leaves the loop carrying a value.
;;             Only a trap edge may be hoisted, and this is why: an ordinary
;;             exit's arguments are computed in the loop and differ every
;;             iteration.
;;   call_*    a call in the loop, which can read and write those bytes.  The
;;             callee increments them, so it sees a different value on every
;;             iteration exactly when the store did not move.
;;   bulk_*    the same, for the four bulk-memory instructions.  Every
;;             interference scan in the pass enumerated the store opcodes and
;;             nothing else, so memory.fill, memory.copy, memory.init and
;;             memory.grow were writers it could not see: the header's store
;;             moved out over the top of them, and from the second iteration on
;;             the loop read what the bulk write had left.  bulk_grow is the one
;;             that trapped rather than answered wrongly -- the store's bounds
;;             check went out with it, to where the page it addresses did not
;;             exist yet.
;;
;; `ok_*` are shapes the pass should accept, so a change that switches it off
;; entirely is more than a missing speedup.
;;
;; One rule is *not* covered here and says so in the source: the requirement that
;; two exactly-aliasing stores carry the same value.  Dropping it leaves this
;; module green, and the reason appears to be that the case cannot be built --
;; the only observation is a read between the two, and a read that matches them
;; that closely is one memory forwarding has already answered and deleted.
(module
  (memory 1)
  ;; trip count 4 at 0; the zero at 8 is the opaque offset; cell 64 is scratch
  (data (i32.const 0) "\04\00\00\00\00\00\00\00\00\00\00\00")
  (data $zeros "\00\00\00\00")

  ;; ------------------------------------------------------------- must decline

  (func (export "alias_store") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    ;; 64 + 0, but not knowably so
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 64) (i32.const 11))      ;; header, invariant
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (i32.store (local.get $p) (i32.const 22))      ;; may alias, and does
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  (func (export "ahead_load") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 7))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (i32.store (i32.const 64) (i32.const 11))      ;; behind the load
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; The header's *first* terminator is the invariant early exit, and it carries
  ;; the accumulator -- a different value every iteration.
  (func $param_loop (param $c i32) (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (i32.add
      (block $out (result i32)
        (block $done
          (loop $l
            (br_if $out (i32.mul (local.get $s) (i32.const 100)) (local.get $c))
            (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
            (local.set $s (i32.add (local.get $s) (i32.const 3)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $l)))
        (i32.const 0))
      (local.get $s)))

  (func (export "param_notaken") (result i32) (call $param_loop (i32.const 0)))
  (func (export "param_taken")   (result i32) (call $param_loop (i32.const 1)))

  (func $bump (result i32)
    (i32.store (i32.const 64)
               (i32.add (i32.load (i32.const 64)) (i32.const 1)))
    (i32.load (i32.const 64)))

  ;; The store is written *before* the trip test, so it really is in the header:
  ;; with it after, it sits in the body, rotation declines the loop for making a
  ;; call, and the store is never a candidate -- so the rule went untested.
  (func (export "call_reads") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (i32.store (i32.const 64) (i32.const 11))      ;; header, invariant
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (call $bump)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; A bulk write in the loop reaches the same bytes as the header's store, in
  ;; the shape alias_store uses: the load is between the two, so the store is
  ;; what puts the value back on every iteration.
  (func (export "bulk_fill") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (i32.store (i32.const 64) (i32.const 11))      ;; header, invariant
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (memory.fill (local.get $p) (i32.const 0) (i32.const 4))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  (func (export "bulk_copy") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (i32.store (i32.const 64) (i32.const 11))      ;; header, invariant
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (memory.copy (local.get $p) (i32.const 128) (i32.const 4))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  (func (export "bulk_init") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (i32.store (i32.const 64) (i32.const 11))      ;; header, invariant
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (memory.init $zeros (local.get $p) (i32.const 0) (i32.const 4))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; memory.grow does not write bytes, but the store addresses a page that only
  ;; exists once it has run.  Hoisting the store takes its bounds check along.
  (func (export "bulk_grow") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (drop (memory.grow (i32.const 1)))
        (i32.store (i32.const 65536) (i32.const 7))    ;; header, invariant
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.load (i32.const 65536)))

  ;; ------------------------------------------------------------ should hoist

  ;; Nothing in the loop reads the cell and nothing else writes it.
  (func (export "ok_store") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (local.get $p) (i32.const 11))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; The two stores of one value to one cell that AssemblyScript's shadow stack
  ;; actually emits: they alias exactly and carry the same value, so which order
  ;; they run in cannot matter and both may go.  Through the opaque pointer, so
  ;; memory forwarding does not delete one first.
  (func (export "ok_same_twice") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (local.get $p) (i32.const 11))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (i32.store (local.get $p) (i32.const 11))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; ------------------------------------- a store the block in front already made

  ;; `lh_kill_redundant` deletes a store in the loop when the run in front of the
  ;; loop already wrote that value to that address.  These two are the shapes it
  ;; must not: with either rule removed the suite stayed green.

  ;; A call in the loop can change those bytes behind its back.  The callee here
  ;; increments the cell, so the store really is re-establishing it each
  ;; iteration and deleting it lets the cell climb.
  (func (export "redun_call") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (i32.store (i32.const 64) (i32.const 11))      ;; establishes it
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 64) (i32.const 11))  ;; looks redundant
        (local.set $s (i32.add (local.get $s) (call $bump)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; Another store in the loop reaches the same bytes with a different value, so
  ;; the candidate is what puts them back each iteration.  Through the opaque
  ;; pointer, or memory forwarding settles it before this pass looks.
  (func (export "redun_clobber") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 11))      ;; establishes it
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 64) (i32.const 11))  ;; looks redundant
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (i32.store (local.get $p) (i32.const 22))  ;; and this undoes it
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; The same for the delete, since lh_kill_redundant runs on every loop --
  ;; ahead of the check that declines a loop making a call, so it sees strictly
  ;; more of them than the hoist does.
  (func (export "redun_fill") (result i32)
    (local $i i32) (local $n i32) (local $s i32) (local $p i32)
    (i32.store (i32.const 64) (i32.const 11))      ;; establishes it
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i32.add (i32.const 64) (i32.load (i32.const 8))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 64) (i32.const 11))  ;; looks redundant
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (memory.fill (local.get $p) (i32.const 0) (i32.const 4))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.load (i32.const 64))))

  ;; The interfering writer in front of the loop rather than inside it.  The
  ;; run the pass reads to decide the store was "already established" used to
  ;; look at nothing but stores, so a call or a bulk write between the
  ;; establishing store and the loop left the verdict standing and the store
  ;; that puts the value back each iteration was deleted.  Both writers reach
  ;; the cell through a pointer the compiler cannot fold, so nothing earlier
  ;; settles it.
  (func (export "redun_call_before") (result i32)
    (local $i i32) (local $n i32) (local $p i32)
    (local.set $p (i32.add (i32.const 192) (i32.load (i32.const 8))))
    (i32.store (i32.const 192) (i32.const 11))     ;; establishes it
    (call $bump192)                                ;; and this undoes it
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 192) (i32.const 11)) ;; looks redundant
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.load (local.get $p)))

  (func $bump192
    (i32.store (i32.const 192)
      (i32.add (i32.load (i32.const 192)) (i32.const 1))))

  (func (export "redun_fill_before") (result i32)
    (local $i i32) (local $n i32) (local $p i32)
    (local.set $p (i32.add (i32.const 256) (i32.load (i32.const 8))))
    (i32.store (i32.const 256) (i32.const 11))     ;; establishes it
    (memory.fill (i32.const 256) (i32.const 12) (i32.const 4))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 256) (i32.const 11)) ;; looks redundant
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.load (local.get $p)))

  ;; The cell on its own, so no answer above can hide a wrong byte in an
  ;; arithmetic coincidence.
  (func (export "cell") (result i32) (i32.load (i32.const 64)))
)
