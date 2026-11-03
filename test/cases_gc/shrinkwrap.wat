;; Shrink-wrapped functions (codegen.c sw_start) under the collector: each
;; function here starts with a test that returns at once, a leaf at the
;; function's symbol, and the rest of it is its full body at "<sym>$full",
;; whose frames the runtime finds by that symbol's record alone.  A
;; collection while a full body's frame is on the stack, with references
;; live in it across a call (push_many, deep), must find and move them; an
;; exception thrown in a full body must unwind through it to the caller's
;; handler (throw_through).  The slow ways call $note, which is recursive
;; so that its calls stay calls, five times: more than the inliner takes a
;; callee with whole.  Each step makes 4 KB of garbage besides, so that
;; pages fill and are freed -- poisoned under WARMC_GC_VERIFY -- and an
;; object a missed root alone held reads as garbage.  Every export checks
;; its own answer and traps when it is wrong, as in gc.wat.
;;
;; Assembled with wasm-tools, not wat2wasm -- see test/run_gc.sh.

(module
  (type $box (struct (field $v i64)))
  (type $arr (array (mut (ref null $box))))
  (type $vec (struct (field $len (mut i32)) (field $data (mut (ref $arr)))))
  (type $big (array (mut i64)))
  (tag $full (param i32))
  (global $grows (mut i32) (i32.const 0))
  (global $sink (mut (ref null $box)) (ref.null $box))
  (global $junk (mut (ref null $big)) (ref.null $big))

  (func $note (param $k i32)
    (if (i32.eqz (local.get $k)) (then (return)))
    (global.set $grows (i32.add (global.get $grows) (i32.const 1)))
    (call $note (i32.sub (local.get $k) (i32.const 1))))

  ;; a bigger array for the vector, its elements copied over
  (func $grow (param $v (ref $vec)) (param $n i32)
    (local $nd (ref $arr))
    (local.set $nd (array.new_default $arr
      (i32.add (i32.shl (local.get $n) (i32.const 1)) (i32.const 1))))
    (array.copy $arr $arr (local.get $nd) (i32.const 0)
      (struct.get $vec $data (local.get $v)) (i32.const 0) (local.get $n))
    (struct.set $vec $data (local.get $v) (local.get $nd)))

  ;; the store and the count on the way that returns at once; the growing,
  ;; with $b live across the calls, behind it
  (func $push (param $v (ref $vec)) (param $b (ref $box)) (param $step i32)
    (local $n i32) (local $d (ref $arr))
    (local.set $n (struct.get $vec $len (local.get $v)))
    (local.set $d (struct.get $vec $data (local.get $v)))
    (if (i32.lt_u (local.get $n) (array.len (local.get $d)))
      (then
        (array.set $arr (local.get $d) (local.get $n) (local.get $b))
        (struct.set $vec $len (local.get $v) (i32.add (local.get $n) (i32.const 1)))
        (return)))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $grow (local.get $v) (local.get $n))
    ;; step live across the calls too, so that the full body saves registers
    (global.set $grows (i32.add (global.get $grows) (local.get $step)))
    (array.set $arr (struct.get $vec $data (local.get $v)) (local.get $n) (local.get $b))
    (struct.set $vec $len (local.get $v) (i32.add (local.get $n) (i32.const 1))))

  (func $sum (param $v (ref $vec)) (result i64)
    (local $i i32) (local $s i64) (local $d (ref $arr))
    (local.set $d (struct.get $vec $data (local.get $v)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (struct.get $vec $len (local.get $v))))
        (local.set $s (i64.add (local.get $s)
          (struct.get $box $v (ref.as_non_null (array.get $arr (local.get $d) (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; n boxes of 0 to n-1 pushed, with garbage made between: their sum is want
  (func (export "push_many") (param $n i32) (param $want i64)
    (local $v (ref $vec)) (local $i i32)
    (global.set $grows (i32.const 0))
    (local.set $v (struct.new $vec (i32.const 0) (array.new_default $arr (i32.const 1))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (global.set $sink (struct.new $box (i64.const -1)))
        (global.set $junk (array.new_default $big (i32.const 512)))
        (call $push (local.get $v) (struct.new $box (i64.extend_i32_u (local.get $i)))
                    (i32.and (local.get $i) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (if (i64.ne (call $sum (local.get $v)) (local.get $want)) (then (unreachable)))
    (if (i32.ne (struct.get $vec $len (local.get $v)) (local.get $n)) (then (unreachable)))
    (if (i32.eqz (global.get $grows)) (then (unreachable))))

  ;; $push, throwing the length where the vector would grow past limit
  (func $push_upto (param $v (ref $vec)) (param $b (ref $box)) (param $limit i32)
    (local $n i32) (local $d (ref $arr))
    (local.set $n (struct.get $vec $len (local.get $v)))
    (local.set $d (struct.get $vec $data (local.get $v)))
    (if (i32.lt_u (local.get $n) (array.len (local.get $d)))
      (then
        (array.set $arr (local.get $d) (local.get $n) (local.get $b))
        (struct.set $vec $len (local.get $v) (i32.add (local.get $n) (i32.const 1)))
        (return)))
    (call $note (i32.const 2))
    (call $note (i32.const 2))
    (call $note (i32.const 2))
    (call $note (i32.const 2))
    (call $note (i32.const 2))
    (if (i32.ge_u (local.get $n) (local.get $limit)) (then (throw $full (local.get $n))))
    (call $grow (local.get $v) (local.get $n))
    (array.set $arr (struct.get $vec $data (local.get $v)) (local.get $n) (local.get $b))
    (struct.set $vec $len (local.get $v) (i32.add (local.get $n) (i32.const 1))))

  ;; capacities 1, 3, 7, ...: the first growth at a length of limit or more
  ;; throws that length, want
  (func (export "throw_through") (param $n i32) (param $limit i32) (param $want i32)
    (local $v (ref $vec)) (local $i i32)
    (local.set $v (struct.new $vec (i32.const 0) (array.new_default $arr (i32.const 1))))
    (block $caught (result i32)
      (try_table (catch $full $caught)
        (block $done
          (loop $l
            (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
            (global.set $sink (struct.new $box (i64.const -1)))
        (global.set $junk (array.new_default $big (i32.const 512)))
            (call $push_upto (local.get $v) (struct.new $box (i64.extend_i32_u (local.get $i)))
                             (local.get $limit))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $l))))
      (i32.const -2))
    (if (i32.ne (local.get $want)) (then (unreachable))))

  ;; d levels down, a box made at each and live across the call below it:
  ;; d(d+1)(2d+1)/6 in all
  (func $walk (param $d i32) (param $acc (ref $box)) (result i64)
    (local $b (ref $box))
    (if (i32.eqz (local.get $d)) (then (return (struct.get $box $v (local.get $acc)))))
    (local.set $b (struct.new $box
      (i64.add (struct.get $box $v (local.get $acc)) (i64.extend_i32_u (local.get $d)))))
    (global.set $sink (struct.new $box (i64.const 0)))
    (global.set $junk (array.new_default $big (i32.const 512)))
    (i64.add (call $walk (i32.sub (local.get $d) (i32.const 1)) (local.get $b))
             (struct.get $box $v (local.get $acc))))

  (func (export "deep") (param $d i32) (param $want i64)
    (if (i64.ne (call $walk (local.get $d) (struct.new $box (i64.const 0))) (local.get $want))
      (then (unreachable))))

  ;; a count bumped, then the way out when k is 0 -- the start makes the
  ;; store there alone (sw_sink_to), the full body makes it on the way on
  ;; -- else the calls: bumped once a call either way, so n of them leave
  ;; it n, and twice on the way on would leave n + the k that were 1
  (type $cnt (struct (field $n (mut i64))))
  (func $bump (param $c (ref $cnt)) (param $k i32)
    (struct.set $cnt $n (local.get $c) (i64.add (struct.get $cnt $n (local.get $c)) (i64.const 1)))
    (if (i32.eqz (local.get $k)) (then (return)))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (global.set $sink (struct.new $box (struct.get $cnt $n (local.get $c)))))

  ;; two counts, each bumped ahead of a way out: the first way out only
  ;; when k is 0, so the second count's way out, k 1, must still find the
  ;; first count bumped -- n bumps of the first, as many as the k not 0 of
  ;; the second, whichever way each call left
  (type $cnt2 (struct (field $n (mut i64)) (field $m (mut i64))))
  ;; (values live across the calls on the way on, so that the whole saves
  ;; registers enough for a start of its own to pay)
  (func $bump2 (param $c (ref $cnt2)) (param $k i32)
    (local $a i64) (local $b i64) (local $d i64) (local $e i64)
    (struct.set $cnt2 $n (local.get $c) (i64.add (struct.get $cnt2 $n (local.get $c)) (i64.const 1)))
    (if (i32.eqz (local.get $k)) (then (return)))
    (struct.set $cnt2 $m (local.get $c) (i64.add (struct.get $cnt2 $m (local.get $c)) (i64.const 1)))
    (if (i32.eq (local.get $k) (i32.const 1)) (then (return)))
    (local.set $a (i64.extend_i32_u (global.get $grows)))
    (local.set $b (i64.mul (local.get $a) (i64.const 3)))
    (local.set $d (i64.add (local.get $b) (i64.extend_i32_u (local.get $k))))
    (local.set $e (i64.xor (local.get $d) (local.get $a)))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (call $note (i32.const 1))
    (global.set $sink (struct.new $box (i64.add (i64.add (local.get $a) (local.get $b)) (i64.add (local.get $d) (local.get $e))))))

  (func (export "bump2_many") (param $n i32) (param $want_n i64) (param $want_m i64)
    (local $c (ref $cnt2)) (local $i i32)
    (local.set $c (struct.new $cnt2 (i64.const 0) (i64.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (call $bump2 (local.get $c) (i32.and (local.get $i) (i32.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (if (i64.ne (struct.get $cnt2 $n (local.get $c)) (local.get $want_n)) (then (unreachable)))
    (if (i64.ne (struct.get $cnt2 $m (local.get $c)) (local.get $want_m)) (then (unreachable))))

  (func (export "bump_many") (param $n i32) (param $want i64)
    (local $c (ref $cnt)) (local $i i32)
    (local.set $c (struct.new $cnt (i64.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (global.set $junk (array.new_default $big (i32.const 512)))
        (call $bump (local.get $c) (i32.and (local.get $i) (i32.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (if (i64.ne (struct.get $cnt $n (local.get $c)) (local.get $want)) (then (unreachable))))
)
