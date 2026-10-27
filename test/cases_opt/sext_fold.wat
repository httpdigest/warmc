;; An i64 that is already the sign extension of its low 32 bits needs no
;; extending again (opt.c ir_sext32): a signed 32-bit load, a narrower
;; load, an extension, a constant in range, an and, or or xor of two such,
;; a select of two, and a block parameter every edge hands one, a loop's
;; too, which Go's int32 kept in an i64 and extended at each use is: in
;; stdgo/multiret's min and max loop, two sxtw on the chain each element.
;; Both ext32s of such a value and extends of its wrap go.  Not of a 64-bit
;; load, an unsigned 32-bit one, a constant past 2^31, an and, or, xor or
;; select with one operand that is not, nor a parameter an edge hands
;; anything else.  Answers worked out in Python.
;;
;; shape: minmax \ssxtw\s=0
;; shape: rewiden \ssxtw\s=0
;; shape: bits \ssxtw\s=0
;; shape: sel \ssxtw\s=0
;; shape: keep64 \ssxtw\s=1
;; shape: keepu \sldrsw\s=1
;; shape: mixed \ssxtw\s=1
;; shape: narrow \ssxtw\s=0
;; shape: fromext \ssxtw\s=2
;;
;; run: minmax 1 => i64:0
;; run: minmax 2 => i64:606348324
;; run: minmax 5 => i64:3688618972
;; run: minmax 16 => i64:3755991008
;; run: minmax 40 => i64:4227595260
;; run: rewiden 0 => i64:-122722787
;; run: keep64 0 => i64:-122722787
;; run: keepu 0 => i64:-122722784
;; run: bits 0 12 0 => i64:1611350537
;; run: bits 0 12 1 => i64:-54528355
;; run: bits 0 12 2 => i64:-1665878892
;; run: sel 0 12 0 => i64:1679544969
;; run: sel 0 12 1 => i64:-122722787
;; run: bits 0 52 0 => i64:-930930159
;; run: bits 0 52 1 => i64:-55607555
;; run: bits 0 52 2 => i64:875322604
;; run: sel 0 52 0 => i64:-863814927
;; run: sel 0 52 1 => i64:-122722787
;; run: rewiden 4 => i64:483625537
;; run: keep64 4 => i64:483625537
;; run: keepu 4 => i64:483625540
;; run: bits 4 12 0 => i64:68387329
;; run: bits 4 12 1 => i64:2094783177
;; run: bits 4 12 2 => i64:2026395848
;; run: sel 4 12 0 => i64:1679544969
;; run: sel 4 12 1 => i64:483625537
;; run: bits 4 52 0 => i64:209914433
;; run: bits 4 52 1 => i64:-590103823
;; run: bits 4 52 2 => i64:-800018256
;; run: sel 4 52 0 => i64:-863814927
;; run: sel 4 52 1 => i64:483625537
;; run: rewiden 8 => i64:1089973861
;; run: keep64 8 => i64:1089973861
;; run: keepu 8 => i64:1089973864
;; run: bits 8 12 0 => i64:1075020289
;; run: bits 8 12 1 => i64:1694498541
;; run: bits 8 12 2 => i64:619478252
;; run: sel 8 12 0 => i64:1679544969
;; run: sel 8 12 1 => i64:1089973861
;; run: bits 8 52 0 => i64:1082337889
;; run: bits 8 52 1 => i64:-856178955
;; run: bits 8 52 2 => i64:-1938516844
;; run: sel 8 52 0 => i64:-863814927
;; run: sel 8 52 1 => i64:1089973861
;; run: rewiden 100 => i64:2083777185
;; run: keep64 100 => i64:2083777185
;; run: keepu 100 => i64:2083777188
;; run: bits 100 12 0 => i64:1679016577
;; run: bits 100 12 1 => i64:2084305577
;; run: bits 100 12 2 => i64:405289000
;; run: sel 100 12 0 => i64:1679544969
;; run: sel 100 12 1 => i64:2083777185
;; run: bits 100 52 0 => i64:1275275937
;; run: bits 100 52 1 => i64:-55313679
;; run: bits 100 52 2 => i64:-1330589616
;; run: sel 100 52 0 => i64:-863814927
;; run: sel 100 52 1 => i64:2083777185
;; run: rewiden 200 => i64:12021285
;; run: keep64 200 => i64:12021285
;; run: keepu 200 => i64:12021288
;; run: bits 200 12 0 => i64:1262081
;; run: bits 200 12 1 => i64:1690304173
;; run: bits 200 12 2 => i64:1689042092
;; run: sel 200 12 0 => i64:1679544969
;; run: sel 200 12 1 => i64:12021285
;; run: bits 200 52 0 => i64:8596001
;; run: bits 200 52 1 => i64:-860389643
;; run: bits 200 52 2 => i64:-868985644
;; run: sel 200 52 0 => i64:-863814927
;; run: sel 200 52 1 => i64:12021285
;; run: keepc 0 => i64:-2147483648
;; run: keepc 1 => i64:-2147483647
;; run: keepc -1 => i64:2147483647
;; run: keepc 2147483647 => i64:-1
;; run: mixed 0 0 => i64:-122722787
;; run: mixed 0 8 => i64:1089973861
;; run: mixed 0 100 => i64:2083777185
;; run: mixed 1 0 => i64:-122722787
;; run: mixed 1 8 => i64:1089973861
;; run: mixed 1 100 => i64:2083777185
;; run: mixed 2 0 => i64:483625537
;; run: mixed 2 8 => i64:483625537
;; run: mixed 2 100 => i64:483625537
;; run: mixed 3 0 => i64:483625537
;; run: mixed 3 8 => i64:483625537
;; run: mixed 3 100 => i64:483625537
;; run: mixed 7 0 => i64:-1402791215
;; run: mixed 7 8 => i64:-1402791215
;; run: mixed 7 100 => i64:-1402791215
;; run: bitsneg 0 4 => i64:-461575076
;; run: selneg 0 4 0 => i64:483625537
;; run: selneg 0 4 1 => i64:-122722787
;; run: bitsneg 0 52 => i64:875322604
;; run: selneg 0 52 0 => i64:-863814927
;; run: selneg 0 52 1 => i64:-122722787
;; run: bitsneg 8 4 => i64:1545872420
;; run: selneg 8 4 0 => i64:483625537
;; run: selneg 8 4 1 => i64:1089973861
;; run: bitsneg 8 52 => i64:-1938516844
;; run: selneg 8 52 0 => i64:-863814927
;; run: selneg 8 52 1 => i64:1089973861
;; run: bitsneg 100 4 => i64:1625317600
;; run: selneg 100 4 0 => i64:483625537
;; run: selneg 100 4 1 => i64:2083777185
;; run: bitsneg 100 52 => i64:-1330589616
;; run: selneg 100 52 0 => i64:-863814927
;; run: selneg 100 52 1 => i64:2083777185
;; run: selc 0 0 => i64:-2147483648
;; run: narrow 0 6 0 => i64:7379
;; run: narrow 0 51 0 => i64:61864
;; run: selc 0 1 => i64:-122722787
;; run: narrow 0 6 1 => i64:29
;; run: narrow 0 51 1 => i64:29
;; run: selc 8 0 => i64:-2147483648
;; run: narrow 8 6 0 => i64:7379
;; run: narrow 8 51 0 => i64:61864
;; run: selc 8 1 => i64:1089973861
;; run: narrow 8 6 1 => i64:101
;; run: narrow 8 51 1 => i64:101
;; run: selc 100 0 => i64:-2147483648
;; run: narrow 100 6 0 => i64:7379
;; run: narrow 100 51 0 => i64:61864
;; run: selc 100 1 => i64:2083777185
;; run: narrow 100 6 1 => i64:161
;; run: narrow 100 51 1 => i64:161
;; run: fromext 0 3 0 => i64:3
;; run: fromext 0 3 1 => i64:0
;; run: fromext 0 -1 0 => i64:-1
;; run: fromext 0 -1 1 => i64:0
;; run: fromext 5 3 0 => i64:3
;; run: fromext 5 3 1 => i64:5
;; run: fromext 5 -1 0 => i64:-1
;; run: fromext 5 -1 1 => i64:5
;; run: fromext -7 3 0 => i64:3
;; run: fromext -7 3 1 => i64:-7
;; run: fromext -7 -1 0 => i64:-1
;; run: fromext -7 -1 1 => i64:-7
;; run: fromext 2147483647 3 0 => i64:3
;; run: fromext 2147483647 3 1 => i64:2147483647
;; run: fromext 2147483647 -1 0 => i64:-1
;; run: fromext 2147483647 -1 1 => i64:2147483647
;; run: fromext -2147483648 3 0 => i64:3
;; run: fromext -2147483648 3 1 => i64:-2147483648
;; run: fromext -2147483648 -1 0 => i64:-1
;; run: fromext -2147483648 -1 1 => i64:-2147483648

