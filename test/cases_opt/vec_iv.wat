;; Loops whose lanes hold their counter (opt.c vectorize_one): a vector of
;; the counter's values in the iterations the lanes stand for, made on entry
;; a lane a step apart and stepped a copy at a time.  A loop that touches no
;; memory vectorises for what it carries round, one element an iteration in
;; its lanes: gobench/bigstruct's sum of (i & 3) + 14, a counter that counts
;; along unread in the loop, whose vector goes; a product xor'd in, of a
;; counter stepping by 3 from 7; a counter running down; 64-bit lanes; two
;; counters in one term; and with memory, a store of 3i + 1 at a[i].  Not a
;; counter where the lanes run against the iterations (a store loop running
;; down), two elements an iteration, lanes narrower than the counter, nor a
;; float sum, which is not reassociable.  Each at trip counts around the
;; vector's and its copies'.  Made by test/gen_veciv.py.
;;
;; shape: rsum \sand\.16b\s=5
;; shape: rsum \sadd\.4s\s=20
;; shape: rxor3 \smul\.4s\s=5
;; shape: rdown \sorr\.16b\s=5
;; shape: rsum64 \seor\.16b\s=5
;; shape: rtwo \seor\.16b\s=5
;; shape: fill \smla\.4s\s=5
;; shape: fillback \\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s=0
;; shape: fillpairs \\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s=0
;; shape: fillbytes \\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s=0
;; shape: fsum \\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s=0
;;
;; run: rsum 0 => i32:0
;; run: rsum 1 => i32:1014
;; run: rsum 3 => i32:3045
;; run: rsum 4 => i32:4062
;; run: rsum 5 => i32:5076
;; run: rsum 15 => i32:15231
;; run: rsum 16 => i32:16248
;; run: rsum 17 => i32:17262
;; run: rsum 31 => i32:31479
;; run: rsum 63 => i32:63975
;; run: rsum 64 => i32:64992
;; run: rsum 65 => i32:66006
;; run: rsum 1000 => i32:1015500
;; run: rxor3 0 => i32:0
;; run: rxor3 1 => i32:0
;; run: rxor3 3 => i32:0
;; run: rxor3 4 => i32:0
;; run: rxor3 5 => i32:0
;; run: rxor3 15 => i32:80
;; run: rxor3 16 => i32:80
;; run: rxor3 17 => i32:0
;; run: rxor3 31 => i32:192
;; run: rxor3 63 => i32:64
;; run: rxor3 64 => i32:64
;; run: rxor3 65 => i32:256
;; run: rxor3 1000 => i32:1288
;; run: rxor3 3007 => i32:448
;; run: rdown 0 => i32:0
;; run: rdown 1 => i32:1
;; run: rdown 3 => i32:7
;; run: rdown 4 => i32:12
;; run: rdown 5 => i32:17
;; run: rdown 15 => i32:127
;; run: rdown 16 => i32:144
;; run: rdown 17 => i32:161
;; run: rdown 31 => i32:511
;; run: rdown 63 => i32:2047
;; run: rdown 64 => i32:2112
;; run: rdown 65 => i32:2177
;; run: rdown 1000 => i32:501000
;; run: rsum64 0 => i64:0
;; run: rsum64 1 => i64:85
;; run: rsum64 3 => i64:256
;; run: rsum64 4 => i64:342
;; run: rsum64 5 => i64:423
;; run: rsum64 15 => i64:1310
;; run: rsum64 16 => i64:1400
;; run: rsum64 17 => i64:1469
;; run: rsum64 31 => i64:2470
;; run: rsum64 63 => i64:6006
;; run: rsum64 64 => i64:6112
;; run: rsum64 65 => i64:6133
;; run: rsum64 1000 => i64:501164
;; run: rtwo 0 => i32:0
;; run: rtwo 1 => i32:100
;; run: rtwo 3 => i32:309
;; run: rtwo 4 => i32:414
;; run: rtwo 5 => i32:518
;; run: rtwo 15 => i32:1707
;; run: rtwo 16 => i32:1848
;; run: rtwo 17 => i32:1996
;; run: rtwo 31 => i32:4019
;; run: rtwo 63 => i32:10243
;; run: rtwo 64 => i32:10464
;; run: rtwo 65 => i32:10628
;; run: rtwo 1000 => i32:1078508
;; run: fill 0 => i32:0
;; run: fill 1 => i32:1
;; run: fill 3 => i32:1092
;; run: fill 4 => i32:33862
;; run: fill 5 => i32:1049735
;; run: fill 15 => i32:-763180586
;; run: fill 16 => i32:2111205656
;; run: fill 17 => i32:1022865945
;; run: fill 31 => i32:1386585070
;; run: fill 63 => i32:1466449950
;; run: fill 64 => i32:-1784691616
;; run: fill 65 => i32:509134945
;; run: fill 1000 => i32:1387297884
;; run: fillback 0 => i32:0
;; run: fillback 1 => i32:0
;; run: fillback 3 => i32:33
;; run: fillback 4 => i32:1026
;; run: fillback 5 => i32:31810
;; run: fillback 15 => i32:-153277209
;; run: fillback 16 => i32:-456626168
;; run: fillback 17 => i32:-1270509304
;; run: fillback 31 => i32:-1519788561
;; run: fillback 63 => i32:-346013697
;; run: fillback 64 => i32:-2136489952
;; run: fillback 65 => i32:-1806679008
;; run: fillback 1000 => i32:562641396
;; run: fillpairs 0 => i32:0
;; run: fillpairs 1 => i32:0
;; run: fillpairs 3 => i32:30816
;; run: fillpairs 4 => i32:29614272
;; run: fillpairs 5 => i32:-1605455552
;; run: fillpairs 15 => i32:717182240
;; run: fillpairs 16 => i32:2017365760
;; run: fillpairs 17 => i32:1658245376
;; run: fillpairs 31 => i32:38617632
;; run: fillpairs 63 => i32:882474016
;; run: fillpairs 64 => i32:1948974080
;; run: fillpairs 65 => i32:358351872
;; run: fillpairs 1000 => i32:-915593856
;; run: fillbytes 0 => i32:0
;; run: fillbytes 1 => i32:0
;; run: fillbytes 3 => i32:0
;; run: fillbytes 4 => i32:168364039
;; run: fillbytes 5 => i32:168364039
;; run: fillbytes 15 => i32:1905043371
;; run: fillbytes 16 => i32:-702717496
;; run: fillbytes 17 => i32:-702717496
;; run: fillbytes 31 => i32:-2062491917
;; run: fillbytes 63 => i32:561327491
;; run: fillbytes 64 => i32:1400227616
;; run: fillbytes 65 => i32:1400227616
;; run: fillbytes 1000 => i32:-1640738732
;; run: fsum 0 => f64:0x0000000000000000
;; run: fsum 1 => f64:0x0000000000000000
;; run: fsum 3 => f64:0x3ff0000000000000
;; run: fsum 4 => f64:0x4000000000000000
;; run: fsum 5 => f64:0x400aaaaaaaaaaaaa
;; run: fsum 15 => f64:0x4041800000000000
;; run: fsum 16 => f64:0x4044000000000000
;; run: fsum 17 => f64:0x4046aaaaaaaaaaab
;; run: fsum 31 => f64:0x4063600000000000
;; run: fsum 63 => f64:0x4084580000000000
;; run: fsum 64 => f64:0x4085000000000000
;; run: fsum 65 => f64:0x4085aaaaaaaaaaab
;; run: fsum 1000 => f64:0x4104532000000000

