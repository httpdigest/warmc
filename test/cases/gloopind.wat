;; An indirect call the effects analysis cannot see the far side of.
;;
;; `pass_global_loop` promotes a global across a call when `call_effects` says
;; the callee cannot observe it.  For a direct call that is a real answer; for an
;; indirect one it is an answer only when the whole table can be read off the
;; element segments, and here it cannot be -- the table is *exported*, so the
;; embedder may have put anything in it.  `indirect_effects` gives up and says
;; "every global", and that answer is the only thing stopping the promotion.
;;
;; It needs its own module because gloop.wat's table *can* be read off its
;; segments: there the per-global mask says the target touches this global, so
;; deleting the give-up rule changes nothing and the ablation passes.  The rule
;; is only observable where the mask has nothing to say.
;;
;; The table entry reads the global the loop is accumulating into and returns it,
;; so a promotion that ignored the give-up feeds it a stale value and the
;; staleness lands in the answer.
(module
  (memory 1)
  (data (i32.const 0) "\05\00\00\00")
  (global $g (mut i32) (i32.const 1000))
  (type $void_to_i32 (func (result i32)))
  (table (export "t") 1 funcref)
  (elem (i32.const 0) $peek)

  ;; Returns the global itself rather than some function of it: an earlier
  ;; version divided by 100 and every export passed with the rule deleted,
  ;; because integer division threw away exactly the digits that had gone stale.
  (func $peek (type $void_to_i32) (global.get $g))

  (func (export "ind_acc") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g)
                                (call_indirect (type $void_to_i32) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; The same through `call_ref`, which has no table to read at all.
  (func (export "ref_acc") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g)
                                (call_ref $void_to_i32 (ref.func $peek))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; And the same reached through a *direct* call, which is where the give-up has
  ;; to survive being folded into a summary: `$outer` touches no global itself,
  ;; and everything it can observe it observes through the table.
  (func $outer (result i32) (call_indirect (type $void_to_i32) (i32.const 0)))

  (func (export "outer_acc") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $outer)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; A recursive callee, which the effects analysis refuses to guess a fixpoint
  ;; for.  What this one holds is that the *mask* survives the recursion: the
  ;; give-up on the recursive edge contributes no bits, so what refuses the
  ;; promotion is `$deep`'s own read -- deleting the give-up leaves this export
  ;; passing, and deleting the mask does not.
  (func $deep (param $n i32) (result i32)
    (if (i32.gt_s (local.get $n) (i32.const 0))
      (then (return (call $deep (i32.sub (local.get $n) (i32.const 1))))))
    (global.get $g))

  (func (export "deep_acc") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $deep (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))
)
