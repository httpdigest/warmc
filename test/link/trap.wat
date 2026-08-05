;; What the memory holds when a trap ends a loop early.
;;
;; The only way to ask this is from an embedder: warmc's own CLI runs one
;; export per process and a trap ends the process, so nothing inside wasm can
;; look at the memory afterwards.  test/link/trapdriver.c catches the trap and
;; then reads the cell.
;;
;; `hoist` divides by its second argument at the top of the loop and stores a
;; constant to a fixed address underneath.  Called with a zero divisor it
;; traps on the first iteration, so the store never runs and the cell keeps
;; whatever it had.  The store is loop-invariant and the loop-hoisting pass
;; moves it to the preheader, which is only correct if nothing ahead of it in
;; the loop can trap -- and the filter it used to apply looked at memory
;; operations only, so the divide did not stop it.

(module
  (memory 1)
  (data (i32.const 64) "\2a\00\00\00")

  (func (export "hoist") (param $n i32) (param $d i32)
    (local $i i32)
    (loop $l
      (drop (i32.div_s (i32.const 100) (local.get $d)))
      (i32.store (i32.const 64) (i32.const 777))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $i) (local.get $n)))))

  ;; The same shape with a divisor that cannot trap, so the store really is
  ;; hoistable and the case above is not passing because the pass gave up.
  (func (export "ok") (param $n i32)
    (local $i i32)
    (loop $l
      (drop (i32.div_s (i32.const 100) (i32.const 5)))
      (i32.store (i32.const 68) (i32.const 778))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $i) (local.get $n)))))

  (func (export "peek") (param $a i32) (result i32) (i32.load (local.get $a))))
