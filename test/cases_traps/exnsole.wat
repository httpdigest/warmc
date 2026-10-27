;; A program's only module with a tag and nothing of the collector's: an
;; exception passes through its frames -- twenty of a recursion no inlining
;; or jump takes away -- so it keeps its unwind records for the walk
;; (src/module_data.c), where one with no tag, never walked, keeps none
;; (run_traps.sh checks that too).
(module
  (tag $t (param i32))
  (func $deep (param $n i32)
    (if (i32.eqz (local.get $n)) (then (throw $t (i32.const 7))))
    (call $deep (i32.sub (local.get $n) (i32.const 1))))
  (func (export "caught_across")
    (if (i32.ne
          (block $h (result i32)
            (try_table (catch $t $h) (call $deep (i32.const 20)))
            (i32.const -1))
          (i32.const 7))
      (then (unreachable))))
  (func (export "uncaught") (call $deep (i32.const 20))))
