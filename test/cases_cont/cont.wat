;; Stack switching beyond the proposal's own tests (test/run_cont.sh): what a
;; continuation holds across collections -- in its frames, bound to it, on
;; its way through a suspension, in the heap -- exceptions into and out of
;; fibers, traps, a fiber's stack running out, switch, many values, and
;; chains of fibers.  Each export returns a value the row checks, or traps.
(module
  (type $f_v (func))
  (type $c_v (cont $f_v))
  (type $f_i (func (param i32)))
  (type $c_i (cont $f_i))
  (type $f_r_i (func (result i32)))
  (type $c_r_i (cont $f_r_i))
  (type $f_i_r_i (func (param i32) (result i32)))
  (type $c_i_r_i (cont $f_i_r_i))

  (type $node (struct (field $v i32) (field $next (ref null $node))))
  (type $garr (array (mut i64)))
  (type $karr (array (mut (ref null $c_v))))
  (type $f_n_r_i (func (param (ref null $node)) (result i32)))
  (type $c_n_r_i (cont $f_n_r_i))

  (rec (type $sw_ft (func (param i32) (param (ref null $sw_ct)) (result i32)))
       (type $sw_ct (cont $sw_ft)))

  ;; nine i64s, nine f64s, two v128s, an i32 and an f32: past every
  ;; register class of both targets
  (type $f_wide (func (param i64 i64 i64 i64 i64 i64 i64 i64 i64
                             f64 f64 f64 f64 f64 f64 f64 f64 f64
                             v128 v128 i32 f32) (result i64)))
  (type $c_wide (cont $f_wide))
  ;; eighteen results, past the return area's sixteen
  (type $f_many (func (param i32) (result i64 i64 i64 i64 i64 i64
                                          f64 f64 f64 f64 f64 f64
                                          i32 i32 i32 i32 v128 v128)))
  (type $c_many (cont $f_many))
  (type $f_susp (func (param i32) (result i64)))
  (type $c_susp (cont $f_susp))
  (type $f_many_r (func (param i64 i64 i64 i64 i64 i64
                               f64 f64 f64 f64 f64 f64
                               i32 i32 i32 i32 v128 v128) (result i64)))
  (type $c_many_r (cont $f_many_r))

  (tag $yield (param i32))
  (tag $ask (param i32) (result i32))
  (tag $exn (param i32))
  (tag $other)
  (tag $give (param (ref null $node)) (result (ref null $node)))
  (tag $swap (result i32))
  (tag $big (param i64 i64 i64 i64 i64 i64 i64 i64 i64
                   f64 f64 f64 f64 f64 f64 f64 f64 f64
                   v128 v128 i32 f32)
            (result i64 i64 i64 i64 i64 i64
                    f64 f64 f64 f64 f64 f64
                    i32 i32 i32 i32 v128 v128))

  (global $sw_n (mut i32) (i32.const 0))

  (elem declare func $gen $asker $gcworker $sumk $giver $inner $outer $thrower
                     $catcher $plain $outer_t $boom $rec $nop $lost $pp $wide
                     $many_res $susp_wide $level $sw_dead)

  ;; ---- helpers ----
  (func $mklist (param $n i32) (result (ref null $node))
    (local $h (ref null $node))
    (loop $l
      (if (local.get $n) (then
        (local.set $h (struct.new $node (local.get $n) (local.get $h)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l))))
    (local.get $h))
  (func $sumlist (param $h (ref null $node)) (result i32)
    (local $s i32)
    (block $e (loop $l
      (br_if $e (ref.is_null (local.get $h)))
      (local.set $s (i32.add (local.get $s) (struct.get $node $v (local.get $h))))
      (local.set $h (struct.get $node $next (local.get $h)))
      (br $l)))
    (local.get $s))
  ;; n arrays of 64 i64s, each the garbage the next makes of it: half a
  ;; kilobyte each, kept in a global so that no optimiser drops it
  (global $sink (mut (ref null $garr)) (ref.null $garr))
  (func $garbage (param $n i32)
    (loop $l (if (local.get $n) (then
      (global.set $sink (array.new $garr (i64.const 7) (i32.const 64)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))))
  (func $nop)

  ;; ---- a generator: the sum of 0 .. n-1, yielded one at a time ----
  (func $gen (param $n i32)
    (local $i i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (suspend $yield (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l))))
  (func $gen_sum (export "gen_sum") (param $n i32) (result i32)
    (local $k (ref null $c_v)) (local $s i32) (local $v i32)
    (block $done
      (block $y (result i32 (ref $c_v))
        (resume $c_i (on $yield $y) (local.get $n) (cont.new $c_i (ref.func $gen)))
        (br $done))
      (local.set $k) (local.set $v)
      (loop $next
        (local.set $s (i32.add (local.get $s) (local.get $v)))
        (block $y2 (result i32 (ref $c_v))
          (resume $c_v (on $yield $y2) (local.get $k))
          (br $done))
        (local.set $k) (local.set $v)
        (br $next)))
    (local.get $s))

  ;; ---- a suspension that gets a value back: the sum of squares 0..9 ----
  (func $asker (result i32)
    (local $acc i32) (local $i i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (suspend $ask (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 10))))
    (local.get $acc))
  (func (export "ask_sum") (result i32)
    (local $k (ref null $c_i_r_i)) (local $q i32)
    (block $first (result i32 (ref $c_i_r_i))
      (return (resume $c_r_i (on $ask $first) (cont.new $c_r_i (ref.func $asker)))))
    (local.set $k) (local.set $q)
    (loop $l
      (block $again (result i32 (ref $c_i_r_i))
        (return (resume $c_i_r_i (on $ask $again)
                  (i32.mul (local.get $q) (local.get $q)) (local.get $k))))
      (local.set $k) (local.set $q)
      (br $l))
    (unreachable))

  ;; ---- references in a suspended fiber's frames across collections ----
  ;; a frame below the fiber's first holds two lists of its own across the
  ;; suspension, which the first holds one across
  (func $holder (param $r i32) (result i32)
    (local $a (ref null $node)) (local $b (ref null $node))
    (local.set $a (call $mklist (i32.const 50)))
    (local.set $b (call $mklist (i32.const 20)))
    (suspend $yield (local.get $r))
    (i32.add (call $sumlist (local.get $a)) (call $sumlist (local.get $b))))
  (func $gcworker (param $rounds i32)
    (local $l (ref null $node)) (local $i i32)
    (local.set $l (call $mklist (i32.const 100)))
    (block $done
      (loop $r
        (br_if $done (i32.ge_u (local.get $i) (local.get $rounds)))
        (if (i32.ne (call $holder (local.get $i)) (i32.const 1485)) (then (unreachable)))
        (if (i32.ne (call $sumlist (local.get $l)) (i32.const 5050)) (then (unreachable)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $r))))
  (func (export "gc_frames") (param $rounds i32) (result i32)
    (local $k (ref null $c_v)) (local $count i32) (local $keep (ref null $node))
    (local.set $keep (call $mklist (i32.const 10)))
    (block $done
      (block $y (result i32 (ref $c_v))
        (resume $c_i (on $yield $y) (local.get $rounds) (cont.new $c_i (ref.func $gcworker)))
        (br $done))
      (local.set $k) (drop)
      (loop $l
        (local.set $count (i32.add (local.get $count) (i32.const 1)))
        (call $garbage (i32.const 2000))
        (block $y2 (result i32 (ref $c_v))
          (resume $c_v (on $yield $y2) (local.get $k))
          (br $done))
        (local.set $k) (drop)
        (br $l)))
    (if (i32.ne (call $sumlist (local.get $keep)) (i32.const 55)) (then (unreachable)))
    (local.get $count))

  ;; ---- many suspended fibers at once, their handles in the heap ----
  (func (export "gc_many") (param $n i32) (param $rounds i32) (result i32)
    (local $ks (ref null $karr)) (local $i i32) (local $done i32) (local $t (ref null $c_v))
    (local.set $ks (array.new_default $karr (local.get $n)))
    (loop $s
      (block $fin
        (block $y (result i32 (ref $c_v))
          (resume $c_i (on $yield $y) (local.get $rounds) (cont.new $c_i (ref.func $gcworker)))
          (br $fin))
        (local.set $t) (drop)
        (array.set $karr (local.get $ks) (local.get $i) (local.get $t)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $s (i32.lt_u (local.get $i) (local.get $n))))
    (block $all
      (loop $round
        (br_if $all (i32.ge_u (local.get $done) (local.get $n)))
        (local.set $i (i32.const 0))
        (loop $each
          (block $next
            (local.set $t (array.get $karr (local.get $ks) (local.get $i)))
            (br_if $next (ref.is_null (local.get $t)))
            (call $garbage (i32.const 50))
            (block $fin2
              (block $y2 (result i32 (ref $c_v))
                (resume $c_v (on $yield $y2) (local.get $t))
                (br $fin2))
              (local.set $t) (drop)
              (array.set $karr (local.get $ks) (local.get $i) (local.get $t))
              (br $next))
            (array.set $karr (local.get $ks) (local.get $i) (ref.null $c_v))
            (local.set $done (i32.add (local.get $done) (i32.const 1))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $each (i32.lt_u (local.get $i) (local.get $n))))
        (br $round)))
    (local.get $done))

  ;; ---- fibers dropped unfinished, which the collector frees ----
  (func (export "dropped") (param $n i32) (result i32)
    (local $i i32)
    (loop $l
      (block $fin
        (block $y (result i32 (ref $c_v))
          (resume $c_i (on $yield $y) (i32.const 3) (cont.new $c_i (ref.func $gcworker)))
          (br $fin))
        (drop) (drop))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $n))

  ;; ---- a reference bound to a continuation across a collection ----
  (func $sumk (param $h (ref null $node)) (result i32) (call $sumlist (local.get $h)))
  (func (export "bind_gc") (result i32)
    (local $k (ref null $c_r_i))
    (local.set $k (cont.bind $c_n_r_i $c_r_i (call $mklist (i32.const 30))
                    (cont.new $c_n_r_i (ref.func $sumk))))
    (call $garbage (i32.const 20000))
    (resume $c_r_i (local.get $k)))

  ;; ---- a reference given to a handler, and one given back ----
  (func $giver (result i32)
    (i32.add (call $sumlist (suspend $give (call $mklist (i32.const 40)))) (i32.const 1)))
  (func (export "payload_gc") (result i32)
    (local $k (ref null $c_n_r_i)) (local $l (ref null $node))
    (block $h (result (ref null $node) (ref $c_n_r_i))
      (return (resume $c_r_i (on $give $h) (cont.new $c_r_i (ref.func $giver)))))
    (local.set $k) (local.set $l)
    (call $garbage (i32.const 20000))
    (if (i32.ne (call $sumlist (local.get $l)) (i32.const 820)) (then (unreachable)))
    (resume $c_n_r_i (call $mklist (i32.const 5)) (local.get $k)))

;; ---- the same, the list made long before, on a page allocation has left
  ;; behind -- which a collection frees, or moves, unless the runtime holds
  ;; it as a root while it allocates the continuation's handle ----
  (func $giver_old (result i32)
    (local $l (ref null $node))
    (local.set $l (call $mklist (i32.const 40)))
    (call $garbage (i32.const 400))
    (i32.add (call $sumlist (suspend $give (local.get $l))) (i32.const 1)))
  (elem declare func $giver_old)
  (func (export "payload_old") (result i32)
    (local $k (ref null $c_n_r_i)) (local $l (ref null $node))
    (block $h (result (ref null $node) (ref $c_n_r_i))
      (return (resume $c_r_i (on $give $h) (cont.new $c_r_i (ref.func $giver_old)))))
    (local.set $k) (local.set $l)
    (call $garbage (i32.const 2000))
    (if (i32.ne (call $sumlist (local.get $l)) (i32.const 820)) (then (unreachable)))
    (resume $c_n_r_i (call $mklist (i32.const 5)) (local.get $k)))
  (func (export "bind_old") (result i32)
    (local $l (ref null $node)) (local $k (ref null $c_r_i))
    (local.set $l (call $mklist (i32.const 30)))
    (call $garbage (i32.const 400))
    (local.set $k (cont.bind $c_n_r_i $c_r_i (local.get $l)
                    (cont.new $c_n_r_i (ref.func $sumk))))
    (call $garbage (i32.const 2000))
    (resume $c_r_i (local.get $k)))

  ;; ---- a suspension past a resume with no handler for it ----
  (func $inner
    (suspend $yield (i32.const 1))
    (suspend $other)
    (suspend $yield (i32.const 2)))
  (func $outer (result i32)
    (local $k (ref null $c_v)) (local $s i32)
    (local.set $k (cont.new $c_v (ref.func $inner)))
    (block $done
      (loop $l
        (block $y (result i32 (ref $c_v))
          (resume $c_v (on $yield $y) (local.get $k))
          (br $done))
        (local.set $k)
        (local.set $s (i32.add (local.get $s)))
        (br $l)))
    (i32.mul (local.get $s) (i32.const 10)))
  (func (export "nested") (result i32)
    (local $k (ref null $c_r_i)) (local $n i32)
    (local.set $k (cont.new $c_r_i (ref.func $outer)))
    (loop $l
      (block $o (result (ref $c_r_i))
        (return (i32.add (resume $c_r_i (on $other $o) (local.get $k)) (local.get $n))))
      (local.set $k)
      (local.set $n (i32.add (local.get $n) (i32.const 100)))
      (br $l))
    (unreachable))

  ;; ---- a chain of d fibers, each holding a list, suspended at its bottom
  ;; to the outermost resume, a collection, and the whole chain resumed ----
  (func $level (param $d i32) (result i32)
    (local $mine (ref null $node))
    (local.set $mine (call $mklist (local.get $d)))
    (if (result i32) (i32.eqz (local.get $d))
      (then (suspend $other) (i32.const 0))
      (else
        (i32.add
          (resume $c_i_r_i (i32.sub (local.get $d) (i32.const 1))
                  (cont.new $c_i_r_i (ref.func $level)))
          (call $sumlist (local.get $mine))))))
  (func (export "deep_chain") (param $d i32) (result i32)
    (local $k (ref null $c_r_i))
    (block $o (result (ref $c_r_i))
      (return (resume $c_i_r_i (on $other $o) (local.get $d)
                      (cont.new $c_i_r_i (ref.func $level)))))
    (local.set $k)
    (call $garbage (i32.const 20000))
    (resume $c_r_i (local.get $k)))

  ;; ---- exceptions ----
  (func $thrower (throw $exn (i32.const 42)))
  (func (export "exn_out") (result i32)
    (block $c (result i32)
      (try_table (catch $exn $c)
        (resume $c_v (cont.new $c_v (ref.func $thrower))))
      (i32.const -1)))
  ;; caught where the continuation waits
  (func $catcher (result i32)
    (block $c (result i32)
      (try_table (catch $exn $c)
        (suspend $other))
      (i32.const -1))
    (i32.add (i32.const 1)))
  (func (export "exn_in") (result i32)
    (local $k (ref null $c_r_i))
    (block $o (result (ref $c_r_i))
      (return (resume $c_r_i (on $other $o) (cont.new $c_r_i (ref.func $catcher)))))
    (local.set $k)
    (resume_throw $c_r_i $exn (i32.const 42) (local.get $k)))
  ;; not caught there: out of the fiber to the resume_throw's handler
  (func $plain (result i32) (suspend $other) (i32.const 5))
  (func (export "exn_in_out") (result i32)
    (local $k (ref null $c_r_i))
    (block $o (result (ref $c_r_i))
      (return (resume $c_r_i (on $other $o) (cont.new $c_r_i (ref.func $plain)))))
    (local.set $k)
    (block $c (result i32)
      (try_table (result i32) (catch $exn $c)
        (resume_throw $c_r_i $exn (i32.const 77) (local.get $k)))
      (return))
    (i32.add (i32.const 1)))
  ;; into a continuation that never ran: thrown at the resume_throw
  (func (export "throw_fresh") (result i32)
    (block $c (result i32)
      (try_table (result i32) (catch $exn $c)
        (resume_throw $c_r_i $exn (i32.const 9) (cont.new $c_r_i (ref.func $plain))))
      (return))
    (i32.add (i32.const 100)))
  ;; out of two fibers
  (func $outer_t (result i32)
    (resume $c_v (cont.new $c_v (ref.func $thrower)))
    (i32.const 0))
  (func (export "exn_deep") (result i32)
    (block $c (result i32)
      (try_table (result i32) (catch $exn $c)
        (resume $c_r_i (cont.new $c_r_i (ref.func $outer_t))))
      (return)))
  ;; caught, and the fiber goes on to suspend and finish
  (func (export "exn_then_more") (result i32)
    (local $k (ref null $c_r_i)) (local $r i32)
    (block $o (result (ref $c_r_i))
      (return (resume $c_r_i (on $other $o) (cont.new $c_r_i (ref.func $catcher)))))
    (local.set $k)
    (local.set $r (resume_throw $c_r_i $exn (i32.const 6) (local.get $k)))
    (i32.add (local.get $r) (call $gen_sum (i32.const 10))))

  ;; ---- traps ----
  (func $boom (unreachable))
  (func (export "trap_in_fiber") (result i32)
    (resume $c_v (cont.new $c_v (ref.func $boom)))
    (i32.const 0))
;; a recursion no level makes a loop of: what each call gives back goes
  ;; through a division before its add, so no accumulator carries it
  (func $rec (param i32) (result i32)
    (i32.add (i32.div_u (call $rec (i32.add (local.get 0) (i32.const 1))) (i32.const 3))
             (local.get 0)))
  (func (export "overflow_in_fiber") (result i32)
    (resume $c_i_r_i (i32.const 0) (cont.new $c_i_r_i (ref.func $rec))))
  (func (export "consumed") (result i32)
    (local $k (ref null $c_v))
    (local.set $k (cont.new $c_v (ref.func $nop)))
    (resume $c_v (local.get $k))
    (resume $c_v (local.get $k))
    (i32.const 0))
  (func (export "null_cont") (result i32)
    (resume $c_v (ref.null $c_v))
    (i32.const 0))
  (func $lost (suspend $other))
  (func (export "unhandled") (result i32)
    (block $y (result i32 (ref $c_v))
      (resume $c_v (on $yield $y) (cont.new $c_v (ref.func $lost)))
      (return (i32.const 0)))
    (drop) (drop)
    (i32.const 0))
  (func (export "unhandled_root") (result i32)
    (suspend $other)
    (i32.const 0))
  (func (export "null_func") (result i32)
    (resume $c_v (cont.new $c_v (ref.null $f_v)))
    (i32.const 0))

  ;; ---- switch: two fibers taking turns, each holding a list ----
  (func $pp (type $sw_ft)
    (local $i i32) (local $k (ref null $sw_ct)) (local $mine (ref null $node))
    (local.set $i (local.get 0))
    (local.set $k (local.get 1))
    (local.set $mine (call $mklist (i32.const 10)))
    (loop $l
      (if (i32.ge_u (local.get $i) (global.get $sw_n)) (then (return (local.get $i))))
      (if (i32.eqz (i32.and (local.get $i) (i32.const 63)))
        (then (call $garbage (i32.const 400))))
      (switch $sw_ct $swap (i32.add (local.get $i) (i32.const 1)) (local.get $k))
      (local.set $k)
      (local.set $i)
      (if (i32.ne (call $sumlist (local.get $mine)) (i32.const 55)) (then (unreachable)))
      (br $l))
    (unreachable))
  (func (export "switch_pp") (param $n i32) (result i32)
    (global.set $sw_n (local.get $n))
    (resume $sw_ct (on $swap switch)
      (i32.const 0) (cont.new $sw_ct (ref.func $pp)) (cont.new $sw_ct (ref.func $pp))))
  ;; a switch to a continuation already consumed traps
  (func $sw_dead (type $sw_ft)
    (switch $sw_ct $swap (i32.const 1) (local.get 1))
    (unreachable))
  (func (export "switch_consumed") (result i32)
    (local $k (ref null $sw_ct))
    (local.set $k (cont.new $sw_ct (ref.func $nop_sw)))
    (drop (resume $sw_ct (i32.const 0) (ref.null $sw_ct) (local.get $k)))
    (resume $sw_ct (on $swap switch) (i32.const 0) (local.get $k)
            (cont.new $sw_ct (ref.func $sw_dead))))
  (func $nop_sw (type $sw_ft) (i32.const 0))
  (elem declare func $nop_sw)

  ;; ---- many values: a fiber's arguments, results and a suspension's ----
  (func $wide (type $f_wide)
    (local $s i64)
    (local.set $s (i64.add (local.get 0) (i64.mul (local.get 1) (i64.const 3))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 2) (i64.const 5))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 3) (i64.const 7))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 4) (i64.const 11))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 5) (i64.const 13))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 6) (i64.const 17))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 7) (i64.const 19))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 8) (i64.const 23))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 9) (f64.const 29)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 10) (f64.const 31)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 11) (f64.const 37)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 12) (f64.const 41)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 13) (f64.const 43)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 14) (f64.const 47)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 15) (f64.const 53)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 16) (f64.const 59)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 17) (f64.const 61)))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 0 (local.get 18)) (i64.const 67))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 1 (local.get 18)) (i64.const 71))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 0 (local.get 19)) (i64.const 73))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 1 (local.get 19)) (i64.const 79))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64.extend_i32_s (local.get 20)) (i64.const 83))))
    (i64.add (local.get $s) (i64.trunc_f32_s (f32.mul (local.get 21) (f32.const 89)))))
  (func $wide_args (param $b i64) (result i64 i64 i64 i64 i64 i64 i64 i64 i64
                                          f64 f64 f64 f64 f64 f64 f64 f64 f64
                                          v128 v128 i32 f32)
    (local.get $b) (i64.add (local.get $b) (i64.const 1)) (i64.add (local.get $b) (i64.const 2))
    (i64.add (local.get $b) (i64.const 3)) (i64.add (local.get $b) (i64.const 4))
    (i64.add (local.get $b) (i64.const 5)) (i64.add (local.get $b) (i64.const 6))
    (i64.add (local.get $b) (i64.const 7)) (i64.add (local.get $b) (i64.const 8))
    (f64.const 1.5) (f64.const 2.5) (f64.const 3.5) (f64.const 4.5) (f64.const 5.5)
    (f64.const 6.5) (f64.const 7.5) (f64.const 8.5) (f64.const 9.5)
    (v128.const i64x2 101 102) (v128.const i64x2 103 104)
    (i32.wrap_i64 (local.get $b)) (f32.const 0.75))
  (func (export "start_wide") (result i32)
    (i64.eq
      (call $wide (call $wide_args (i64.const 1000)))
      (resume $c_wide (call $wide_args (i64.const 1000)) (cont.new $c_wide (ref.func $wide)))))
  (func $many_res (type $f_many)
    (i64.extend_i32_u (local.get 0))
    (i64.const 2) (i64.const 3) (i64.const 4) (i64.const 5) (i64.const 6)
    (f64.const 0.5) (f64.const 1.5) (f64.const 2.5) (f64.const 3.5) (f64.const 4.5)
    (f64.convert_i32_u (local.get 0))
    (i32.const 7) (i32.const 8) (i32.const 9) (local.get 0)
    (v128.const i32x4 1 2 3 4) (i32x4.splat (local.get 0)))
  (func $fold (type $f_many_r)
    (local $s i64)
    (local.set $s (i64.add (local.get 0) (i64.mul (local.get 1) (i64.const 3))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 2) (i64.const 5))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 3) (i64.const 7))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 4) (i64.const 11))))
    (local.set $s (i64.add (local.get $s) (i64.mul (local.get 5) (i64.const 13))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 6) (f64.const 34)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 7) (f64.const 38)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 8) (f64.const 42)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 9) (f64.const 46)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 10) (f64.const 50)))))
    (local.set $s (i64.add (local.get $s) (i64.trunc_f64_s (f64.mul (local.get 11) (f64.const 54)))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64.extend_i32_s (local.get 12)) (i64.const 59))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64.extend_i32_s (local.get 13)) (i64.const 61))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64.extend_i32_s (local.get 14)) (i64.const 67))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64.extend_i32_s (local.get 15)) (i64.const 71))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 0 (local.get 16)) (i64.const 73))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 1 (local.get 16)) (i64.const 79))))
    (local.set $s (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 0 (local.get 17)) (i64.const 83))))
    (i64.add (local.get $s) (i64.mul (i64x2.extract_lane 1 (local.get 17)) (i64.const 89))))
  (func (export "results_wide") (result i32)
    (i64.eq
      (call $fold (call $many_res (i32.const 12)))
      (call $fold (resume $c_many (i32.const 12) (cont.new $c_many (ref.func $many_res))))))
  ;; a suspension with twenty-two values and eighteen back
  (func $susp_wide (type $f_susp)
    (call $fold (suspend $big (call $wide_args (i64.extend_i32_u (local.get 0))))))
  (func (export "suspend_wide") (result i32)
    (local $k (ref null $c_many_r)) (local $got i64)
    (block $h (result i64 i64 i64 i64 i64 i64 i64 i64 i64
                      f64 f64 f64 f64 f64 f64 f64 f64 f64
                      v128 v128 i32 f32 (ref $c_many_r))
      (drop (resume $c_susp (on $big $h) (i32.const 500) (cont.new $c_susp (ref.func $susp_wide))))
      (return (i32.const -1)))
    (local.set $k)
    (local.set $got (call $wide))
    (if (i64.ne (local.get $got) (call $wide (call $wide_args (i64.const 500))))
      (then (return (i32.const -2))))
    (i64.eq (resume $c_many_r (call $many_res (i32.const 33)) (local.get $k))
            (call $fold (call $many_res (i32.const 33)))))

