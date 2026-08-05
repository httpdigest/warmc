;; The collector's pause profile, as three workloads with nothing to hide
;; behind.  Not part of the correctness suite -- there is no native oracle for
;; a garbage collector -- and driven by test/run_gcbench.sh, which runs each
;; export under WARMC_GC_STATS and prints the pause lines side by side.
;;
;;   tree d n        a live binary tree of 2^(d+1)-1 nodes, then n garbage
;;                   allocations.  What a big live set costs per slice: the
;;                   whole tree is marked every cycle, and under stop-the-world
;;                   the longest pause is the whole of that marking.
;;
;;   bigtable e n    a table grown to e entries, every one holding a
;;                   reference, then n garbage allocations.  The root-scan
;;                   pause: before tables were scanned by the mark phase's
;;                   cursor, every cycle began with one pause proportional
;;                   to e.
;;
;;   churnstore d i n   a live tree of 2^(d+1)-1 nodes, then n rounds of i
;;                   reference stores with no allocation between them,
;;                   followed by one allocation.  When i outruns the barrier
;;                   buffer, the cycle restarts: pinned buffers restart
;;                   stop-the-world, growable ones double and stay
;;                   incremental.
;;
;;   coarse d k n    a live tree of 2^(d+1)-1 nodes, then n garbage arrays of
;;                   k words each and nothing else.  Every other workload here
;;                   allocates in small pieces, so the marking a step is asked
;;                   to pay for is small however the budget is capped, and a
;;                   ceiling that is far too low is invisible.  This one asks
;;                   for the whole of a step's budget in one allocation: the
;;                   effective marking rate is the ceiling over k, so a
;;                   ceiling that cannot keep up shows here as the collector
;;                   losing the race and the heap answering with a full
;;                   collection -- a pause three orders of magnitude larger
;;                   than a slice.  It is the workload that says how low
;;                   WARMC_GC_SLICE_MAX may go.
(module
  (type $pair (struct (field $a (mut (ref null $pair)))
                      (field $b (mut (ref null $pair)))))
  (type $prefs (array (mut (ref null $pair))))
  (type $words (array (mut i64)))

  (table $bt 0 1048576 (ref null $pair))
  (global $root (mut (ref null $pair)) (ref.null $pair))
  ;; Garbage the escape analysis must not delete: dropped allocations vanish
  ;; at -O1 and above, and this bench exists to feed the collector.
  (global $sink (mut anyref) (ref.null any))
  (global $arr (mut (ref null $prefs)) (ref.null $prefs))

  (func $fail (unreachable))

  (func $mktree (param $d i32) (result (ref null $pair))
    (if (result (ref null $pair)) (i32.eqz (local.get $d))
      (then (struct.new $pair (ref.null $pair) (ref.null $pair)))
      (else (struct.new $pair
              (call $mktree (i32.sub (local.get $d) (i32.const 1)))
              (call $mktree (i32.sub (local.get $d) (i32.const 1)))))))

  (func $churn (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $sink (struct.new $pair (ref.null $pair) (ref.null $pair)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  (func (export "tree") (param $d i32) (param $n i32)
    (global.set $root (call $mktree (local.get $d)))
    (call $churn (local.get $n))
    (if (ref.is_null (global.get $root)) (then (call $fail)))
    (global.set $root (ref.null $pair)))

  (func (export "bigtable") (param $e i32) (param $n i32)
    (local $i i32) (local $p (ref $pair))
    (local.set $p (struct.new $pair (ref.null $pair) (ref.null $pair)))
    (drop (table.grow $bt (local.get $p) (local.get $e)))
    ;; Every sixty-fourth entry its own object, so the scan both rejects
    ;; duplicates and finds new work at a realistic mix.
    (block $done
      (br_if $done (i32.eqz (local.get $e)))
      (loop $l
        (table.set $bt (local.get $i)
          (struct.new $pair (ref.null $pair) (ref.null $pair)))
        (local.set $i (i32.add (local.get $i) (i32.const 64)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $e)))))
    (call $churn (local.get $n))
    (if (ref.is_null (table.get $bt (i32.const 0))) (then (call $fail))))

  ;; press live n   a live array of `live` nodes built one at a time, with `n`
  ;;                 garbage allocations between each: a *large live set* under
  ;;                 a high allocation rate, which is the shape the other four
  ;;                 do not have.  It is here because it found two pauses they
  ;;                 could not: a chunked array rescanned whole inside one
  ;;                 slice, and a barrier buffer growing by copying itself.
  ;;                 Both were bounded by nothing the slice budget could see.
  ;;
  ;;                 It is also the memory-pressure probe: at `live` of eight
  ;;                 million the live set is three hundred and twenty megabytes
  ;;                 of a one gigabyte heap, and the answer is checked, so a
  ;;                 collector that lost one of them says so.
  (func (export "press") (param $live i32) (param $n i32)
    (local $a (ref $prefs)) (local $i i32) (local $j i32) (local $s i32)
    (local.set $a (array.new_default $prefs (local.get $live)))
    (global.set $arr (local.get $a))
    (loop $l
      (array.set $prefs (local.get $a) (local.get $i)
        (struct.new $pair (ref.null $pair) (ref.null $pair)))
      (local.set $j (i32.const 0))
      (block $done
        (br_if $done (i32.eqz (local.get $n)))
        (loop $c
          (global.set $sink (struct.new $pair (ref.null $pair) (ref.null $pair)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br_if $c (i32.lt_u (local.get $j) (local.get $n)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $live))))
    ;; every one of them still there
    (local.set $i (i32.const 0))
    (loop $v
      (if (ref.is_null (array.get $prefs (local.get $a) (local.get $i)))
        (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $v (i32.lt_u (local.get $i) (local.get $live))))
    (global.set $arr (ref.null $prefs)))

  (func (export "churnstore") (param $d i32) (param $i i32) (param $n i32)
    (local $a (ref $prefs)) (local $j i32) (local $k i32)
    (global.set $root (call $mktree (local.get $d)))
    (local.set $a (array.new_default $prefs (i32.const 8192)))
    (global.set $arr (local.get $a))
    (local.set $j (i32.const 0))
    (loop $seed
      (array.set $prefs (local.get $a) (local.get $j)
        (struct.new $pair (ref.null $pair) (ref.null $pair)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $seed (i32.lt_u (local.get $j) (i32.const 8192))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $outer
        (local.set $j (i32.const 0))
        (block $inner_done
          (br_if $inner_done (i32.eqz (local.get $i)))
          (loop $inner
            ;; Move a reference one slot along: every store overwrites a
            ;; non-null reference, which is what the barrier has to record.
            (array.set $prefs (local.get $a)
              (i32.and (local.get $j) (i32.const 8191))
              (array.get $prefs (local.get $a)
                (i32.and (i32.add (local.get $j) (i32.const 1))
                         (i32.const 8191))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br_if $inner (i32.lt_u (local.get $j) (local.get $i)))))
        ;; A little allocation per round -- fewer allocations than marking
        ;; takes slices, so a mark phase always spans several rounds and the
        ;; barrier is live when the next burst of stores arrives.
        (call $churn (i32.const 128))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $outer (i32.lt_u (local.get $k) (local.get $n)))))
    (if (ref.is_null (global.get $root)) (then (call $fail)))
    (global.set $root (ref.null $pair))
    (global.set $arr (ref.null $prefs)))

  ;; A large live set and nothing but large allocations.  `k` words per array,
  ;; so one step of the collector is asked to cover k*8 bytes of allocation --
  ;; and since the arrays hold no references, none of the cost is tracing them:
  ;; what is being measured is purely whether the budget one allocation buys
  ;; keeps the marking of the *tree* ahead of the heap filling up.
  (func (export "coarse") (param $d i32) (param $k i32) (param $n i32)
    (local $i i32)
    (global.set $root (call $mktree (local.get $d)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $sink (array.new_default $words (local.get $k)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (if (ref.is_null (global.get $root)) (then (call $fail)))
    (global.set $root (ref.null $pair)))
)
