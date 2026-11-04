;; Loop unswitching (opt.c pass_unswitch): a loop with a branch on a value
;; made before it is copied, the original taking the branch's first way
;; every time round and the copy its second, a test ahead of them picking
;; one -- plain (both ways hot, a small loop), with one way cold by a branch
;; hint (an inline cache's guard and its slow way), with values the loop
;; makes read after it (a sum, a reference), round an inner loop of an
;; outer one whose test is the outer's (the inner copied, an outer loop
;; never: its copy would be its inner loops' too), and with the branch's
;; way leaving the loop.  The answers are the same at every level
;; (wasmtime's).
;;
;; stats -O2: unswitched=5
;;
;; run: plain 0 10 => i32:45
;; run: plain 1 10 => i32:-45
;; run: plain 0 0 => i32:0
;; run: plain 7 1 => i32:0
;; run: guard 0 5 => i32:15
;; run: guard 1 5 => i32:615
;; run: guard 1 0 => i32:100
;; run: escref 0 4 => i32:6
;; run: escref 1 4 => i32:12
;; run: escref 1 0 => i32:-1
;; run: nested 0 3 => i32:9
;; run: nested 1 3 => i32:18
;; run: nested 0 0 => i32:0
;; run: leave 0 10 => i32:45
;; run: leave 1 10 => i32:0
;; run: leave 1 2 => i32:0
(module
  (type $s (struct (field (mut i32))))
  (global $sink (mut i32) (i32.const 0))

  (func $plain (export "plain") (param $k i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_s (local.get $i) (local.get $n)))
        (if (local.get $k)
          (then (local.set $s (i32.sub (local.get $s) (local.get $i))))
          (else (local.set $s (i32.add (local.get $s) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  (func $slow (param i32) (result i32)
    (global.set $sink (local.get 0))
    (i32.add (local.get 0) (i32.const 100)))

  (func $guard (export "guard") (param $k i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $out
      (loop $l
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $out (i32.gt_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (@metadata.code.branch_hint "\00")
        (if (local.get $k)
          (then (local.set $s (call $slow (local.get $s)))))
        (br $l)))
    (if (result i32) (local.get $k)
      (then (i32.add (local.get $s) (i32.const 100)))
      (else (local.get $s))))

  (func $escref (export "escref") (param $k i32) (param $n i32) (result i32)
    (local $i i32) (local $r (ref null $s))
    (block $out
      (loop $l
        (br_if $out (i32.ge_s (local.get $i) (local.get $n)))
        (if (local.get $k)
          (then (local.set $r (struct.new $s (i32.mul (local.get $i) (i32.const 4)))))
          (else (local.set $r (struct.new $s (i32.mul (local.get $i) (i32.const 2))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (if (result i32) (ref.is_null (local.get $r))
      (then (i32.const -1))
      (else (struct.get $s 0 (local.get $r)))))

  (func $nested (export "nested") (param $k i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $j (i32.const 0))
        (block $iout
          (loop $il
            (br_if $iout (i32.ge_s (local.get $j) (local.get $n)))
            (if (local.get $k)
              (then (local.set $s (i32.add (local.get $s) (i32.const 2))))
              (else (local.set $s (i32.add (local.get $s) (i32.const 1)))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $il)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  (func $leave (export "leave") (param $k i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (br_if $out (local.get $k))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s)))