;; ---- one float or vector result, which comes back in a vector register ----
  (type $f_r_f (func (result f64)))
  (type $c_r_f (cont $f_r_f))
  (type $f_r_v (func (result v128)))
  (type $c_r_v (cont $f_r_v))
  (func $half (result f64) (f64.const 2.75))
  (func $vec (result v128) (v128.const i32x4 5 6 7 8))
  (elem declare func $half $vec)
  (func (export "result_f64") (result i32)
    (f64.eq (resume $c_r_f (cont.new $c_r_f (ref.func $half))) (f64.const 2.75)))
  (func (export "result_v128") (result i32)
    (i32x4.extract_lane 3 (i32x4.add (resume $c_r_v (cont.new $c_r_v (ref.func $vec)))
                                     (v128.const i32x4 0 0 0 100))))

  ;; ---- a resume with two handlers, each suspension to its own ----
  (tag $first (param i32))
  (tag $second (param i32))
  (func $two (suspend $second (i32.const 20)) (suspend $first (i32.const 3)))
  (elem declare func $two)
  (func (export "two_handlers") (result i32)
    (local $k (ref null $c_v)) (local $s i32) (local $t i32)
    (local.set $k (cont.new $c_v (ref.func $two)))
    (block $done
      (loop $l
        (block $a (result i32 (ref $c_v))
          (block $b (result i32 (ref $c_v))
            (resume $c_v (on $first $a) (on $second $b) (local.get $k))
            (br $done))
          ;; $second's: tens
          (local.set $k)
          (local.set $t)
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $t) (i32.const 10))))
          (br $l))
        ;; $first's: ones
        (local.set $k)
        (local.set $s (i32.add (local.get $s)))
        (br $l)))
    (local.get $s))

