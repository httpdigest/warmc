;; Three adapters at once, chosen for the three things composed.wat cannot
;; reach.
;;
;; composed.wat crosses a string on the way *back* -- the callee returns it and
;; the adapter lowers it into the caller's memory.  The other direction is a
;; different half of the same code and shares none of it: the caller's
;; arguments arrive flat, the string is validated in the caller's memory and
;; copied into the callee's, and the callee is handed a pointer it can read.
;; `sum` is that direction.
;;
;; `flag` is an adapter with no memory at all.  Its result is a `bool`, which
;; the core function behind it returns as 7 on purpose: lifting a bool is
;; `!= 0`, so a boundary that carried the bits across unchanged would answer 7
;; where the specification says 1.  Nothing in the caller's own world can
;; produce that -- a core module's bool is whatever it says it is -- so the
;; wrong answer is only reachable through a lift.
;;
;; `posts` is how post-return is observed inside a single call, which the
;; harness needs because it runs one call per process.  The specification calls
;; post-return after the results have been lowered and before the caller
;; continues, so a call to `posts` made afterwards must see 1.  Without a
;; count, a post-return that never ran looks exactly like one that did: it has
;; nothing to free in a provider that allocates by bumping.
;;
;; `wasmtime run --invoke "run()"` is the oracle for all three at once.
(component
  (component $provider
    (core module $m
      (memory (export "mem") 1)
      (global $bump (mut i32) (i32.const 1024))
      (global $posts (mut i32) (i32.const 0))
      (func (export "realloc") (param i32 i32 i32 i32) (result i32)
        global.get $bump
        global.get $bump local.get 3 i32.add global.set $bump)
      ;; Reads the string out of *its own* memory, which is only correct if
      ;; the adapter copied it there.  A pointer into the caller's memory
      ;; would read whatever this one happens to hold at that address.
      (func (export "sum") (param $p i32) (param $n i32) (result i32)
        (local $i i32) (local $s i32)
        (block $done (loop $l
          (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $s (i32.add (local.get $s)
                                 (i32.load8_u (i32.add (local.get $p) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l)))
        (local.get $s))
      (func (export "dropsum") (param i32)
        (global.set $posts (i32.add (global.get $posts) (i32.const 1))))
      (func (export "seven") (result i32) (i32.const 7))
      (func (export "count") (result i32) (global.get $posts))
      ;; Seventeen u32s is one past MAX_FLAT_PARAMS, so the caller does not
      ;; pass them in registers: it lays them out in memory and passes a
      ;; pointer.  The adapter has to check that pointer against the caller's
      ;; memory, allocate a block of its own in the callee's, and copy the
      ;; parameters across field by field -- which is `emit_params_copy`, and
      ;; nothing else in a component reaches it.  This reads the copy.
      (func (export "wide") (param $p i32) (result i32)
        (local $i i32) (local $s i32)
        (block $done (loop $l
          (br_if $done (i32.ge_u (local.get $i) (i32.const 68)))
          (local.set $s (i32.add (local.get $s)
                                 (i32.load (i32.add (local.get $p) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 4)))
          (br $l)))
        (local.get $s)))
    (core instance $i (instantiate $m))
    (func $sum (param "s" string) (result u32)
      (canon lift (core func $i "sum") (memory (core memory $i "mem")) (realloc (core func $i "realloc"))
                  (post-return (core func $i "dropsum"))))
    (func $flag (result bool)
      (canon lift (core func $i "seven")))
    (func $posts (result u32)
      (canon lift (core func $i "count")))
    (func $wide
      (param "a" u32) (param "b" u32) (param "c" u32) (param "d" u32)
      (param "e" u32) (param "f" u32) (param "g" u32) (param "h" u32)
      (param "i" u32) (param "j" u32) (param "k" u32) (param "l" u32)
      (param "m" u32) (param "n" u32) (param "o" u32) (param "p" u32)
      (param "q" u32) (result u32)
      (canon lift (core func $i "wide") (memory (core memory $i "mem")) (realloc (core func $i "realloc"))))
    (instance $iface
      (export "sum" (func $sum))
      (export "flag" (func $flag))
      (export "posts" (func $posts))
      (export "wide" (func $wide)))
    (export "test:fuse/ops" (instance $iface)))

  (component $consumer
    (import "test:fuse/ops" (instance $g
      (export "sum" (func (param "s" string) (result u32)))
      (export "flag" (func (result bool)))
      (export "posts" (func (result u32)))
      (export "wide" (func
        (param "a" u32) (param "b" u32) (param "c" u32) (param "d" u32)
        (param "e" u32) (param "f" u32) (param "g" u32) (param "h" u32)
        (param "i" u32) (param "j" u32) (param "k" u32) (param "l" u32)
        (param "m" u32) (param "n" u32) (param "o" u32) (param "p" u32)
        (param "q" u32) (result u32)))))
    (alias export $g "sum" (func $sum))
    (alias export $g "flag" (func $flag))
    (alias export $g "posts" (func $posts))
    (alias export $g "wide" (func $wide))
    ;; Two pages, and a cursor that starts in the second one.  That is what
    ;; makes the two allocators tell apart in the *other* direction: an
    ;; address this one hands out is not an address in the callee's memory at
    ;; all, so an adapter that allocated the callee's copy out of the caller's
    ;; allocator writes past the end of a one-page memory and traps.  Without
    ;; it both cursors are in range in both memories and the swap is invisible
    ;; -- whatever address comes back is where the copy goes and where it is
    ;; read from.
    (core module $mem
      (memory (export "mem") 2)
      ;; The bytes the caller sends.  They are in the *caller's* memory, which
      ;; is the whole point: nothing puts them in the callee's but the adapter.
      (data (i32.const 16) "warmc")
      (global $bump (mut i32) (i32.const 100000))
      (func (export "realloc") (param i32 i32 i32 i32) (result i32)
        global.get $bump
        global.get $bump local.get 3 i32.add global.set $bump))
    (core instance $mi (instantiate $mem))
    (core func $lsum (canon lower (func $sum)
      (memory (core memory $mi "mem")) (realloc (core func $mi "realloc"))))
    ;; Neither of these needs a memory: a `bool` and a `u32` are one flat value
    ;; each and nothing is behind a pointer.  So the adapter for them imports
    ;; no memory and no allocator, which is the other shape one comes in.
    (core func $lflag (canon lower (func $flag)))
    (core func $lposts (canon lower (func $posts)))
    (core func $lwide (canon lower (func $wide)
      (memory (core memory $mi "mem")) (realloc (core func $mi "realloc"))))
    (core module $c
      (import "" "sum" (func $sum (param i32 i32) (result i32)))
      (import "" "flag" (func $flag (result i32)))
      (import "" "posts" (func $posts (result i32)))
      (import "" "wide" (func $wide (param i32) (result i32)))
      (import "" "mem" (memory 1))
      ;; Three fields again, so a wrong answer says which of the three it was.
      (func (export "run") (result i32)
        (local $s i32) (local $f i32)
        (local.set $s (call $sum (i32.const 16) (i32.const 5)))
        (local.set $f (call $flag))
        (i32.add (i32.mul (local.get $s) (i32.const 1000))
                 (i32.add (i32.mul (local.get $f) (i32.const 10))
                          (call $posts))))
      ;; 1 through 17 laid out where the lowered signature says they go: one
      ;; 4-byte field each, at the alignment of the widest, which is the tuple
      ;; rule applied to a parameter list.  Their sum is 153, and every field
      ;; contributes -- a copy that stopped short or shifted would not add up.
      (func (export "wide") (result i32)
        (local $i i32)
        (block $done (loop $l
          (br_if $done (i32.ge_u (local.get $i) (i32.const 17)))
          (i32.store (i32.add (i32.const 4096) (i32.mul (local.get $i) (i32.const 4)))
                     (i32.add (local.get $i) (i32.const 1)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l)))
        (call $wide (i32.const 4096))))
    (core instance $ci (instantiate $c
      (with "" (instance (export "sum" (func $lsum))
                         (export "flag" (func $lflag))
                         (export "posts" (func $lposts))
                         (export "wide" (func $lwide))
                         (export "mem" (memory $mi "mem"))))))
    (func (export "run") (result u32)
      (canon lift (core func $ci "run")))
    (func (export "wide") (result u32)
      (canon lift (core func $ci "wide"))))

  (instance $pi (instantiate $provider))
  (alias export $pi "test:fuse/ops" (instance $g))
  (instance $ci (instantiate $consumer (with "test:fuse/ops" (instance $g))))
  (alias export $ci "run" (func $run))
  (alias export $ci "wide" (func $wide))
  (export "run" (func $run))
  (export "wide" (func $wide))
)