(module
  (memory 1)
  (data (i32.const 0) "\1d\66\af\f8\41\8a\d3\1c\65\ae\f7\40\89\d2\1b\64\ad\f6\3f\88\d1\1a\63\ac\f5\3e\87\d0\19\62\ab\f4\3d\86\cf\18\61\aa\f3\3c\85\ce\17\60\a9\f2\3b\84\cd\16\5f\a8\f1\3a\83\cc\15\5e\a7\f0\39\82\cb\14\5d\a6\ef\38\81\ca\13\5c\a5\ee\37\80\c9\12\5b\a4\ed\36\7f\c8\11\5a\a3\ec\35\7e\c7\10\59\a2\eb\34\7d\c6\0f\58\a1\ea\33\7c\c5\0e\57\a0\e9\32\7b\c4\0d\56\9f\e8\31\7a\c3\0c\55\9e\e7\30\79\c2\0b\54\9d\e6\2f\78\c1\0a\53\9c\e5\2e\77\c0\09\52\9b\e4\2d\76\bf\08\51\9a\e3\2c\75\be\07\50\99\e2\2b\74\bd\06\4f\98\e1\2a\73\bc\05\4e\97\e0\29\72\bb\04\4d\96\df\28\71\ba\03\4c\95\de\27\70\b9\02\4b\94\dd\26\6f\b8\01\4a\93\dc\25\6e\b7\00\49\92\db\24\6d\b6\ff\48\91\da\23\6c\b5\fe\47\90\d9\22\6b\b4\fd\46\8f\d8\21\6a\b3\fc\45\8e\d7\20\69\b2\fb\44\8d\d6\1f\68\b1\fa\43\8c\d5\1e\67\b0\f9\42\8b\d4")
  (func $minmax (export "minmax") (param $n i32) (result i64)
    (local $i i32) (local $lo i64) (local $hi i64) (local $v i64)
    (local.set $lo (i64.load32_s (i32.const 0)))
    (local.set $hi (local.get $lo))
    (loop $l
      (local.set $v (i64.load32_s (i32.shl (local.get $i) (i32.const 2))))
      (if (i64.lt_s (local.get $v) (i64.extend32_s (local.get $lo)))
        (then (local.set $lo (local.get $v))))
      (if (i64.gt_s (local.get $v) (i64.extend32_s (local.get $hi)))
        (then (local.set $hi (local.get $v))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i64.sub (i64.extend32_s (local.get $hi)) (i64.extend32_s (local.get $lo))))
  (func $rewiden (export "rewiden") (param $a i32) (result i64)
    (i64.extend_i32_s (i32.wrap_i64 (i64.load32_s (local.get $a)))))
  (func $bits (export "bits") (param $a i32) (param $b i32) (param $k i32) (result i64)
    (local $x i64) (local $y i64) (local $r i64)
    (local.set $x (i64.load32_s (local.get $a)))
    (local.set $y (i64.load32_s (local.get $b)))
    (local.set $r (i64.xor (local.get $x) (local.get $y)))
    (if (i32.eqz (local.get $k)) (then (local.set $r (i64.and (local.get $x) (local.get $y)))))
    (if (i32.eq (local.get $k) (i32.const 1)) (then (local.set $r (i64.or (local.get $x) (local.get $y)))))
    (i64.extend_i32_s (i32.wrap_i64 (local.get $r))))
  (func $sel (export "sel") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (i64.extend32_s (select (i64.load32_s (local.get $a)) (i64.load32_s (local.get $b)) (local.get $c))))
  (func $keep64 (export "keep64") (param $a i32) (result i64)
    (i64.extend32_s (i64.load (local.get $a))))
  (func $keepu (export "keepu") (param $a i32) (result i64)
    (i64.add (i64.extend32_s (i64.load32_u (local.get $a))) (i64.const 3)))
  (func $keepc (export "keepc") (param $c i32) (result i64)
    (i64.extend32_s (i64.add (i64.const 0x80000000) (i64.extend_i32_s (local.get $c)))))
  (func $mixed (export "mixed") (param $n i32) (param $a i32) (result i64)
    (local $x i64) (local $i i32)
    (local.set $x (i64.load (local.get $a)))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (local.set $x (i64.load32_s (i32.shl (local.get $i) (i32.const 2))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.extend32_s (local.get $x)))
  (func $bitsneg (export "bitsneg") (param $a i32) (param $b i32) (result i64)
    (i64.extend32_s (i64.xor (i64.load32_s (local.get $a)) (i64.load (local.get $b)))))
  (func $selneg (export "selneg") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (i64.extend32_s (select (i64.load32_s (local.get $a)) (i64.load (local.get $b)) (local.get $c))))
  (func $selc (export "selc") (param $a i32) (param $c i32) (result i64)
    (i64.extend32_s (select (i64.load32_s (local.get $a)) (i64.const 0x80000000) (local.get $c))))
  (func $narrow (export "narrow") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (i64.extend32_s (select (i64.load8_u (local.get $a)) (i64.load16_u (local.get $b)) (local.get $c))))
  (func $fromext (export "fromext") (param $x i32) (param $y i32) (param $c i32) (result i64)
    (i64.extend32_s (select (i64.extend_i32_s (local.get $x)) (i64.extend_i32_s (local.get $y))
                            (local.get $c))))
)
