;; A bit reversal written as swaps of ever wider fields, masked and shifted,
;; is one reversal (opt.c bit_map, bitrev_src: rbit on arm64; x86-64 has no
;; such instruction and keeps the swaps): 32 and 64 bits, the last swap a
;; rotate or an or of two shifts, or left out or rotating further -- a
;; reversal rotated, rbit then ror -- after a byte swap, with the swaps the
;; other way round, their parts joined by add or xor, shifted by counts past
;; the width, and on a constant once inlined or unrolled.  Not when a mask
;; leaves a bit out, a swap is missing, two parts overlap (a reversal's bit
;; and another, either side of the or), or the parts come from two values
;; (the halves of two reversals).  Each result is xored with its own constant so that no two
;; functions are the same code.  Answers worked out in Python.
;;
;; shape: r32 rbit=1
;; shape: r32 ror=0
;; shape: r32or rbit=1
;; shape: r32or ror=0
;; shape: r64 rbit=1
;; shape: r64 ror=0
;; shape: r64rot rbit=1
;; shape: r64rot ror=1
;; shape: halfrev rbit=1
;; shape: halfrev ror=1
;; shape: bytebits rbit=1
;; shape: bytebits ror=0
;; shape: order rbit=1
;; shape: order ror=0
;; shape: r32add rbit=1
;; shape: r32add ror=0
;; shape: r32xor rbit=1
;; shape: r32xor ror=0
;; shape: bigshift rbit=1
;; shape: bigshift ror=0
;; shape: overlap1 rbit=1
;; shape: overlap2 rbit=1
;; shape: overlap1 ror=1
;; shape: overlap2 ror=1
;; shape: wrongmask rbit=0
;; shape: missing rbit=0
;; shape: overlap rbit=0
;; shape: twosrc rbit=0
;; stats -O1 a64: bitrevs=21
;; stats -O2 a64: bitrevs=25
;;
;; run: r32 0 => i32:0
;; run: r32 1 => i32:-2147483648
;; run: r32 -2147483648 => i32:1
;; run: r32 -1 => i32:-1
;; run: r32 305419896 => i32:510274632
;; run: r32 -559038737 => i32:-142756485
;; run: r32 252645135 => i32:-252645136
;; run: r32 1431655765 => i32:-1431655766
;; run: r32or 0 => i32:1
;; run: r32or 1 => i32:-2147483647
;; run: r32or -2147483648 => i32:0
;; run: r32or -1 => i32:-2
;; run: r32or 305419896 => i32:510274633
;; run: r32or -559038737 => i32:-142756486
;; run: r32or 252645135 => i32:-252645135
;; run: r32or 1431655765 => i32:-1431655765
;; run: r64 0 => i64:0
;; run: r64 1 => i64:-9223372036854775808
;; run: r64 -9223372036854775808 => i64:1
;; run: r64 -1 => i64:-1
;; run: r64 81985529216486895 => i64:-597899502893742976
;; run: r64 -2401053089206439923 => i64:-5760245399772351109
;; run: r64rot 0 => i64:0
;; run: r64rot 1 => i64:128
;; run: r64rot -9223372036854775808 => i64:256
;; run: r64rot -1 => i64:-1
;; run: r64rot 81985529216486895 => i64:-5488320151121788681
;; run: r64rot -2401053089206439923 => i64:1116703555042245552
;; run: halfrev 0 => i32:0
;; run: halfrev 1 => i32:32768
;; run: halfrev -2147483648 => i32:65536
;; run: halfrev -1 => i32:-1
;; run: halfrev 305419896 => i32:742923882
;; run: halfrev -559038737 => i32:-1250166915
;; run: halfrev 252645135 => i32:-252645136
;; run: halfrev 1431655765 => i32:-1431655766
;; run: bytebits 0 => i32:2
;; run: bytebits 1 => i32:-2147483646
;; run: bytebits -2147483648 => i32:3
;; run: bytebits -1 => i32:-3
;; run: bytebits 305419896 => i32:510274634
;; run: bytebits -559038737 => i32:-142756487
;; run: bytebits 252645135 => i32:-252645134
;; run: bytebits 1431655765 => i32:-1431655768
;; run: order 0 => i32:3
;; run: order 1 => i32:-2147483645
;; run: order -2147483648 => i32:2
;; run: order -1 => i32:-4
;; run: order 305419896 => i32:510274635
;; run: order -559038737 => i32:-142756488
;; run: order 252645135 => i32:-252645133
;; run: order 1431655765 => i32:-1431655767
;; run: r32add 0 => i32:4
;; run: r32add 1 => i32:-2147483644
;; run: r32add -2147483648 => i32:5
;; run: r32add -1 => i32:-5
;; run: r32add 305419896 => i32:510274636
;; run: r32add -559038737 => i32:-142756481
;; run: r32add 252645135 => i32:-252645132
;; run: r32add 1431655765 => i32:-1431655762
;; run: r32xor 0 => i32:5
;; run: r32xor 1 => i32:-2147483643
;; run: r32xor -2147483648 => i32:4
;; run: r32xor -1 => i32:-6
;; run: r32xor 305419896 => i32:510274637
;; run: r32xor -559038737 => i32:-142756482
;; run: r32xor 252645135 => i32:-252645131
;; run: r32xor 1431655765 => i32:-1431655761
;; run: bigshift 0 => i32:6
;; run: bigshift 1 => i32:-2147483642
;; run: bigshift -2147483648 => i32:7
;; run: bigshift -1 => i32:-7
;; run: bigshift 305419896 => i32:510274638
;; run: bigshift -559038737 => i32:-142756483
;; run: bigshift 252645135 => i32:-252645130
;; run: bigshift 1431655765 => i32:-1431655764
;; run: overlap1 0 => i32:0
;; run: overlap1 1 => i32:-2147483648
;; run: overlap1 -2147483648 => i32:1
;; run: overlap1 -1 => i32:-1
;; run: overlap1 305419896 => i32:510274632
;; run: overlap1 -559038737 => i32:-142756485
;; run: overlap1 252645135 => i32:-252645136
;; run: overlap1 1431655765 => i32:-1431655766
;; run: overlap2 0 => i32:7
;; run: overlap2 1 => i32:-2147483641
;; run: overlap2 -2147483648 => i32:6
;; run: overlap2 -1 => i32:-8
;; run: overlap2 305419896 => i32:510274639
;; run: overlap2 -559038737 => i32:-142756484
;; run: overlap2 252645135 => i32:-252645129
;; run: overlap2 1431655765 => i32:-1431655763
;; run: wrongmask 0 => i32:0
;; run: wrongmask 1 => i32:-2147483648
;; run: wrongmask -2147483648 => i32:1
;; run: wrongmask -1 => i32:-285212673
;; run: wrongmask 305419896 => i32:241839176
;; run: wrongmask -559038737 => i32:-427969157
;; run: wrongmask 252645135 => i32:-521080592
;; run: wrongmask 1431655765 => i32:-1431655766
;; run: missing 0 => i32:0
;; run: missing 1 => i32:134217728
;; run: missing -2147483648 => i32:16
;; run: missing -1 => i32:-1
;; run: missing 305419896 => i32:-509164924
;; run: missing -559038737 => i32:2144820151
;; run: missing 252645135 => i32:252645135
;; run: missing 1431655765 => i32:-1431655766
;; run: overlap 0 => i32:0
;; run: overlap 1 => i32:2
;; run: overlap -2147483648 => i32:1073741824
;; run: overlap -1 => i32:-1
;; run: overlap 305419896 => i32:557361588
;; run: overlap -559038737 => i32:-312574497
;; run: overlap 252645135 => i32:252645135
;; run: overlap 1431655765 => i32:-1431655766
;; run: twosrc 305419896 -1698898192 => i32:557361588
;; run: twosrc -1 0 => i32:1431655765
;; run: twosrc 0 -1 => i32:-1431655766
;; run: cin => i32:510274632
;; run: loopc => i32:457082072
;; run: twosrc2 305419896 -1698898192 => i32:259730504
;; run: twosrc2 -1 0 => i32:65535
;; run: twosrc2 0 -1 => i32:-65536
;; run: twosrc2 1 -2147483648 => i32:0

