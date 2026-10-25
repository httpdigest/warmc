;; The class-initializer pattern TeaVM compiles a class's lazy
;; initialization to (opt.c pass_once): a global holds the initializer until
;; it runs, which first stores an empty function there, and each static
;; access calls what the global holds.  A call through such a global does
;; nothing when the initializer only disarms globals of the pattern that
;; nothing reads but to call them -- tail-called, tested for null or cast
;; too ($A, $A2, also inside a try) -- and where the global holds the empty
;; function on every way there: after another call through it ($B, also by
;; way of a cast, $M), a call of the initializer or a store of the empty
;; function, or in a loop's copy, which a loop entered before ($B in loop)
;; goes over to and one entered after takes ($B in loop_after); and a callee
;; called from two places, in a loop, whose only call is one of these is
;; inlined (hot).
;;
;; What keeps a call: a store of anything but the empty function ($C armed
;; again, $C2 by a function scanned before its initializer, $U with what no
;; ref.func says); an initializer whose first effect is something else --
;; it may return without disarming ($E), or throw before it does, after a
;; store into another global ($P) or a call ($P3); an "empty" function that
;; does something ($G2), stores into another global ($Qd) or traps ($Tz);
;; a function type with a result ($R); a global read for what it holds --
;; the initializer's type tells it from the empty function -- directly ($H),
;; by way of a cast to funcref ($H2), of a branch's result ($H3), of a
;; return ($H4) or as an argument ($H5); an initializer that disarms a
;; global that is not dead ($L); and a loop with a reference read after it
;; or a landing pad in it, which is not copied.  test/link/once_x.wat has a
;; global the embedder sets.
;;
;; At -O1, with nothing inlined: the calls through $A and $A2 dead, five;
;; one each after another call (twice, twice_cast), a call of the
;; initializer (direct) and a store of the empty function (disarmed); and
;; one out of each of two loops' copies.
;; stats -O1: once_dead=5 once_dominated=4 once_looped=2
;; At -O2 hot calls no function of the module's, $b_get inlined:
;; shape: hot bl\s+\S+ <_(?!warmc_)=0
;;
;; run: dead => i32:7
;; run: dead_twice => i32:14
;; run: twice => i32:1
;; run: loop 5 => i32:5
;; run: loop_after 4 => i32:4
;; run: rearm => i32:2
;; run: early => i32:2
;; run: busy => i32:2
;; run: seen => i32:0
;; run: refloop 3 => i32:3
;; run: hot 3 => i32:64
;; run: seen_cast => i32:0
;; run: seen_merge 1 => i32:0
;; run: seen_ret => i32:0
;; run: dead_tests => i32:3
;; run: quiet => i32:0
;; run: trapper => trap: unreachable
;; run: direct => i32:1
;; run: disarmed => i32:0
;; run: chain => i32:0
;; run: dead_try => i32:5
;; run: tryloop 3 => i32:3
;; run: twice_cast => i32:1
;; run: dead_tail => i32:4
;; run: results => i32:3
;; run: throwfirst => i32:1
;; run: callfirst => i32:1
;; run: rearm_unk => i32:2
;; run: rearm_early => i32:2
;; run: seen_arg => i32:0
;; run: b_twice 2 => i32:38
(module
  (type $vf (sub (func)))
  (type $vfF (sub $vf (func)))
  (global $count (mut i32) (i32.const 0))
  (global $flag (mut i32) (i32.const 1))
  (func $dummy (type $vf))
  ;; scanned before the initializers they re-arm with (rearm_early, rearm_unk)
  (func $rearm_c2 (global.set $C2 (ref.func $C2_init)))
  (func $rearm_u (global.set $U (global.get $bumpg)))
  (elem declare func $dummy $A_init $B_init $C_init $E_init $G_init $G2 $H_init
    $H2_init $H3_init $H4_init $A2_init $Q_init $Qd $K_init $T_init $Tz $L_init
    $M_init $R_init $Rd $P_init $P2_init $P3_init $bump $U_init $C2_init $H5_init
    $is_init2)

  ;; dead: its initializer only disarms it
  (global $A (mut (ref null $vf)) (ref.func $A_init))
  (func $A_init (type $vf) (global.set $A (ref.func $dummy)))
  (func (export "dead") (result i32)
    (call_ref $vf (global.get $A))
    (i32.const 7))
  (func (export "dead_twice") (result i32)
    (call_ref $vf (global.get $A))
    (call_ref $vf (global.get $A))
    (i32.const 14))

  ;; once: its initializer counts
  (global $B (mut (ref null $vf)) (ref.func $B_init))
  (func $B_init (type $vf)
    (global.set $B (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "twice") (result i32)
    (call_ref $vf (global.get $B))
    (call_ref $vf (global.get $B))
    (global.get $count))
  ;; the loop is entered before B is initialized and goes over to its copy
  (func (export "loop") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (call_ref $vf (global.get $B))
      (local.set $s (i32.add (local.get $s) (global.get $count)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  ;; and here entered after, B initialized by a call this function cannot see
  (func $init_b (call_ref $vf (global.get $B)))
  (func (export "loop_after") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (call $init_b)
    (loop $l
      (call_ref $vf (global.get $B))
      (local.set $s (i32.add (local.get $s) (global.get $count)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; a callee too big to be tiny, called from two places and in a loop:
  ;; inlined, its call through B being none the loop keeps
  (func $b_get (param $x i32) (result i32)
    (call_ref $vf (global.get $B))
    (i32.add (i32.mul (local.get $x) (i32.const 3))
      (i32.add (i32.xor (local.get $x) (i32.const 5))
        (i32.add (i32.shl (local.get $x) (i32.const 2))
          (i32.add (global.get $count) (i32.rotl (local.get $x) (i32.const 3)))))))
  (func (export "b_twice") (param $x i32) (result i32) (call $b_get (local.get $x)))
  (func $hot (export "hot") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (local.get $s) (call $b_get (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; armed again: not the pattern
  (global $C (mut (ref null $vf)) (ref.func $C_init))
  (func $C_init (type $vf)
    (global.set $C (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "rearm") (result i32)
    (call_ref $vf (global.get $C))
    (global.set $C (ref.func $C_init))
    (call_ref $vf (global.get $C))
    (global.get $count))

  ;; an initializer that may return without disarming: not the pattern
  (global $E (mut (ref null $vf)) (ref.func $E_init))
  (func $E_init (type $vf)
    (if (global.get $flag)
      (then
        (global.set $count (i32.add (global.get $count) (i32.const 1)))
        (global.set $flag (i32.const 0))
        (return)))
    (global.set $E (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "early") (result i32)
    (call_ref $vf (global.get $E))
    (call_ref $vf (global.get $E))
    (call_ref $vf (global.get $E))
    (global.get $count))

  ;; the function the initializer leaves does something: not the pattern
  (global $G (mut (ref null $vf)) (ref.func $G_init))
  (func $G_init (type $vf) (global.set $G (ref.func $G2)))
  (func $G2 (type $vf) (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "busy") (result i32)
    (call_ref $vf (global.get $G))
    (call_ref $vf (global.get $G))
    (call_ref $vf (global.get $G))
    (global.get $count))

  ;; read for what it holds: its initializer's type is not the empty one's
  (global $H (mut (ref null $vf)) (ref.func $H_init))
  (func $H_init (type $vfF) (global.set $H (ref.func $dummy)))
  (func (export "seen") (result i32)
    (call_ref $vf (global.get $H))
    (ref.test (ref $vfF) (global.get $H)))

  ;; read for what it holds by way of a cast to funcref, of a branch's
  ;; result, and of a return
  (global $H2 (mut (ref null $vf)) (ref.func $H2_init))
  (func $H2_init (type $vfF) (global.set $H2 (ref.func $dummy)))
  (func $is_init (param $f funcref) (result i32)
    (ref.test (ref $vfF) (local.get $f)))
  (func (export "seen_cast") (result i32)
    (call_ref $vf (global.get $H2))
    (call $is_init (ref.cast (ref null func) (global.get $H2))))
  (global $H3 (mut (ref null $vf)) (ref.func $H3_init))
  (func $H3_init (type $vfF) (global.set $H3 (ref.func $dummy)))
  (func (export "seen_merge") (param $k i32) (result i32)
    (call_ref $vf (global.get $H3))
    (ref.test (ref $vfF)
      (if (result (ref null $vf)) (local.get $k)
        (then (global.get $H3))
        (else (ref.func $dummy)))))
  (global $H4 (mut (ref null $vf)) (ref.func $H4_init))
  (func $H4_init (type $vfF) (global.set $H4 (ref.func $dummy)))
  (func $h4 (result (ref null $vf)) (global.get $H4))
  (func (export "seen_ret") (result i32)
    (call_ref $vf (global.get $H4))
    (ref.test (ref $vfF) (call $h4)))

  ;; tested for null and asserted not null, which tells nothing apart: dead
  (global $A2 (mut (ref null $vf)) (ref.func $A2_init))
  (func $A2_init (type $vf) (global.set $A2 (ref.func $dummy)))
  (func (export "dead_tests") (result i32)
    (if (ref.is_null (global.get $A2)) (then unreachable))
    (drop (ref.cast (ref null func) (global.get $A2)))
    (call_ref $vf (ref.as_non_null (global.get $A2)))
    (i32.const 3))
  ;; and tail-called, which leaves it dead
  (func $tail_a (return_call_ref $vf (global.get $A)))
  (func (export "dead_tail") (result i32)
    (call $tail_a)
    (i32.const 4))

  ;; a function type with a result: the empty function has none, so this is
  ;; not the pattern
  (type $fi (func (result i32)))
  (global $R (mut (ref null $fi)) (ref.func $R_init))
  (func $R_init (type $fi) (global.set $R (ref.func $Rd)) (i32.const 1))
  (func $Rd (type $fi) (i32.const 2))
  (func (export "results") (result i32)
    (i32.add (call_ref $fi (global.get $R)) (call_ref $fi (global.get $R))))

  ;; the function an initializer leaves stores into another global of the
  ;; pattern, so is not empty
  (global $K (mut (ref null $vf)) (ref.func $K_init))
  (func $K_init (type $vf)
    (global.set $K (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (global $Q (mut (ref null $vf)) (ref.func $Q_init))
  (func $Q_init (type $vf) (global.set $Q (ref.func $Qd)))
  (func $Qd (type $vf) (global.set $K (ref.func $dummy)))
  (func (export "quiet") (result i32)
    (call_ref $vf (global.get $Q))
    (call_ref $vf (global.get $Q))
    (call_ref $vf (global.get $K))
    (global.get $count))

  ;; and one that traps
  (global $T (mut (ref null $vf)) (ref.func $T_init))
  (func $T_init (type $vf) (global.set $T (ref.func $Tz)))
  (func $Tz (type $vf) (unreachable))
  (func (export "trapper") (result i32)
    (call_ref $vf (global.get $T))
    (call_ref $vf (global.get $T))
    (i32.const 1))

  ;; the initializer called directly, and the empty function stored: either
  ;; leaves the global holding it after
  (func (export "direct") (result i32)
    (call $B_init)
    (call_ref $vf (global.get $B))
    (global.get $count))
  (func (export "disarmed") (result i32)
    (global.set $B (ref.func $dummy))
    (call_ref $vf (global.get $B))
    (global.get $count))

  ;; an initializer that disarms a global of the pattern that is not dead
  (global $L (mut (ref null $vf)) (ref.func $L_init))
  (func $L_init (type $vf)
    (global.set $L (ref.func $dummy))
    (global.set $B (ref.func $dummy)))
  (func (export "chain") (result i32)
    (call_ref $vf (global.get $L))
    (call_ref $vf (global.get $B))
    (global.get $count))

  ;; a dead call in a try: the way to the handler goes with it
  (tag $t)
  (func (export "dead_try") (result i32)
    (block $h
      (try_table (catch_all $h)
        (call_ref $vf (global.get $A))))
    (i32.const 5))

  ;; a loop with a landing pad in it: not copied
  (func $maythrow (param $i i32) (if (i32.eq (local.get $i) (i32.const 99)) (then (throw $t))))
  (func (export "tryloop") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (call_ref $vf (global.get $B))
      (block $h
        (try_table (catch_all $h) (call $maythrow (local.get $i))))
      (local.set $s (i32.add (local.get $s) (global.get $count)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; the callee by way of a cast: still the global's
  (global $M (mut (ref null $vf)) (ref.func $M_init))
  (func $M_init (type $vf)
    (global.set $M (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "twice_cast") (result i32)
    (call_ref $vf (ref.cast (ref null $vf) (global.get $M)))
    (call_ref $vf (ref.cast (ref null $vf) (global.get $M)))
    (global.get $count))

  ;; an initializer whose first effect is another store, or a call, and
  ;; that throws before it disarms its own global: after the throw the
  ;; global still holds it, and the next call runs it again
  (global $P (mut (ref null $vf)) (ref.func $P_init))
  (global $P2 (mut (ref null $vf)) (ref.func $P2_init))
  (func $P2_init (type $vf) (global.set $P2 (ref.func $dummy)))
  (func $throw_once
    (if (global.get $flag) (then (global.set $flag (i32.const 0)) (throw $t))))
  (func $P_init (type $vf)
    (global.set $P2 (ref.func $dummy))
    (call $throw_once)
    (global.set $P (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "throwfirst") (result i32)
    (block $h (try_table (catch_all $h) (call_ref $vf (global.get $P))))
    (call_ref $vf (global.get $P))
    (global.get $count))
  (global $P3 (mut (ref null $vf)) (ref.func $P3_init))
  (func $P3_init (type $vf)
    (call $throw_once)
    (global.set $P3 (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "callfirst") (result i32)
    (block $h (try_table (catch_all $h) (call_ref $vf (global.get $P3))))
    (call_ref $vf (global.get $P3))
    (global.get $count))

  ;; stored what no ref.func says, and re-armed by a function scanned first
  (func $bump (type $vf) (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (global $bumpg (ref null $vf) (ref.func $bump))
  (global $U (mut (ref null $vf)) (ref.func $U_init))
  (func $U_init (type $vf)
    (global.set $U (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "rearm_unk") (result i32)
    (call_ref $vf (global.get $U))
    (call $rearm_u)
    (call_ref $vf (global.get $U))
    (global.get $count))
  (global $C2 (mut (ref null $vf)) (ref.func $C2_init))
  (func $C2_init (type $vf)
    (global.set $C2 (ref.func $dummy))
    (global.set $count (i32.add (global.get $count) (i32.const 1))))
  (func (export "rearm_early") (result i32)
    (call_ref $vf (global.get $C2))
    (call $rearm_c2)
    (call_ref $vf (global.get $C2))
    (global.get $count))

  ;; handed to a function as an argument
  (type $vfp (func (param funcref) (result i32)))
  (global $H5 (mut (ref null $vf)) (ref.func $H5_init))
  (func $H5_init (type $vfF) (global.set $H5 (ref.func $dummy)))
  (func $is_init2 (type $vfp) (ref.test (ref $vfF) (local.get 0)))
  (global $is_init2g (ref null $vfp) (ref.func $is_init2))
  (func (export "seen_arg") (result i32)
    (call_ref $vf (global.get $H5))
    (call_ref $vfp (global.get $H5) (global.get $is_init2g)))

  ;; a loop with an object read after it: not copied, the call kept
  (type $box (struct (field i32)))
  (func (export "refloop") (param $n i32) (result i32)
    (local $i i32) (local $o (ref null $box))
    (loop $l
      (call_ref $vf (global.get $B))
      (local.set $o (struct.new $box (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (struct.get $box 0 (local.get $o)) (global.get $count)))
)
