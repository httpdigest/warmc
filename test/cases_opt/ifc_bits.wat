;; If-conversion's latency rule (opt.c ifc_try): a select must not wait on
;; its condition much longer than on the values it picks, since a guessed
;; branch would not.  In a loop, a test of one bit of a value the loop
;; computes takes the select however late the bit is -- hashed bits, which
;; a branch guesses wrong half the time (hsbench/bits' testBit 17 of a
;; splitmix value), against 0 or the bit, either way round (ifc_one_bit).
;; A comparison of the same value keeps its branch, as do a test of two
;; bits, a bit of a loaded word hashed, a loaded bit picking the hash, and
;; the bit tested once outside a loop.  Made by test/gen_ifcbits.py.
;;
;; shape: hashbit \scsel\s=1
;; shape: hashbit0 \scsel\s=1
;; shape: hashbiteq \scsel\s=1
;; shape: hashbitne \scsel\s=1
;; shape: hashbiteq0 \scsel\s=1
;; shape: hashcmp \scsel\s=0
;; shape: twobits \scsel\s=0
;; shape: loadedbit \scsel\s=0
;; shape: loadedlate \scsel\s=0
;; shape: straightbit \scsel\s=0
;;
;; run: hashbit 0 => i32:0
;; run: hashbit 1 => i32:54321
;; run: hashbit 7 => i32:212343
;; run: hashbit 100 => i32:3291324
;; run: hashbit 1000 => i32:32913240
;; run: hashbit0 0 => i32:0
;; run: hashbit0 1 => i32:12345
;; run: hashbit0 7 => i32:254319
;; run: hashbit0 100 => i32:3249348
;; run: hashbit0 1000 => i32:33836712
;; run: hashbiteq 0 => i32:0
;; run: hashbiteq 1 => i32:54321
;; run: hashbiteq 7 => i32:212343
;; run: hashbiteq 100 => i32:3291324
;; run: hashbiteq 1000 => i32:32913240
;; run: hashbitne 0 => i32:0
;; run: hashbitne 1 => i32:12345
;; run: hashbitne 7 => i32:86415
;; run: hashbitne 100 => i32:3039468
;; run: hashbitne 1000 => i32:32997192
;; run: hashbiteq0 0 => i32:0
;; run: hashbiteq0 1 => i32:12345
;; run: hashbiteq0 7 => i32:338271
;; run: hashbiteq0 100 => i32:3207372
;; run: hashbiteq0 1000 => i32:33836712
;; run: hashcmp 0 => i32:0
;; run: hashcmp 1 => i32:12345
;; run: hashcmp 7 => i32:338271
;; run: hashcmp 100 => i32:4172820
;; run: hashcmp 1000 => i32:43827000
;; run: twobits 0 => i32:0
;; run: twobits 1 => i32:54321
;; run: twobits 7 => i32:212343
;; run: twobits 100 => i32:2451804
;; run: twobits 1000 => i32:22880976
;; run: loadedbit 0 => i32:0
;; run: loadedbit 1 => i32:54321
;; run: loadedbit 7 => i32:212343
;; run: loadedbit 100 => i32:3459228
;; run: loadedbit 1000 => i32:33920664
;; run: loadedlate 0 => i32:0
;; run: loadedlate 1 => i32:12345
;; run: loadedlate 7 => i32:312761531
;; run: loadedlate 100 => i32:-1934484733
;; run: loadedlate 1000 => i32:1035261860
;; run: straightbit 0 => i32:54321
;; run: straightbit 1 => i32:12345
;; run: straightbit 7 => i32:12345
;; run: straightbit 100 => i32:54321
;; run: straightbit 1000 => i32:54321

(module
  (memory 1)
  (data (i32.const 0) "\00\00\00\00\07\00\00\00\0e\00\00\00\15\00\00\00")
  (func $hashbit (export "hashbit") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $hashbit0 (export "hashbit0") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.eqz (i32.and (local.get $z) (i32.const 0x400))) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $hashbiteq (export "hashbiteq") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.eq (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0x20000)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $hashbitne (export "hashbitne") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x8)) (i32.const 0x8)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $hashbiteq0 (export "hashbiteq0") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.eq (i32.and (local.get $z) (i32.const 0x80000000)) (i32.const 0)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $hashcmp (export "hashcmp") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.lt_u (local.get $z) (i32.const 0x40000000)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $twobits (export "twobits") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x30000)) (i32.const 0)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $loadedbit (export "loadedbit") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (i32.xor (local.get $i) (i32.load (i32.shl (i32.and (local.get $i) (i32.const 3)) (i32.const 2)))) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0)) (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $loadedlate (export "loadedlate") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
        (local.set $s (i32.add (local.get $s)
          (if (result i32)
              (i32.ne (i32.and (i32.load (i32.shl (i32.and (local.get $i) (i32.const 3)) (i32.const 2)))
                               (i32.const 4)) (i32.const 0))
            (then (local.get $z)) (else (i32.const 12345)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $straightbit (export "straightbit") (param $n i32) (result i32)
    (local $z i32)
    (local.set $z (i32.mul (local.get $n) (i32.const 0x9e3779b1)))
    (local.set $z (i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b)))
    (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0))
      (then (i32.const 12345)) (else (i32.const 54321))))
)