;; ---- floats each side keeps in its callee-saved registers across
  ;; switches, which the other side's code uses for its own ----
  (func $fgen
    (local $a f64) (local $b f64) (local $c f64) (local $d f64)
    (local $e f64) (local $f f64) (local $i i32)
    (local.set $a (f64.const 1.5)) (local.set $b (f64.const 2.25))
    (local.set $c (f64.const -3.5)) (local.set $d (f64.const 0.125))
    (local.set $e (f64.const 10)) (local.set $f (f64.const 0.5))
    (loop $l
      (suspend $yield (i32.trunc_f64_s
        (f64.add (f64.add (f64.add (local.get $a) (local.get $b))
                          (f64.add (local.get $c) (local.get $d)))
                 (f64.add (local.get $e) (local.get $f)))))
      (local.set $a (f64.add (local.get $a) (f64.const 1)))
      (local.set $b (f64.add (local.get $b) (f64.const 2)))
      (local.set $c (f64.sub (local.get $c) (f64.const 0.5)))
      (local.set $d (f64.mul (local.get $d) (f64.const 2)))
      (local.set $e (f64.sub (local.get $e) (f64.const 1)))
      (local.set $f (f64.add (local.get $f) (f64.const 0.25)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 12)))))
  (elem declare func $fgen)
  (func (export "floats_across") (result i32)
    (local $k (ref null $c_v)) (local $s i32)
    (local $x f64) (local $y f64) (local $z f64) (local $w f64) (local $u f64) (local $t f64)
    (local.set $x (f64.const 100)) (local.set $y (f64.const -7.75))
    (local.set $z (f64.const 0.5)) (local.set $w (f64.const 1000))
    (local.set $u (f64.const 3)) (local.set $t (f64.const -1))
    (local.set $k (cont.new $c_v (ref.func $fgen)))
    (block $done
      (loop $l
        (block $y (result i32 (ref $c_v))
          (resume $c_v (on $yield $y) (local.get $k))
          (br $done))
        (local.set $k)
        (local.set $s (i32.add (local.get $s)))
        (local.set $x (f64.sub (local.get $x) (f64.const 3)))
        (local.set $y (f64.add (local.get $y) (f64.const 1.25)))
        (local.set $z (f64.mul (local.get $z) (f64.const 1.5)))
        (local.set $w (f64.sub (local.get $w) (f64.const 11)))
        (local.set $u (f64.add (local.get $u) (f64.const 2)))
        (local.set $t (f64.mul (local.get $t) (f64.const -1)))
        (br $l)))
    (i32.add (local.get $s)
      (i32.trunc_f64_s (f64.add (f64.add (f64.add (local.get $x) (local.get $y))
                                         (f64.add (local.get $z) (local.get $w)))
                                (f64.add (local.get $u) (local.get $t))))))

  ;; ---- the cost of a round trip: n suspensions and resumes ----
  (func $counter
    (local $i i32)
    (loop $l
      (suspend $yield (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
  (elem declare func $counter)
  (func (export "round_trips") (param $n i32) (result i32)
    (local $k (ref null $c_v)) (local $s i32) (local $i i32)
    (local.set $k (cont.new $c_v (ref.func $counter)))
    (loop $l
      (block $y (result i32 (ref $c_v))
        (resume $c_v (on $yield $y) (local.get $k))
        (unreachable))
      (local.set $k)
      (local.set $s (i32.add (local.get $s)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
