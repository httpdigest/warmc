;; An early return ahead of the prologue whose entry block loads and stores
;; (isel.c early_ret_shape): the test makes the entry's loads, the early
;; return its stores and its result from what they loaded, and the entry
;; runs whole after the prologue the other way.  Each function here calls
;; itself the other way, so it has a prologue to skip and stays out of line.
;;
;; What it may not take, each a case here: a load after a store ($before,
;; which would read memory the store had not yet written), a load of another
;; memory ($m1, whose base the test does not load), an offset no ldr carries
;; ($farofs), and more loads than it has registers for ($six, which also
;; makes an operand of its test).  What it takes: a destructor's load, store
;; and test ($dtor), a test of a loaded word ($chain) and of a pointer it
;; loaded through ($chase), loads extended either way into i32 and i64
;; ($sgn, $sgn64, $zx), a compare of two loads ($lt2), an operand made from
;; one ($mask), stores of loaded words at offsets ($copy), and a store each
;; call makes once whichever way it returns ($count, the last of whose stores
;; the early return makes, which $count_check reads back).  A load in the
;; test that traps, and a store of the early return that does, trap as they
;; would have ($oob, $stoob).
;;
;; stats -O2 a64: early_rets=12
;;
;; run: chain 256 => i32:112
;; run: chain 272 => i32:100
;; run: destroy 32 => i32:3000
;; run: destroy 36 => i32:240
;; run: before 512 512 => i32:7
;; run: before 512 516 => i32:117
;; run: m1 768 => i32:21
;; run: farofs 0 => i32:6
;; run: sgn 1024 => i32:-128
;; run: sgn 1025 => i32:-111
;; run: sgn64 1028 => i64:-32640
;; run: sgn64 1030 => i64:-32579
;; run: zx 1024 => i64:128
;; run: zx 1025 => i64:143
;; run: lt2 1280 => i32:20
;; run: lt2 1284 => i32:40
;; run: lt2 1288 => i32:110
;; run: mask 1536 => i32:17
;; run: mask 1540 => i32:44
;; run: chase 1792 => i32:3648
;; run: copy_check 2048 => i32:33
;; run: count 2304 => i32:3
;; run: count_check 2304 => i32:303
;; run: six 2560 => i32:6
;; run: six 2564 => i32:18
;; run: oob 65536 => trap: out of bounds memory access
;; run: stoob 2304 65535 => trap: out of bounds memory access
;; run: stoob 2304 1000 => i32:3
(module
  (memory $m 1)
  (memory $m1 1)

  ;; a list of {value, next} at 0x100: 5, 7, 0, 9
  (data (i32.const 0x100) "\05\00\00\00\08\01\00\00\07\00\00\00\10\01\00\00\00\00\00\00\18\01\00\00\09\00\00\00\00\00\00\00")
  ;; a root pointer at 0x20 to a node of three words {tag, left, right} at
  ;; 0x40 whose children are nodes at 0x50 and 0x60 with none of their own;
  ;; 0x24 holds no pointer
  (data (i32.const 0x20) "\40\00\00\00")
  (data (i32.const 0x40) "\01\00\00\00\50\00\00\00\60\00\00\00")
  (data (i32.const 0x50) "\02\00\00\00\00\00\00\00\00\00\00\00")
  (data (i32.const 0x60) "\03\00\00\00\00\00\00\00\00\00\00\00")
  ;; 10 at 0x200 and 0x204
  (data (i32.const 0x200) "\0a\00\00\00\0a\00\00\00")
  ;; bytes 0x80 0x11 at 0x400, then halfwords 0x8080 and 61 at 0x404
  (data (i32.const 0x400) "\80\11\00\00\80\80\3d\00")
  ;; 10 30 70 at 0x500
  (data (i32.const 0x500) "\0a\00\00\00\1e\00\00\00\46\00\00\00")
  ;; 16 and 27 at 0x600
  (data (i32.const 0x600) "\10\00\00\00\1b\00\00\00")
  ;; 0x700 points at 0x710 whose word at +8 is 0x720; that points at 0x730,
  ;; whose word at +8 is 0
  (data (i32.const 0x700) "\10\07\00\00")
  (data (i32.const 0x710) "\20\07\00\00\00\00\00\00\20\07\00\00")
  (data (i32.const 0x720) "\30\07\00\00")
  ;; 11 22 0 at 0x800, 16 bytes apart
  (data (i32.const 0x800) "\0b\00\00\00")
  (data (i32.const 0x810) "\16\00\00\00")
  ;; three words at 0x900: 1 1 0
  (data (i32.const 0x900) "\01\00\00\00\01\00\00\00\00\00\00\00")
  ;; six words at 0xa00: 1 2 3 4 5 6, the first plus the last odd
  (data (i32.const 0xa00) "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00")
  ;; in memory 1 at 0x300: 6 15 0; memory 0 is 0 there
  (data (memory $m1) (i32.const 0x300) "\06\00\00\00\0f\00\00\00\00\00\00\00")

  ;; values until a zero one, which ends it with 100
  (func $chain (export "chain") (param $p i32) (result i32)
    (local $v i32) (local $n i32)
    (local.set $n (i32.load offset=4 (local.get $p)))
    (local.set $v (i32.load (local.get $p)))
    (if (i32.eqz (local.get $v)) (then (return (i32.const 100))))
    (i32.add (local.get $v) (call $chain (local.get $n))))

  ;; unique_ptr's destructor: the pointer out of its slot, the slot zeroed,
  ;; and the node's children destroyed; how many nodes it destroyed
  (func $dtor (param $slot i32) (result i32)
    (local $q i32)
    (local.set $q (i32.load (local.get $slot)))
    (i32.store (local.get $slot) (i32.const 0))
    (if (i32.eqz (local.get $q)) (then (return (i32.const 0))))
    (i32.add (i32.const 1)
      (i32.add (call $dtor (i32.add (local.get $q) (i32.const 4)))
               (call $dtor (i32.add (local.get $q) (i32.const 8))))))
  ;; 1000 a node destroyed, and what the slots of the tree still hold
  (func $destroy (export "destroy") (param $slot i32) (result i32)
    (i32.add (i32.mul (call $dtor (local.get $slot)) (i32.const 1000))
      (i32.add (i32.load (i32.const 0x20))
        (i32.add (i32.add (i32.load (i32.const 0x44)) (i32.load (i32.const 0x48)))
                 (i32.add (i32.load (i32.const 0x54)) (i32.load (i32.const 0x64)))))))

  ;; a store, then a load the store may have written: not taken
  (func $before (export "before") (param $p i32) (param $q i32) (result i32)
    (local $v i32)
    (i32.store (local.get $q) (i32.const 0))
    (local.set $v (i32.load (local.get $p)))
    (if (i32.eqz (local.get $v)) (then (return (i32.const 7))))
    (i32.add (i32.add (local.get $v) (i32.const 100)) (call $before (local.get $p) (local.get $p))))

  ;; a load of memory 1: not taken
  (func $m1 (export "m1") (param $p i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load $m1 (local.get $p)))
    (if (i32.eqz (local.get $v)) (then (return (i32.const 0))))
    (i32.add (local.get $v) (call $m1 (i32.add (local.get $p) (i32.const 4)))))

  ;; an offset past an ldr's reach: not taken
  (func $farofs (export "farofs") (param $p i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load offset=40000 (local.get $p)))
    (if (i32.eqz (local.get $v))
      (then
        (if (i32.eqz (local.get $p))
          (then (return (i32.add (call $farofs (i32.const 4)) (i32.const 5)))))
        (return (i32.const 1))))
    (local.get $v))

  ;; a byte sign-extended: below zero, it is the result
  (func $sgn (export "sgn") (param $p i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load8_s (local.get $p)))
    (if (i32.lt_s (local.get $v) (i32.const 0)) (then (return (local.get $v))))
    (i32.add (local.get $v) (call $sgn (i32.const 0x400))))

  ;; a halfword sign-extended into an i64
  (func $sgn64 (export "sgn64") (param $p i32) (result i64)
    (local $v i64)
    (local.set $v (i64.load16_s (local.get $p)))
    (if (i64.lt_s (local.get $v) (i64.const 0)) (then (return (local.get $v))))
    (i64.add (local.get $v) (call $sgn64 (i32.const 0x404))))

  ;; a byte zero-extended into an i64: above 100 it is the result
  (func $zx (export "zx") (param $p i32) (result i64)
    (local $v i64)
    (local.set $v (i64.load8_u (local.get $p)))
    (if (i64.gt_u (local.get $v) (i64.const 100)) (then (return (local.get $v))))
    (i64.add (i64.const 5) (call $zx (i32.add (local.get $p) (i32.const 1)))))

  ;; two loaded words compared: the first below the second, their difference
  (func $lt2 (export "lt2") (param $p i32) (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (i32.load (local.get $p)))
    (local.set $b (i32.load offset=4 (local.get $p)))
    (if (i32.lt_u (local.get $a) (local.get $b)) (then (return (i32.sub (local.get $b) (local.get $a)))))
    (i32.add (local.get $a) (call $lt2 (i32.sub (local.get $p) (i32.const 4)))))

  ;; a loaded word whose low three bits are clear, or one
  (func $mask (export "mask") (param $p i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load (local.get $p)))
    (if (i32.eqz (i32.and (local.get $v) (i32.const 7))) (then (return (i32.or (local.get $v) (i32.const 1)))))
    (i32.add (local.get $v) (call $mask (i32.sub (local.get $p) (i32.const 4)))))

  ;; through a pointer loaded at p to the word at +8 of what it points to:
  ;; zero there, and the pointer is the result
  (func $chase (export "chase") (param $p i32) (result i32)
    (local $q i32) (local $r i32)
    (local.set $q (i32.load (local.get $p)))
    (local.set $r (i32.load offset=8 (local.get $q)))
    (if (i32.eqz (local.get $r)) (then (return (local.get $q))))
    (i32.add (local.get $r) (call $chase (local.get $q))))

  ;; a word copied 8 on and a zero stored 12 on, then a test of the word
  (func $copy (param $p i32) (result i32)
    (local $a i32)
    (local.set $a (i32.load (local.get $p)))
    (i32.store offset=8 (local.get $p) (local.get $a))
    (i32.store offset=12 (local.get $p) (i32.const 0))
    (if (i32.eqz (local.get $a)) (then (return (i32.const 0))))
    (i32.add (local.get $a) (call $copy (i32.add (local.get $p) (i32.const 16)))))
  ;; what copy sums, less what it copied
  (func $copy_check (export "copy_check") (param $p i32) (result i32)
    (i32.sub (call $copy (local.get $p))
      (i32.add (i32.add (i32.load offset=8 (local.get $p)) (i32.load offset=24 (local.get $p)))
               (i32.sub (i32.load offset=40 (local.get $p)) (i32.const 33)))))

  ;; a counter at 0x80 each call bumps once, until a zero word
  (func $count (export "count") (param $p i32) (result i32)
    (local $c i32) (local $v i32)
    (local.set $c (i32.load (i32.const 0x80)))
    (local.set $v (i32.load (local.get $p)))
    (i32.store (i32.const 0x80) (i32.add (local.get $c) (i32.const 1)))
    (if (i32.eqz (local.get $v)) (then (return (i32.add (local.get $c) (i32.const 1)))))
    (call $count (i32.add (local.get $p) (i32.const 4))))

  ;; what count returns, and the counter it left in memory, which the last
  ;; call's early return stored
  (func $count_check (export "count_check") (param $p i32) (result i32)
    (i32.add (i32.mul (call $count (local.get $p)) (i32.const 100)) (i32.load (i32.const 0x80))))

  ;; six loaded words, one more than the test has registers for: not taken
  ;; -- the test would make (a + f) & 1 where the sixth is -- though its
  ;; result, the sixth, it could make; the others go to the other way
  (func $six (export "six") (param $p i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $e i32) (local $f i32)
    (local.set $a (i32.load (local.get $p)))
    (local.set $b (i32.load offset=4 (local.get $p)))
    (local.set $c (i32.load offset=8 (local.get $p)))
    (local.set $d (i32.load offset=12 (local.get $p)))
    (local.set $e (i32.load offset=16 (local.get $p)))
    (local.set $f (i32.load offset=20 (local.get $p)))
    (if (i32.and (i32.add (local.get $a) (local.get $f)) (i32.const 1))
      (then (return (local.get $f))))
    (i32.add (i32.add (i32.add (local.get $b) (local.get $c)) (i32.add (local.get $d) (local.get $e)))
             (call $six (i32.add (local.get $p) (i32.const 4)))))

  ;; the test's load traps
  (func $oob (export "oob") (param $p i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load (local.get $p)))
    (if (i32.eqz (local.get $v)) (then (return (i32.const 0))))
    (i32.add (local.get $v) (call $oob (i32.add (local.get $p) (i32.const 4)))))

  ;; the early return's store traps: 0x900's third word is zero, and the
  ;; store at q runs past the memory's end
  (func $stoob (export "stoob") (param $p i32) (param $q i32) (result i32)
    (local $v i32)
    (local.set $v (i32.load offset=8 (local.get $p)))
    (i32.store (local.get $q) (i32.const 1))
    (if (i32.eqz (local.get $v)) (then (return (i32.const 3))))
    (i32.add (local.get $v) (call $stoob (i32.add (local.get $p) (i32.const 4)) (local.get $q))))
)