(module
  (memory 1)
  (func $h (param $a i32) (param $n i32) (result i32)
    (local $r i32)
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $r (i32.add (i32.mul (local.get $r) (i32.const 31)) (i32.load (local.get $a))))
        (local.set $a (i32.add (local.get $a) (i32.const 4)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $r))
  (func $rsum (export "rsum") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $t i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.add (i32.and (local.get $i) (i32.const 3)) (i32.const 14))))
        (local.set $t (i32.add (local.get $t) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $t) (i32.const 1000))))
  (func $rxor3 (export "rxor3") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (local.set $i (i32.const 7))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.xor (local.get $s) (i32.mul (local.get $i) (i32.const 5))))
        (local.set $i (i32.add (local.get $i) (i32.const 3)))
        (br $l)))
    (local.get $s))
  (func $rdown (export "rdown") (param $n i32) (result i32)
    (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.or (local.get $n) (i32.const 1))))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $rsum64 (export "rsum64") (param $n i64) (result i64)
    (local $i i64) (local $s i64)
    (block $done
      (loop $l
        (br_if $done (i64.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i64.add (local.get $s) (i64.xor (local.get $i) (i64.const 0x55))))
        (local.set $i (i64.add (local.get $i) (i64.const 1)))
        (br $l)))
    (local.get $s))
  (func $rtwo (export "rtwo") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 100))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.xor (local.get $i) (local.get $j))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (br $l)))
    (local.get $s))
  (func $fill (export "fill") (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2))
                   (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (local.get $n)))
  (func $fillback (export "fillback") (param $n i32) (result i32)
    (local $i i32)
    (local.set $i (local.get $n))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2)) (local.get $i))
        (br $l)))
    (call $h (i32.const 1024) (local.get $n)))
  (func $fillpairs (export "fillpairs") (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 3)) (local.get $i))
        (i32.store offset=1028 (i32.shl (local.get $i) (i32.const 3)) (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (i32.shl (local.get $n) (i32.const 1))))
  (func $fillbytes (export "fillbytes") (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store8 offset=1024 (local.get $i) (i32.add (local.get $i) (i32.const 7)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (i32.shr_u (local.get $n) (i32.const 2))))
  (func $fsum (export "fsum") (param $n i32) (result f64)
    (local $i i32) (local $s f64)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (f64.add (local.get $s) (f64.div (f64.convert_i32_u (local.get $i)) (f64.const 3))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
)
