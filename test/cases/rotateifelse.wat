;; A loop whose header is an if/else with both arms inside the loop, the shape
;; clang leaves around the setjmp in test/cbench/setjmp.  Loop rotation used to
;; copy that header's test into the latch, which turned the loop into a cycle
;; with two ways in -- the old header now jumps into both arms -- and the
;; then-arm, a guard that traps, became a header of its own whose back edge it
;; did not dominate.  pass_loop_hoist found that header's body by flooding back
;; from the latch, ran out of the loop through the else-arm into the function's
;; entry, and moved the guard's operands -- the stack-pointer prologue among
;; them -- into a preheader on the then-arm: the prologue's global.set stored a
;; value made later, and the else-arm's stores went through a stale register.
;; -O1 and -O2 trapped out of bounds.
(module
  (memory 1)
  (global $sp (mut i32) (i32.const 1000))
  (global $lim (mut i32) (i32.const -1))
  (func (export "rotate_if_else_header") (result i32)
    (local $base i32) (local $i i32) (local $acc i32)
    global.get $sp
    i32.const 32
    i32.sub
    local.tee $base
    global.set $sp
    (loop $l
      local.get $i
      i32.const 1
      i32.and
      (if
        (then
          (if (i32.gt_u
                (i32.xor (i32.mul (i32.add (i32.rotl (i32.add (local.get $base) (i32.const 28))
                                                     (i32.const 3))
                                           (i32.const 7))
                                  (i32.const 5))
                         (i32.const 0x55))
                (global.get $lim))
            (then unreachable))
          local.get $base i32.const 28 i32.add local.get $i i32.store
          local.get $i i32.const 1 i32.add local.set $i
          br $l))
      local.get $base local.get $i i32.store
      local.get $acc local.get $base i32.const 28 i32.add i32.load i32.add local.set $acc
      local.get $i i32.const 1 i32.add local.tee $i
      i32.const 100 i32.lt_u
      br_if $l)
    local.get $base i32.const 32 i32.add global.set $sp
    local.get $acc))