(module
  (func $r32 (export "r32") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.rotl (local.get $x) (i32.const 16)))
  (func $r32or (export "r32or") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.xor (i32.or (i32.shr_u (local.get $x) (i32.const 16)) (i32.shl (local.get $x) (i32.const 16))) (i32.const 1)))
  (func $r64 (export "r64") (param $x i64) (result i64)
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 1)) (i64.const 0x5555555555555555))
                         (i64.shl (i64.and (local.get $x) (i64.const 0x5555555555555555)) (i64.const 1))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 2)) (i64.const 0x3333333333333333))
                         (i64.shl (i64.and (local.get $x) (i64.const 0x3333333333333333)) (i64.const 2))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 4)) (i64.const 0xf0f0f0f0f0f0f0f))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xf0f0f0f0f0f0f0f)) (i64.const 4))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 8)) (i64.const 0xff00ff00ff00ff))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xff00ff00ff00ff)) (i64.const 8))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 16)) (i64.const 0xffff0000ffff))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xffff0000ffff)) (i64.const 16))))
    (i64.rotl (local.get $x) (i64.const 32)))
  (func $r64rot (export "r64rot") (param $x i64) (result i64)
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 1)) (i64.const 0x5555555555555555))
                         (i64.shl (i64.and (local.get $x) (i64.const 0x5555555555555555)) (i64.const 1))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 2)) (i64.const 0x3333333333333333))
                         (i64.shl (i64.and (local.get $x) (i64.const 0x3333333333333333)) (i64.const 2))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 4)) (i64.const 0xf0f0f0f0f0f0f0f))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xf0f0f0f0f0f0f0f)) (i64.const 4))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 8)) (i64.const 0xff00ff00ff00ff))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xff00ff00ff00ff)) (i64.const 8))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 16)) (i64.const 0xffff0000ffff))
                         (i64.shl (i64.and (local.get $x) (i64.const 0xffff0000ffff)) (i64.const 16))))
    (i64.rotl (local.get $x) (i64.const 40)))
  (func $halfrev (export "halfrev") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (local.get $x))
  (func $bytebits (export "bytebits") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.or (i32.shl (local.get $x) (i32.const 24))
                                  (i32.and (i32.shl (local.get $x) (i32.const 8)) (i32.const 0xff0000)))
                          (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00))
                                  (i32.shr_u (local.get $x) (i32.const 24)))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (i32.xor (local.get $x) (i32.const 2)))
  (func $order (export "order") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (i32.xor (i32.rotl (local.get $x) (i32.const 16)) (i32.const 3)))
  (func $r32add (export "r32add") (param $x i32) (result i32)
    (local.set $x (i32.add (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.add (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.add (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.add (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.xor (i32.rotl (local.get $x) (i32.const 16)) (i32.const 4)))
  (func $r32xor (export "r32xor") (param $x i32) (result i32)
    (local.set $x (i32.xor (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.xor (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.xor (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.xor (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.xor (i32.rotl (local.get $x) (i32.const 16)) (i32.const 5)))
  (func $bigshift (export "bigshift") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 33)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 33))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 34)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 34))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 36)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 36))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 40)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 40))))
    (i32.xor (i32.rotl (local.get $x) (i32.const 48)) (i32.const 6)))
  (func $wrongmask (export "wrongmask") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0e))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0e)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.rotl (local.get $x) (i32.const 16)))
  (func $missing (export "missing") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.rotl (local.get $x) (i32.const 16)))
  (func $overlap (export "overlap") (param $x i32) (result i32)
    (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
            (i32.shl (i32.and (local.get $x) (i32.const 0x55555557)) (i32.const 1))))
  (func $twosrc (export "twosrc") (param $x i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
            (i32.shl (i32.and (local.get $y) (i32.const 0x55555555)) (i32.const 1))))
  (func $overlap1 (export "overlap1") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.or (i32.rotl (local.get $x) (i32.const 16))
            (i32.shl (i32.and (local.get $x) (i32.const 1)) (i32.const 5))))
  (func $overlap2 (export "overlap2") (param $x i32) (result i32)
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x55555555))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x55555555)) (i32.const 1))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 2)) (i32.const 0x33333333))
                         (i32.shl (i32.and (local.get $x) (i32.const 0x33333333)) (i32.const 2))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 4)) (i32.const 0xf0f0f0f))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xf0f0f0f)) (i32.const 4))))
    (local.set $x (i32.or (i32.and (i32.shr_u (local.get $x) (i32.const 8)) (i32.const 0xff00ff))
                         (i32.shl (i32.and (local.get $x) (i32.const 0xff00ff)) (i32.const 8))))
    (i32.xor (i32.or (i32.shl (i32.and (local.get $x) (i32.const 1)) (i32.const 5))
            (i32.rotl (local.get $x) (i32.const 16))) (i32.const 7)))
  (func $twosrc2 (export "twosrc2") (param $x i32) (param $y i32) (result i32)
    (i32.or (i32.shr_u (call $halfrev (local.get $x)) (i32.const 16))
            (i32.shl (call $halfrev (local.get $y)) (i32.const 16))))
  (func $loopc (export "loopc") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $r32 (i32.add (local.get $i) (i32.const 0x12345678)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 3))))
    (local.get $acc))
  (func $cin (export "cin") (result i32)
    (call $r32 (i32.const 0x12345678)))
)
