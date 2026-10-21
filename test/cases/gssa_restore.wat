;; A global used as a stack pointer, the way LLVM, TinyGo and AssemblyScript
;; keep their shadow stacks, through global promotion (gssa).  Promotion keeps
;; the global as a value and writes it to memory only before a call that can
;; see it.  It once decided whether to write by comparing the value with the
;; one the function read on entry, and so a function that lowered the pointer,
;; called out and then restored it returned without writing the restore: the
;; memory still held the lowered pointer it had written before the call.
;;
;;   * $framed is that function, reached through call_indirect so nothing
;;     inlines it, and the export after two calls of it reads the pointer.
;;   * $looped moves the pointer on every iteration and calls out on some of
;;     them, the shape -O2 promotes by default; the callee reports the pointer
;;     it sees each time, and the export adds those up with the final value.

(module
  (memory 1)
  (type $t (func (result i32)))
  (table 3 funcref)
  (elem (i32.const 0) $leaf $framed $looped_entry)
  (global $sp (mut i32) (i32.const 8192))
  (global $leaf_ix (export "leaf_ix") (mut i32) (i32.const 0))
  (global $framed_ix (export "framed_ix") (mut i32) (i32.const 1))
  (global $looped_ix (export "looped_ix") (mut i32) (i32.const 2))

  ;; Takes a frame of its own and answers the pointer it was called with.
  (func $leaf (type $t)
    (local $at i32)
    (local.set $at (global.get $sp))
    (global.set $sp (i32.sub (local.get $at) (i32.const 16)))
    (i32.store (global.get $sp) (local.get $at))
    (global.set $sp (i32.add (global.get $sp) (i32.const 16)))
    (local.get $at))

  (func $framed (type $t)
    (local $saved i32) (local $r i32)
    (local.set $saved (global.get $sp))
    (global.set $sp (i32.sub (local.get $saved) (i32.const 48)))
    (i32.store (global.get $sp) (i32.const 99))
    (local.set $r (call_indirect (type $t) (global.get $leaf_ix)))
    (global.set $sp (local.get $saved))
    (local.get $r))

  (func $looped (param $n i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
      (i32.store (global.get $sp) (local.get $i))
      (local.set $acc (i32.add (local.get $acc) (i32.load (global.get $sp))))
      (if (i32.eq (i32.and (local.get $i) (i32.const 7)) (i32.const 5))
        (then
          (local.set $acc
            (i32.add (local.get $acc) (call_indirect (type $t) (global.get $leaf_ix))))))
      (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (local.get $n))))
    (local.get $acc))

  (func $looped_entry (type $t)
    (call $looped (i32.const 100)))

  (func (export "frame_restored_after_a_call") (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (call_indirect (type $t) (global.get $framed_ix)))
    (local.set $b (call_indirect (type $t) (global.get $framed_ix)))
    (i32.add (i32.mul (local.get $a) (i32.const 3))
             (i32.add (local.get $b) (global.get $sp))))

  (func (export "pointer_moved_in_a_loop") (result i32)
    (local $r i32)
    (local.set $r (call_indirect (type $t) (global.get $looped_ix)))
    (i32.add (local.get $r) (global.get $sp))))
