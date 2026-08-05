;; The one thing an exception can carry away that a trap cannot: a global the
;; promotion in `pass_global_loop` has stopped maintaining.
;;
;; The general form of that promotion keeps the global in a register for the
;; whole loop and stores it back on the edges that leave.  An *exception* leaves
;; by no edge at all, so there is nowhere to put the write-back -- and unlike a
;; trap, which ends the program, a handler up the stack goes on to read the
;; global.  So the promotion is taken only when the module declares no tags,
;; which makes a throw impossible rather than merely unlikely.
;;
;; This module declares one, and that is the whole point of it: the loop below
;; is otherwise exactly the shape the general form is for -- an accumulator, a
;; call the callee's effects clear -- so nothing but the tag rule stops it.
;; Delete that rule and `eh_stale` reports the global as it was on the way in.
;;
;; The trip count comes from memory so the loop is not unrolled or folded first,
;; and the throw is on the third iteration so there is a partial sum to lose.
(module
  (memory 1)
  (data (i32.const 0) "\05\00\00\00")
  (tag $e (param i32))
  (global $g (mut i32) (i32.const 1000))

  ;; Touches no global, so the call itself is one the promotion may see through.
  ;; What it does do is throw, on the iteration named by its argument.
  (func $maybe (param $i i32) (result i32)
    (if (i32.eq (local.get $i) (i32.const 2))
      (then (local.get $i) (throw $e)))
    (i32.const 10))

  ;; No handler here: the exception leaves this function, so the loop is left by
  ;; something none of its edges describes.
  (func $body (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $maybe (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.const 0))

  ;; Two iterations add 10 each and the third throws, so the global is 1020 when
  ;; the handler runs.  A promotion that ignored the tag would leave 1000.
  (func (export "eh_stale") (result i32)
    (global.set $g (i32.const 1000))
    (drop (block $h (result i32)
      (try_table (catch $e $h) (drop (call $body)))
      (i32.const -1)))
    (global.get $g))

  ;; The payload as well, so a case that only ever looks at the global cannot
  ;; pass by throwing from somewhere else.
  (func (export "eh_payload") (result i32)
    (global.set $g (i32.const 1000))
    (block $h (result i32)
      (try_table (catch $e $h) (drop (call $body)))
      (i32.const -1)))

  ;; And the same loop with the throw switched off, which the promotion may not
  ;; take either -- the rule is about the module, not about this call site.  Here
  ;; to keep the accumulator itself under test in a module that has a tag.
  (func $quiet (param $i i32) (result i32) (i32.const 10))

  (func (export "eh_none") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $quiet (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))
)
