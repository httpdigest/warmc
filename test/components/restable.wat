;; The three canonical built-ins over a resource, run rather than declared.
;;
;; [[resources]] next door creates resource.new, resource.drop and
;; resource.rep and stops there, so it says whether they are *emitted* and
;; nothing about what they do.  This one calls them, and is arranged so that
;; each way of getting the table wrong comes out as a different number rather
;; than as a crash:
;;
;;   handing out index 0     -- the specification reserves it, and a table that
;;                              does not answers 0 + 20 + ... instead of 1 + 20
;;   never reusing a slot    -- the third new answers 3, not 1, so the third
;;                              digit group moves by 2000
;;   losing the rep          -- rep(b) is 9 because that is what new(9) stored,
;;                              and a table that returns the handle instead
;;                              answers 2
;;   not running the dtor    -- `seen` stays 0 and the answer loses 70000
;;   running it with the
;;   handle rather than the
;;   representation          -- `seen` is 1 rather than 7
;;
;; So 71921 is the only answer that means all five are right, and wasmtime
;; computes it from the same bytes.

(component
  (core module $store
    (global $seen (mut i32) (i32.const 0))
    (global $seen2 (mut i32) (i32.const 0))
    (func (export "dtor") (param $rep i32)
      (global.set $seen (i32.add (global.get $seen) (local.get $rep))))
    (func (export "dtor2") (param $rep i32)
      (global.set $seen2 (i32.add (global.get $seen2) (local.get $rep))))
    (func (export "seen") (result i32) (global.get $seen))
    (func (export "seen2") (result i32) (global.get $seen2)))
  (core instance $s (instantiate $store))

  (type $r (resource (rep i32) (dtor (core func $s "dtor"))))
  (core func $new (canon resource.new $r))
  (core func $drop (canon resource.drop $r))
  (core func $rep (canon resource.rep $r))

  ;; A second resource, so that the two index spaces have to stay apart.  A
  ;; table shared between them would hand the second type an index the first
  ;; already holds, and $stress reads every representation back to notice.
  (type $r2 (resource (rep i32) (dtor (core func $s "dtor2"))))
  (core func $new2 (canon resource.new $r2))
  (core func $drop2 (canon resource.drop $r2))
  (core func $rep2 (canon resource.rep $r2))

  (core module $use
    (import "res" "new" (func $new (param i32) (result i32)))
    (import "res" "rep" (func $rep (param i32) (result i32)))
    (import "res" "drop" (func $drop (param i32)))
    (import "res" "seen" (func $seen (result i32)))
    (func (export "run") (result i32)
      (local $a i32) (local $b i32) (local $c i32)
      (local.set $a (call $new (i32.const 7)))
      (local.set $b (call $new (i32.const 9)))
      (call $drop (local.get $a))
      (local.set $c (call $new (i32.const 11)))
      (i32.add
        (i32.add (local.get $a)
                 (i32.mul (local.get $b) (i32.const 10)))
        (i32.add
          (i32.mul (call $rep (local.get $b)) (i32.const 100))
          (i32.add (i32.mul (local.get $c) (i32.const 1000))
                   (i32.mul (call $seen) (i32.const 10000)))))))

  (core module $stress
    (import "res" "new" (func $new (param i32) (result i32)))
    (import "res" "rep" (func $rep (param i32) (result i32)))
    (import "res" "drop" (func $drop (param i32)))
    (import "res" "new2" (func $new2 (param i32) (result i32)))
    (import "res" "rep2" (func $rep2 (param i32) (result i32)))
    (import "res" "drop2" (func $drop2 (param i32)))
    (import "res" "seen" (func $seen (result i32)))
    (import "res" "seen2" (func $seen2 (result i32)))
    (memory 1)

    ;; Sixty live handles of one type and sixty of another, which is well past
    ;; the eight a table starts with, so every answer below is also an answer
    ;; about growing one while its entries stay where they were.
    (func (export "stress") (result i32)
      (local $i i32) (local $acc i32) (local $h i32)

      (local.set $i (i32.const 0))
      (loop $mk
        (i32.store (i32.mul (local.get $i) (i32.const 4))
                   (call $new (i32.add (i32.mul (local.get $i) (i32.const 3))
                                       (i32.const 1))))
        (i32.store offset=1024 (i32.mul (local.get $i) (i32.const 4))
                   (call $new2 (i32.add (local.get $i) (i32.const 1000))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $mk (i32.lt_u (local.get $i) (i32.const 60))))

      ;; Every representation still reads back, from both tables, after all
      ;; the growing is done.
      (local.set $i (i32.const 0))
      (loop $rd
        (local.set $acc (i32.add (local.get $acc)
          (i32.eq (call $rep (i32.load (i32.mul (local.get $i) (i32.const 4))))
                  (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1)))))
        (local.set $acc (i32.add (local.get $acc)
          (i32.eq (call $rep2 (i32.load offset=1024
                                        (i32.mul (local.get $i) (i32.const 4))))
                  (i32.add (local.get $i) (i32.const 1000)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $rd (i32.lt_u (local.get $i) (i32.const 60))))

      ;; The two tables number their entries independently, so the nth handle
      ;; of each is the same number.  A shared table would not.
      (local.set $acc (i32.add (local.get $acc)
        (i32.mul (i32.const 1000)
                 (i32.eq (i32.load (i32.const 0))
                         (i32.load offset=1024 (i32.const 0))))))

      ;; Give half of one type back, then take the same number again: a free
      ;; list that is a stack hands them out in the reverse of the order they
      ;; were freed, and the representations must follow the new handles.
      (local.set $i (i32.const 0))
      (loop $dr
        (call $drop (i32.load (i32.mul (local.get $i) (i32.const 8))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $dr (i32.lt_u (local.get $i) (i32.const 30))))

      (local.set $i (i32.const 0))
      (loop $re
        (local.set $h (call $new (i32.add (local.get $i) (i32.const 500))))
        (local.set $acc (i32.add (local.get $acc)
          (i32.eq (call $rep (local.get $h))
                  (i32.add (local.get $i) (i32.const 500)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $re (i32.lt_u (local.get $i) (i32.const 30))))

      ;; 30 destructors ran, each on the representation its handle carried.
      ;; The handles given back are the ones at even positions, whose reps are
      ;; 6i+1 for i in 0..29, summing to 30 + 6*435.
      (i32.add (local.get $acc)
               (i32.mul (i32.const 100000)
                        (i32.eq (call $seen) (i32.const 2640))))))

  (alias core export $s "seen" (core func $seenf))
  (alias core export $s "seen2" (core func $seenf2))
  (core instance $env
    (export "new" (func $new))
    (export "rep" (func $rep))
    (export "drop" (func $drop))
    (export "new2" (func $new2))
    (export "rep2" (func $rep2))
    (export "drop2" (func $drop2))
    (export "seen" (func $seenf))
    (export "seen2" (func $seenf2)))
  (core instance $u (instantiate $use (with "res" (instance $env))))
  (core instance $st (instantiate $stress (with "res" (instance $env))))

  (alias core export $u "run" (core func $run))
  (func (export "run") (result u32) (canon lift (core func $run)))
  (alias core export $st "stress" (core func $stressf))
  (func (export "stress") (result u32) (canon lift (core func $stressf)))
)
