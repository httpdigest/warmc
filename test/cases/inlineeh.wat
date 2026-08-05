;; A multi-result call inlined from inside a try region.
;;
;; ir_build emits a call returning several values as the call followed by one
;; `result` instruction per value.  It calls eh_split first, though, and that
;; splits the block whenever it has any try depth at all -- a try_table with no
;; clauses counts.  So inside a try region the `result`s are not in the block
;; the call ends; they are in the one after it.
;;
;; The inliner replaced them by walking a single block: the flat path looked in
;; the caller's own block, the splice path in the continuation it had just
;; filled from the instructions after the call.  Neither is where eh_split put
;; them, so the call was unlinked and the `result`s were left behind reading a
;; value nothing defines.  ir_verify only range-checks argument references, so
;; they survived it, and isel lowers a `result` to a load from the caller's
;; return area -- which is to say the answers came back as whatever that stack
;; had in it.  With a callee pure enough to delete, that read zero.
;;
;; Both paths are covered because they are chosen by different things: the flat
;; path needs a single-block callee, the splice path anything else.  A callee
;; that stores is here because a pure one can be deleted outright, which hides
;; the bug behind a deterministic zero instead of stack residue.
(module
  (memory 1)
  (tag $t)

  (func $two (param $x i32) (result i32 i32)
    (i32.add (local.get $x) (i32.const 100))
    (i32.add (local.get $x) (i32.const 200)))

  ;; Multi-block, so inlining takes the splice path.
  (func $two_blocks (param $x i32) (result i32 i32)
    (block (br 0))
    (i32.add (local.get $x) (i32.const 100))
    (i32.add (local.get $x) (i32.const 200)))

  (func $three (param $x i32) (result i32 i32 i32)
    (local.get $x)
    (i32.add (local.get $x) (i32.const 10))
    (i32.add (local.get $x) (i32.const 20)))

  ;; Stores, so no pass may delete the body and read a settled zero for it.
  (func $two_store (param $x i32) (result i32 i32)
    (i32.store (i32.const 128) (local.get $x))
    (i32.add (local.get $x) (i32.const 100))
    (i32.add (local.get $x) (i32.const 200)))

  ;; ------------------------------------------------- inside a catching region

  (func (export "catch_all_two") (result i32)
    (block $out
      (try_table (catch_all $out)
        (call $two (i32.const 5))
        (i32.add)
        (return)))
    (i32.const -1))

  (func (export "catch_all_blocks") (result i32)
    (block $out
      (try_table (catch_all $out)
        (call $two_blocks (i32.const 5))
        (i32.add)
        (return)))
    (i32.const -1))

  (func (export "catch_all_three") (result i32)
    (block $out
      (try_table (catch_all $out)
        (call $three (i32.const 5))
        (i32.add)
        (i32.add)
        (return)))
    (i32.const -1))

  (func (export "catch_tag_two") (result i32)
    (block $out
      (try_table (catch $t $out)
        (call $two (i32.const 5))
        (i32.add)
        (return)))
    (i32.const -1))

  ;; A try_table with no clauses at all still sets the block's try depth, so it
  ;; still splits.  This is the flat path's version of the same bug.
  (func (export "no_clause_two") (result i32)
    (try_table
      (call $two (i32.const 5))
      (i32.add)
      (return))
    (i32.const -1))

  (func (export "no_clause_store") (result i32)
    (try_table
      (call $two_store (i32.const 5))
      (i32.add)
      (return))
    (i32.const -1))

  (func (export "catch_all_store") (result i32)
    (block $out
      (try_table (catch_all $out)
        (call $two_store (i32.const 5))
        (i32.add)
        (return)))
    (i32.const -1))

  ;; ------------------------------------------------------ outside one, for a
  ;; reading of the same callees where the results were never moved.

  (func (export "plain_two") (result i32)
    (call $two (i32.const 5))
    (i32.add))

  (func (export "plain_three") (result i32)
    (call $three (i32.const 5))
    (i32.add)
    (i32.add))

  (func (export "plain_store") (result i32)
    (call $two_store (i32.const 5))
    (i32.add))

  ;; The callee's result count is the one thing splice() sized its scratch
  ;; buffer for and never grew: it took the operand and edge-argument writers
  ;; up from 64 on demand and left the return path writing straight past it.
  ;; 65 is the threshold; 80 is here so the overrun is unmistakable.  A release
  ;; build answers this correctly while corrupting the heap behind it, so the
  ;; detector is test/run_asan.sh -- this module is the input it needs.
  (func $wide (param $x i32) (result i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    (block (br 0))
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x
      local.get $x)

  (func (export "wide_results") (result i32)
    (call $wide (i32.const 3))
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add
      i32.add)

  ;; What the stores left, so a wrong answer above cannot cancel out.
  (func (export "cell") (result i32) (i32.load (i32.const 128)))
)
