;; Reads of references at constant indices, which take the element's offset
;; as the load's immediate and the array itself as its base
;; (isel_core.inc arr_imm_index): where the array dies at the read, its
;; register may be the one the load writes, so the barrier's slot, which
;; the slow path heals, is made before the load overwrites it
;; (emit_gc_load_barrier_pre).  Collected at every allocation and every
;; object moved each time (test/run_gc.sh runs it so), each read after a
;; collection takes the slow path; a slot made wrong heals some other word,
;; which the verifier or the sums see.  Every export checks its own answer
;; and traps when it is wrong.  Assembled with wasm-tools.

(module
  (type $s (struct (field i32)))
  (type $refs (array (mut (ref null $s))))
  (type $outer (array (mut (ref null $refs))))

  (global $o (mut (ref null $outer)) (ref.null $outer))
  ;; where each round's allocation goes, so that it is made and fills pages
  (global $junk (mut (ref null $refs)) (ref.null $refs))

  (func $init
    (global.set $o
      (array.new_fixed $outer 3
        (array.new_fixed $refs 3 (struct.new $s (i32.const 1)) (struct.new $s (i32.const 2))
                                 (struct.new $s (i32.const 3)))
        (array.new_fixed $refs 3 (struct.new $s (i32.const 10)) (struct.new $s (i32.const 20))
                                 (struct.new $s (i32.const 30)))
        (array.new_fixed $refs 3 (struct.new $s (i32.const 100)) (struct.new $s (i32.const 200))
                                 (struct.new $s (i32.const 300))))))
  (start $init)

  (func $check (param $got i32) (param $want i32)
    (if (i32.ne (local.get $got) (local.get $want)) (then unreachable)))

  ;; n rounds, an allocation of 2 KiB each, of reads through two constant
  ;; indices:
  ;; 2 + 20 + 300 + 1 a round
  (func (export "chains") (param $n i32)
    (local $i i32) (local $sum i32)
    (loop $l
      (global.set $junk (array.new_default $refs (i32.const 256)))
      (local.set $sum
        (i32.add (local.get $sum)
          (i32.add
            (i32.add
              (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 0)) (i32.const 1)))
              (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 1)) (i32.const 1))))
            (i32.add
              (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 2)) (i32.const 2)))
              (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 0)) (i32.const 0)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (call $check (local.get $sum) (i32.mul (local.get $n) (i32.const 323))))

  ;; n rounds of a row's elements turned round at constant indices -- 0 and
  ;; 2 swapped, a new one at 1 -- each round allocating: the row's three
  ;; sum to 4 + the round's new one
  (func (export "swaps") (param $n i32)
    (local $i i32) (local $row (ref null $refs)) (local $t (ref null $s))
    (loop $l
      (local.set $row (array.get $outer (global.get $o) (i32.const 0)))
      (local.set $t (array.get $refs (local.get $row) (i32.const 0)))
      (array.set $refs (local.get $row) (i32.const 0) (array.get $refs (local.get $row) (i32.const 2)))
      (array.set $refs (local.get $row) (i32.const 2) (local.get $t))
      (array.set $refs (local.get $row) (i32.const 1) (struct.new $s (local.get $i)))
      (global.set $junk (array.new_default $refs (i32.const 256)))
      (call $check
        (i32.add
          (i32.add (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 0)) (i32.const 0)))
                   (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 0)) (i32.const 1))))
          (struct.get $s 0 (array.get $refs (array.get $outer (global.get $o) (i32.const 0)) (i32.const 2))))
        (i32.add (i32.const 4) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
)
