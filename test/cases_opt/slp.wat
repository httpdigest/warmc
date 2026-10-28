;; A block's stores to adjacent addresses, as many as fill a vector, whose
;; values are alike lane by lane -- one operation of operands themselves
;; alike: loads of adjacent addresses, one value in every lane, constants --
;; are one vector computation and one vector store (opt.c pass_slp): a pair
;; of doubles scaled by k, four words added, subtracted, and'ed, or'd,
;; xor'd, copied or set to constants, a scalar added to four words, a
;; difference, quotient, minimum and maximum of doubles, a sum times k, k
;; times x plus a constant, a sum 31 adds deep, offsets past what ldr q
;; encodes, stores in either order, a load of a lane's address between, a
;; store into a lane's source after it is read; addresses as i32 adds, which
;; may wrap, and an offset, a pointer made once and adds in one group, past
;; the memory or wrapping to 0; loads in the block before the stores', also
;; two groups' with a store between, and a 2x4 product's rows in a loop, the
;; matrix loaded as vectors before it and each row's elements splatted once;
;; and a commutative operation's operands turned round in a lane, found by
;; which of them fits lane 0's first -- a factor, a loaded scalar, a load
;; through either of two pointers or from either of two memories, a product,
;; a sum of products.  A float splat only multiplies read, in its block and
;; beside a vector, is read by lane, an fmul by element (isel.c's vbyel);
;; not one an add reads too, one in another block, one beside another splat,
;; nor a float's four lanes taken as two doubles.  Not a sum 32 adds deep,
;; nor where a lane reads what another lane stored, nor a lane's value or
;; load is read elsewhere, nor where a memory.fill, a store or a load that
;; may touch a lane stands between, in the stores' block or the loads', nor
;; for lanes loaded in two blocks, nor one vector load for two loads of an
;; address with a store to it between, nor across a gap, a misaligned
;; offset, two pointers or two memories, nor a 64-bit add of a constant past
;; 2^32, nor a lane's add below lane 0's, which only the scalars trap at
;; where lane 0's wraps, nor where lane 0's address is made after another
;; lane's load, nor lanes loaded just after a call handed their address, or
;; it plus a constant, which wrote them one at a time, nor for loads
;; narrower than their lanes, one lane's alone, from two pointers or a
;; stride apart, nor products made in the block before, nor for stores
;; narrower than their values or of two widths, nor two constants a store
;; pair writes as cheaply, nor lanes of different operations or a difference
;; turned round in one, nor a 64-bit multiply, which has no vector
;; instruction.  At -O2 -fno-mem too, where a lane's load of what another
;; lane stored is not forwarded the value but read, after the store.  Each
;; function stores through $p and reads back through $q, the same address,
;; which forwarding cannot see.  Made by test/gen_slp.py, which works the
;; answers out on a model of the memory.
;;
;; shape: scale2 \sfmul\.2d\s=1
;; shape: scale2 \sfmul\s+d\d+=0
;; shape: scale2 \sldur\s+q\d+=1
;; shape: add4 \sadd\.4s\s=1
;; shape: copy4 \sldu?r\s+q\d+=1
;; shape: copy4 \sstu?r\s+q\d+=1
;; shape: consts4 \sstu?r\s+q\d+=1
;; shape: splat4 \sadd\.4s\s=1
;; shape: splat4 \sdup\.4s\s=1
;; shape: sub2 \sfsub\.2d\s=1
;; shape: swapped \sfmul\.2d\s=1
;; shape: chain4 \smul\.4s\s=1
;; shape: chain4 \sadd\.4s\s=1
;; shape: highoff \sfadd\.2d\s=1
;; shape: revorder \sfmul\.2d\s=1
;; shape: deep31 \sadd\.4s\s=31
;; shape: deep32 \sadd\.4s\s=0
;; shape: isub4 \ssub\.4s\s=1
;; shape: and4 \sand\.16b\s=1
;; shape: or4 \sorr\.16b\s=1
;; shape: xor4 \seor\.16b\s=1
;; shape: fdiv2 \sfdiv\.2d\s=1
;; shape: fmin2 \sfmin\.2d\s=1
;; shape: fmax2 \sfmax\.2d\s=1
;; shape: bytes4 \sadd\.4s\s=0
;; shape: twoload \sadd\.4s\s=0
;; shape: strided \sfadd\.2d\s=0
;; shape: crossblock \sfadd\.2d\s=1
;; shape: copyshared \sstu?r\s+q\d+=0
;; shape: mul2 \sstu?r\s+q\d+=0
;; shape: subswap \sfsub\.2d\s=0
;; shape: dupload \sfmul\.2d\s=1
;; shape: latewrite \sfmul\.2d\s=1
;; shape: axpy2 \sfmul\.2d\s=1
;; shape: axpy2 \sfadd\.2d\s=1
;; shape: swap4 \sadd\.4s\s=1
;; shape: swapped2 \sfmul\.2d\s=1
;; shape: swapsplat \sfmul\.2d\s=1
;; shape: swapptr \sfadd\.2d\s=1
;; shape: swapmul4 \smla\.4s\s=1
;; shape: swapdeep4 \sm(ul|la)\.4s\s=2
;; shape: swapsplat4 \smul\.4s\s=1
;; shape: twoptr2 \sstu?r\s+q\d+=0
;; shape: interleave \sfmul\.2d\s=1
;; shape: interleave \sfadd\.2d\s=1
;; shape: twogroups \sadd\.4s\s=1
;; shape: twogroups \sfmul\.2d\s=1
;; shape: nested \seor\.16b\s=1
;; shape: nested \sstu?r\s+q\d+=2
;; shape: addrc2 \sfmul\.2d\s=1
;; shape: addrmix \sstu?r\s+q\d+=1
;; shape: rowmat \sldr\s+q\d+=4
;; shape: rowmat \sfmul\.2d\s=20
;; shape: rowmat \sdup\.2d\s=0
;; shape: rowmat \sfmul\.2d\s+v\d+, v\d+, v\d+\[0\]=20
;; shape: rowmat \sfmul\s+d\d+=0
;; shape: rowmat \sldr\s+d\d+, \[x\d+, #0x1[0-3]=0
;; shape: ptrmix \sfmul\.2d\s=1
;; shape: swapmem \sfadd\.2d\s=1
;; shape: outer2 \sfadd\.2d\s=1
;; shape: outer2 \sfmul\.2d\s=1
;; shape: sharesplat \sfadd\.2d\s=2
;; shape: sharesplat \sdup\.2d\s=1
;; shape: sharedlanes \sldr\s+q\d+=1
;; shape: sharedlanes \sldr\s+d\d+, \[x\d+, #0x2c[08]\]=0
;; shape: byelem4 \sfmul\.4s\s+v\d+, v\d+, v\d+\[0\]=1
;; shape: byelem4 \sdup\.4s\s=0
;; shape: byelemadd \sdup\.2d\s=1
;; shape: byelemadd \sfmul\.2d\s+v\d+, v\d+, v\d+\[0\]=0
;; shape: byelemblock \sdup\.2d\s=1
;; shape: raw \sfmul\.2d\s=0
;; shape: multiuse \sfmul\.2d\s=0
;; shape: fill \sfmul\.2d\s=0
;; shape: loadpast \sfmul\.2d\s=0
;; shape: storeread \sfmul\.2d\s=0
;; shape: waw \sfmul\.2d\s=0
;; shape: misalign \sstu?r\s+q\d+=0
;; shape: mixedsize \sstu?r\s+q\d+=0
;; shape: gap \sstu?r\s+q\d+=0
;; shape: twoptr \sstu?r\s+q\d+=0
;; shape: narrow \sstu?r\s+q\d+=0
;; shape: consts2 \sstu?r\s+q\d+=0
;; shape: mixed \sf(mul|add)\.2d\s=0
;; shape: wrapdown \sstu?r\s+q\d+=0
;; shape: outerstore \sfadd\.2d\s=0
;; shape: opblock \sf(mul|add)\.2d\s=0
;; shape: lateaddr \sfmul\.2d\s=0
;; shape: twomem \sstu?r\s+q\d+=0
;; shape: narrowlane \sadd\.2d\s=0
;; shape: splitload \sfmul\.2d\s=0
;; shape: sharestore \sfadd\.2d\s=1
;; shape: sharestore \sfmul\.2d\s=1
;; shape: sharestore \sldr\s+q\d+=2
;; shape: wide64 \sstu?r\s+q\d+=0
;; shape: aftercall \sldu?r\s+q\d+=0
;; shape: aftercall2 \sldu?r\s+q\d+=0
;;
;; flags: -O2 -fno-mem
;;
;; run: scale2 256 256 6 => f64:0x402a400000000000
;; run: scale2 512 512 -8 => f64:0x400c000000000000
;; run: add4 0 0 => i32:-1441783716
;; run: add4 16 16 => i32:-56963492
;; run: copy4 0 0 => i32:821853602
;; run: copy4 8 8 => i32:-825276894
;; run: consts4 0 0 => i32:-467485863
;; run: consts4 4 4 => i32:-467485863
;; run: splat4 0 0 5 => i32:41925486
;; run: splat4 32 32 -100000 => i32:-1651808210
;; run: sub2 0 0 => f64:0xc02d800000000000
;; run: sub2 24 24 => f64:0xc02d800000000000
;; run: swapped 0 0 2 => f64:0xc004800000000000
;; run: swapped 40 40 12 => f64:0x4044a00000000000
;; run: chain4 0 0 3 => i32:-30383852
;; run: chain4 16 16 -77 => i32:91221588
;; run: highoff 0 0 => f64:0x401c000000000000
;; run: highoff 8 8 => f64:0x4036200000000000
;; run: revorder 0 0 5 => f64:0x4031300000000000
;; run: revorder 16 16 -2 => f64:0xc020400000000000
;; run: deep31 0 0 => i32:688817600
;; run: deep32 0 0 => i32:-835229714
;; run: isub4 0 0 => i32:1525326848
;; run: and4 0 0 => i32:3354126
;; run: or4 0 0 => i32:1324502606
;; run: xor4 0 0 => i32:1321148480
;; run: fdiv2 0 0 => f64:0x4018aebaebaebaec
;; run: fdiv2 8 8 => f64:0x3ff8630630630630
;; run: fmin2 0 0 => f64:0xc014800000000000
;; run: fmin2 8 8 => f64:0x4021800000000000
;; run: fmax2 0 0 => f64:0x3ff4000000000000
;; run: fmax2 8 8 => f64:0x402e400000000000
;; run: bytes4 0 0 => i32:43515781
;; run: twoload 0 0 64 => i32:-818361540
;; run: strided 0 0 => f64:0x4022400000000000
;; run: crossblock 0 0 0 => f64:0x400b000000000000
;; run: crossblock 0 0 1 => f64:0x4016000000000000
;; run: copyshared 0 0 => f64:0xc023300000000000
;; run: mul2 0 0 4294967299 => i32:1887436800
;; run: subswap 0 0 9 => f64:0xc024400000000000
;; run: dupload 0 0 7 => f64:0xc01e600000000000
;; run: latewrite 0 0 -5 => f64:0x402ed00000000000
;; run: axpy2 0 0 6 => f64:0xc018c00000000000
;; run: swap4 0 0 => i32:1327856732
;; run: swapped2 0 0 1 => f64:0x4010400000000000
;; run: swapsplat 0 0 => f64:0x4043240000000000
;; run: swapptr 0 0 => f64:0xc020000000000000
;; run: swapptr 0 96 => f64:0x4c626ad42e2af123
;; run: swapmul4 0 0 9 => i32:-1212470838
;; run: swapdeep4 0 0 => i32:-1256826988
;; run: swapsplat4 0 0 => i32:-383330006
;; run: twoptr2 0 0 => i32:647490
;; run: twoptr2 0 128 => i32:1635027294
;; run: interleave 0 0 5 => f64:0xc027c00000000000
;; run: twogroups 0 0 2 => f64:0xc1d57bf7eaec0000
;; run: nested 0 0 => i32:-1794955038
;; run: clash 0 0 6 => f64:0xc026200000000000
;; run: addrc2 0 0 10 => f64:0x4021800000000000
;; run: addrc2 40 40 -4 => f64:0x4007000000000000
;; run: addrc2 -8 -8 2 => f64:0x401dc00000000000
;; run: addrmix 0 0 => i32:3033820
;; run: addrmix -8 0 => trap: out of bounds memory access
;; run: rowmat 0 0 4 => f64:0xc122d07d98000000
;; run: rowmat 64 64 3 => f64:0x40f243a300000000
;; run: ptrmix 0 0 7 => f64:0x402d500000000000
;; run: swapmem 0 0 => f64:0x4032800000000000
;; run: outer2 0 0 0 8 => f64:0x4061240000000000
;; run: outer2 0 0 1 8 => f64:0x4061680000000000
;; run: sharesplat 0 0 1 => f64:0x4050680000000000
;; run: sharedlanes 0 0 0 6 => f64:0x4068718000000000
;; run: sharedlanes 0 0 1 6 => f64:0x4068b58000000000
;; run: byelem4 0 0 3 => i32:-2112847872
;; run: byelem4 16 16 -5 => i32:679878656
;; run: byelemadd 0 0 5 => f64:0xbffb800000000000
;; run: byelemblock 0 0 0 3 => f64:0x401bc00000000000
;; run: byelemblock 0 0 1 3 => f64:0x4022200000000000
;; run: byelemtwo 0 0 7 => f64:0x404e300000000000
;; run: byelemlanes 0 0 => f64:0x4050c4003a10c400
;; run: raw 0 0 8 => f64:0xc03a000000000000
;; run: raw 8 8 -6 => f64:0xbff0800000000000
;; run: multiuse 0 0 6 => f64:0xc029200000000000
;; run: fill 0 0 12 => f64:0xc033800000000000
;; run: loadpast 0 0 3 => f64:0x3fd8000000000000
;; run: storeread 0 0 10 => f64:0xc034f00000000000
;; run: waw 0 0 -12 => f64:0x4017800000000000
;; run: misalign 0 0 => f64:0xc02a000000000000
;; run: mixedsize 0 0 => i32:1758721730
;; run: gap 0 0 => i32:-1975737152
;; run: twoptr 0 0 => i32:821853602
;; run: twoptr 0 128 => i32:-669118590
;; run: narrow 0 0 4886718345 => i32:1508021890
;; run: consts2 0 0 => i32:89652
;; run: mixed 0 0 6 => f64:0xc01b800000000000
;; run: wrapdown 0 0 => i32:955330
;; run: wrapdown -8 0 => trap: out of bounds memory access
;; run: outerstore 0 0 0 => f64:0x403f400000000000
;; run: outerstore 0 0 1 => f64:0x4040b00000000000
;; run: opblock 0 0 1 6 => f64:0x4037600000000000
;; run: lateaddr 0 0 3 => f64:0x4020800000000000
;; run: twomem 0 0 => i32:1263170
;; run: narrowlane 0 0 4294967301 => i32:-1829352904
;; run: splitload 0 0 0 6 => f64:0x4038000000000000
;; run: splitload 0 0 1 6 => f64:0x4038000000000000
;; run: sharestore 0 0 8 => f64:0x404d000000000000
;; run: sharestore 0 64 8 => f64:0x5ac69d4c3c6f239b
;; run: wide64 0 => trap: out of bounds memory access
;; run: aftercall 0 0 640 0 6 => f64:0x4016000000000000
;; run: aftercall2 0 0 0 -10 => f64:0xc01a000000000000

(module
  (memory 1)
  (memory $b 1)
  (memory $w i64 1)
  (data (i32.const 0) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\39\30\00\00\ea\a9\37\1e\9b\23\6f\3c\4c\9d\a6\5a\fd\16\de\78\ae\90\15\17\5f\0a\4d\35\10\84\84\53\c1\fd\bb\71\72\77\f3\0f\23\f1\2a\2e\d4\6a\62\4c\85\e4\99\6a\36\5e\d1\08\e7\d7\08\27\98\51\40\45\49\cb\77\63\fa\44\af\01\ab\be\e6\1f\5c\38\1e\3e\0d\b2\55\5c\be\2b\8d\7a\6f\a5\c4\18\20\1f\fc\36\d1\98\33\55\82\12\6b\73\33\8c\a2\11\e4\05\da\2f\95\7f\11\4e\46\f9\48\6c\f7\72\80\0a\a8\ec\b7\28\59\66\ef\46\0a\e0\26\65\bb\59\5e\03\6c\d3\95\21\1d\4d\cd\3f\ce\c6\04\5e\7f\40\3c\7c\30\ba\73\1a\e1\33\ab\38\92\ad\e2\56\43\27\1a\75\f4\a0\51\13\a5\1a\89\31\56\94\c0\4f\07\0e\f8\6d\b8\87\2f\0c\00\00\00\00\00\00\0a\c0\00\00\00\00\00\00\f6\3f\00\00\00\00\00\00\18\40\00\00\00\00\00\00\00\c0\00\00\00\00\00\00\05\40\00\00\00\00\00\00\1d\40\00\00\00\00\00\00\e8\bf\00\00\00\00\00\00\0f\40\00\00\00\00\00\00\21\40\00\00\00\00\00\00\e0\3f\00\00\00\00\00\80\14\40\00\00\00\00\00\00\07\c0\00\00\00\00\00\00\fc\3f\00\00\00\00\00\80\19\40\00\00\00\00\00\00\fa\bf\00\00\00\00\00\00\08\40\00\00\00\00\00\80\1e\40\00\00\00\00\00\00\d8\bf\00\00\00\00\00\00\11\40\00\00\00\00\00\c0\21\40\00\00\00\00\00\00\ec\3f\00\00\00\00\00\00\16\40\00\00\00\00\00\00\04\c0\00\00\00\00\00\00\01\40\00\00\00\00\00\00\1b\40\00\00\00\00\00\00\f4\bf\00\00\00\00\00\00\0b\40\00\00\00\00\00\00\20\40\00\00\00\00\00\00\00\00\00\00\00\00\00\80\12\40\00\00\00\00\00\80\22\40\00\00\00\00\00\00\f4\3f\00\00\00\00\00\80\17\40\00\00\00\00\00\00\01\c0\00\00\00\00\00\00\04\40\00\00\00\00\00\80\1c\40\00\00\00\00\00\00\ec\bf\00\00\00\00\00\00\0e\40\00\00\00\00\00\c0\20\40\00\00\00\00\00\00\d8\3f\00\00\00\00\00\00\14\40\00\00\00\00\00\00\08\c0\00\00\00\00\00\00\fa\3f\00\00\00\00\00\00\19\40\00\00\00\00\00\00\fc\bf\00\00\00\00\00\00\07\40\00\00\00\00\00\00\1e\40\00\00\00\00\00\00\e0\bf\00\00\00\00\00\80\10\40\00\00\00\00\00\80\21\40\00\00\00\00\00\00\e8\3f\00\00\00\00\00\80\15\40\00\00\00\00\00\00\05\c0\00\00\00\00\00\00\00\40\00\00\00\00\00\80\1a\40\00\00\00\00\00\00\f6\bf\00\00\00\00\00\00\0a\40\00\00\00\00\00\80\1f\40\00\00\00\00\00\00\c0\bf\00\00\00\00\00\00\12\40\00\00\00\00\00\40\22\40\00\00\00\00\00\00\f2\3f\00\00\00\00\00\00\17\40\00\00\00\00\00\00\02\c0\00\00\00\00\00\00\03\40\00\00\00\00\00\00\1c\40\00\00\00\00\00\00\f0\bf\00\00\00\00\00\00\0d\40\00\00\00\00\00\80\20\40\00\00\00\00\00\00\d0\3f\00\00\00\00\00\80\13\40\00\00\00\00\00\00\09\c0\00\00\00\00\00\00\f8\3f\00\00\00\00\00\80\18\40\00\00\00\00\00\00\fe\bf\00\00\00\00\00\00\06\40\00\00\00\00\00\80\1d\40\00\00\00\00\00\00\e4\bf\00\00\00\00\00\00\10\40\00\00\00\00\00\40\21\40\00\00\00\00\00\00\e4\3f\00\00\00\00\00\00\15\40\00\00\00\00\00\00\06\c0\00\00\00\00\00\00\fe\3f\00\00\00\00\00\00\1a\40\00\00\00\00\00\00\f8\bf\00\00\00\00\00\00\09\40\00\00\00\00\00\00\1f\40\00\00\00\00\00\00\d0\bf\00\00\00\00\00\80\11\40\00\00\00\00\00\00\22\40\00\00\00\00\00\00\f0\3f\00\00\00\00\00\80\16\40\00\00\00\00\00\00\03\c0\00\00\00\00\00\00\02\40\00\00\00\00\00\80\1b\40\00\00\00\00\00\00\f2\bf\00\00\00\00\00\00\0c\40\00\00\00\00\00\40\20\40\00\00\00\00\00\00\c0\3f\00\00\00\00\00\00\13\40\00\00\00\00\00\00\0a\c0\00\00\00\00\00\00\f6\3f\00\00\00\00\00\00\18\40\00\00\00\00\00\00\00\c0\00\00\00\00\00\00\05\40\00\00\00\00\00\00\1d\40\00\00\00\00\00\00\e8\bf\00\00\00\00\00\00\0f\40\00\00\00\00\00\00\21\40\00\00\00\00\00\00\e0\3f\00\00\00\00\00\80\14\40\00\00\00\00\00\00\07\c0\00\00\00\00\00\00\fc\3f\00\00\00\00\00\80\19\40\00\00\00\00\00\00\fa\bf\00\00\00\00\00\00\08\40\00\00\00\00\00\80\1e\40\00\00\00\00\00\00\d8\bf\00\00\00\00\00\00\11\40\00\00\00\00\00\c0\21\40\00\00\00\00\00\00\ec\3f\00\00\00\00\00\00\16\40\00\00\00\00\00\00\04\c0\00\00\00\00\00\00\01\40\00\00\00\00\00\00\1b\40\00\00\00\00\00\00\f4\bf\00\00\00\00\00\00\0b\40\00\00\00\00\00\00\20\40\00\00\00\00\00\00\00\00\00\00\00\00\00\80\12\40\00\00\00\00\00\80\22\40\00\00\00\00\00\00\f4\3f\00\00\00\00\00\80\17\40\00\00\00\00\00\00\01\c0\00\00\00\00\00\00\04\40\00\00\00\00\00\80\1c\40\00\00\00\00\00\00\ec\bf\00\00\00\00\00\00\0e\40\00\00\00\00\00\c0\20\40\00\00\00\00\00\00\d8\3f\00\00\00\00\00\00\14\40\00\00\00\00\00\00\08\c0\00\00\00\00\00\00\fa\3f\00\00\00\00\00\00\19\40\00\00\00\00\00\00\fc\bf\00\00\00\00\00\00\07\40\00\00\00\00\00\00\1e\40\00\00\00\00\00\00\e0\bf\00\00\00\00\00\80\10\40\00\00\00\00\00\80\21\40\00\00\00\00\00\00\e8\3f\00\00\00\00\00\80\15\40\00\00\00\00\00\00\05\c0\00\00\00\00\00\00\00\40\00\00\00\00\00\80\1a\40\00\00\00\00\00\00\f6\bf\00\00\00\00\00\00\0a\40\00\00\00\00\00\80\1f\40\00\00\00\00\00\00\c0\bf\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\07\00\00\00\3e\9e\00\00\75\3c\01\00\ac\da\01\00\e3\78\02\00\1a\17\03\00\51\b5\03\00\88\53\04\00\bf\f1\04\00\f6\8f\05\00\2d\2e\06\00\64\cc\06\00\9b\6a\07\00\d2\08\08\00\09\a7\08\00\40\45\09\00\77\e3\09\00\ae\81\0a\00\e5\1f\0b\00\1c\be\0b\00\53\5c\0c\00\8a\fa\0c\00\c1\98\0d\00\f8\36\0e\00\2f\d5\0e\00\66\73\0f\00\9d\11\10\00\d4\af\10\00\0b\4e\11\00\42\ec\11\00\79\8a\12\00\b0\28\13\00\e7\c6\13\00\1e\65\14\00\55\03\15\00\8c\a1\15\00\c3\3f\16\00\fa\dd\16\00\31\7c\17\00\68\1a\18\00\9f\b8\18\00\d6\56\19\00\0d\f5\19\00\44\93\1a\00\7b\31\1b\00\b2\cf\1b\00\e9\6d\1c\00\20\0c\1d\00\57\aa\1d\00\8e\48\1e\00\c5\e6\1e\00\fc\84\1f\00\33\23\20\00\6a\c1\20\00\a1\5f\21\00\d8\fd\21\00\0f\9c\22\00\46\3a\23\00\7d\d8\23\00\b4\76\24\00\eb\14\25\00\22\b3\25\00\59\51\26\00\90\ef\26\00\c7\8d\27\00\fe\2b\28\00\35\ca\28\00\6c\68\29\00\a3\06\2a\00\da\a4\2a\00\11\43\2b\00\48\e1\2b\00\7f\7f\2c\00\b6\1d\2d\00\ed\bb\2d\00\24\5a\2e\00\5b\f8\2e\00\92\96\2f\00\c9\34\30\00\00\d3\30\00\37\71\31\00\6e\0f\32\00\a5\ad\32\00\dc\4b\33\00\13\ea\33\00\4a\88\34\00\81\26\35\00\b8\c4\35\00\ef\62\36\00\26\01\37\00\5d\9f\37\00\94\3d\38\00\cb\db\38\00\02\7a\39\00\39\18\3a\00\70\b6\3a\00\a7\54\3b\00\de\f2\3b\00\15\91\3c\00\4c\2f\3d\00\83\cd\3d\00\ba\6b\3e\00\f1\09\3f\00\28\a8\3f\00\5f\46\40\00\96\e4\40\00\cd\82\41\00\04\21\42\00\3b\bf\42\00\72\5d\43\00\a9\fb\43\00\e0\99\44\00\17\38\45\00\4e\d6\45\00\85\74\46\00\bc\12\47\00\f3\b0\47\00\2a\4f\48\00\61\ed\48\00\98\8b\49\00\cf\29\4a\00\06\c8\4a\00\3d\66\4b\00\74\04\4c\00\ab\a2\4c\00\e2\40\4d\00\19\df\4d\00\50\7d\4e\00\87\1b\4f\00\be\b9\4f\00\f5\57\50\00\2c\f6\50\00\63\94\51\00\9a\32\52\00\d1\d0\52\00\08\6f\53\00\3f\0d\54\00\76\ab\54\00\ad\49\55\00\e4\e7\55\00\1b\86\56\00\52\24\57\00\89\c2\57\00\c0\60\58\00\f7\fe\58\00\2e\9d\59\00\65\3b\5a\00\9c\d9\5a\00\d3\77\5b\00\0a\16\5c\00\41\b4\5c\00\78\52\5d\00\af\f0\5d\00\e6\8e\5e\00\1d\2d\5f\00\54\cb\5f\00\8b\69\60\00\c2\07\61\00\f9\a5\61\00\30\44\62\00\67\e2\62\00\9e\80\63\00\d5\1e\64\00\0c\bd\64\00\43\5b\65\00\7a\f9\65\00\b1\97\66\00\e8\35\67\00\1f\d4\67\00\56\72\68\00\8d\10\69\00\c4\ae\69\00\fb\4c\6a\00\32\eb\6a\00\69\89\6b\00\a0\27\6c\00\d7\c5\6c\00\0e\64\6d\00\45\02\6e\00\7c\a0\6e\00\b3\3e\6f\00\ea\dc\6f\00\21\7b\70\00\58\19\71\00\8f\b7\71\00\c6\55\72\00\fd\f3\72\00\34\92\73\00\6b\30\74\00\a2\ce\74\00\d9\6c\75\00\10\0b\76\00\47\a9\76\00\7e\47\77\00\b5\e5\77\00\ec\83\78\00\23\22\79\00\5a\c0\79\00\91\5e\7a\00\c8\fc\7a\00\ff\9a\7b\00\36\39\7c\00\6d\d7\7c\00\a4\75\7d\00\db\13\7e\00\12\b2\7e\00\49\50\7f\00\80\ee\7f\00\b7\8c\80\00\ee\2a\81\00\25\c9\81\00\5c\67\82\00\93\05\83\00\ca\a3\83\00\01\42\84\00\38\e0\84\00\6f\7e\85\00\a6\1c\86\00\dd\ba\86\00\14\59\87\00\4b\f7\87\00\82\95\88\00\b9\33\89\00\f0\d1\89\00\27\70\8a\00\5e\0e\8b\00\95\ac\8b\00\cc\4a\8c\00\03\e9\8c\00\3a\87\8d\00\71\25\8e\00\a8\c3\8e\00\df\61\8f\00\16\00\90\00\4d\9e\90\00\84\3c\91\00\bb\da\91\00\f2\78\92\00\29\17\93\00\60\b5\93\00\97\53\94\00\ce\f1\94\00\05\90\95\00\3c\2e\96\00\73\cc\96\00\aa\6a\97\00\e1\08\98\00\18\a7\98\00\4f\45\99\00\86\e3\99\00\bd\81\9a\00\f4\1f\9b\00\2b\be\9b\00\62\5c\9c\00\99\fa\9c\00\d0\98\9d\00\07\37\9e\00\3e\d5\9e\00\75\73\9f\00\ac\11\a0\00\e3\af\a0\00\1a\4e\a1\00\51\ec\a1\00\88\8a\a2\00\bf\28\a3\00\f6\c6\a3\00\2d\65\a4\00\64\03\a5\00\9b\a1\a5\00\d2\3f\a6\00\09\de\a6\00\40\7c\a7\00\77\1a\a8\00\ae\b8\a8\00\e5\56\a9\00\1c\f5\a9\00\53\93\aa\00\8a\31\ab\00\c1\cf\ab\00\f8\6d\ac\00\2f\0c\ad\00\66\aa\ad\00\9d\48\ae\00\d4\e6\ae\00\0b\85\af\00\42\23\b0\00\79\c1\b0\00\b0\5f\b1\00\e7\fd\b1\00\1e\9c\b2\00\55\3a\b3\00\8c\d8\b3\00\c3\76\b4\00\fa\14\b5\00\31\b3\b5\00\68\51\b6\00\9f\ef\b6\00\d6\8d\b7\00\0d\2c\b8\00\44\ca\b8\00\7b\68\b9\00\b2\06\ba\00\e9\a4\ba\00\20\43\bb\00\57\e1\bb\00\8e\7f\bc\00\c5\1d\bd\00\fc\bb\bd\00\33\5a\be\00\6a\f8\be\00\a1\96\bf\00\d8\34\c0\00\0f\d3\c0\00\46\71\c1\00\7d\0f\c2\00\b4\ad\c2\00\eb\4b\c3\00\22\ea\c3\00\59\88\c4\00\90\26\c5\00\c7\c4\c5\00\fe\62\c6\00\35\01\c7\00\6c\9f\c7\00\a3\3d\c8\00\da\db\c8\00\11\7a\c9\00\48\18\ca\00\7f\b6\ca\00\b6\54\cb\00\ed\f2\cb\00\24\91\cc\00\5b\2f\cd\00\92\cd\cd\00\c9\6b\ce\00\00\0a\cf\00\37\a8\cf\00\6e\46\d0\00\a5\e4\d0\00\dc\82\d1\00\13\21\d2\00\4a\bf\d2\00\81\5d\d3\00\b8\fb\d3\00\ef\99\d4\00\26\38\d5\00\5d\d6\d5\00\94\74\d6\00\cb\12\d7\00\02\b1\d7\00\39\4f\d8\00\70\ed\d8\00\a7\8b\d9\00\de\29\da\00\15\c8\da\00\4c\66\db\00\83\04\dc\00\ba\a2\dc\00\f1\40\dd\00\28\df\dd\00\5f\7d\de\00\96\1b\df\00\cd\b9\df\00\04\58\e0\00\3b\f6\e0\00\72\94\e1\00\a9\32\e2\00\e0\d0\e2\00\17\6f\e3\00\4e\0d\e4\00\85\ab\e4\00\bc\49\e5\00\f3\e7\e5\00\2a\86\e6\00\61\24\e7\00\98\c2\e7\00\cf\60\e8\00\06\ff\e8\00\3d\9d\e9\00\74\3b\ea\00\ab\d9\ea\00\e2\77\eb\00\19\16\ec\00\50\b4\ec\00\87\52\ed\00\be\f0\ed\00\f5\8e\ee\00\2c\2d\ef\00\63\cb\ef\00\9a\69\f0\00\d1\07\f1\00\08\a6\f1\00\3f\44\f2\00\76\e2\f2\00\ad\80\f3\00\e4\1e\f4\00\1b\bd\f4\00\52\5b\f5\00\89\f9\f5\00\c0\97\f6\00\f7\35\f7\00\2e\d4\f7\00\65\72\f8\00\9c\10\f9\00\d3\ae\f9\00\0a\4d\fa\00\41\eb\fa\00\78\89\fb\00\af\27\fc\00\e6\c5\fc\00\1d\64\fd\00\54\02\fe\00\8b\a0\fe\00\c2\3e\ff\00\f9\dc\ff\00\30\7b\00\01\67\19\01\01\9e\b7\01\01\d5\55\02\01\0c\f4\02\01\43\92\03\01\7a\30\04\01\b1\ce\04\01\e8\6c\05\01\1f\0b\06\01\56\a9\06\01\8d\47\07\01\c4\e5\07\01\fb\83\08\01\32\22\09\01\69\c0\09\01\a0\5e\0a\01\d7\fc\0a\01\0e\9b\0b\01\45\39\0c\01\7c\d7\0c\01\b3\75\0d\01\ea\13\0e\01\21\b2\0e\01\58\50\0f\01\8f\ee\0f\01\c6\8c\10\01\fd\2a\11\01\34\c9\11\01\6b\67\12\01\a2\05\13\01\d9\a3\13\01\10\42\14\01\47\e0\14\01\7e\7e\15\01\b5\1c\16\01\ec\ba\16\01\23\59\17\01\5a\f7\17\01\91\95\18\01\c8\33\19\01\ff\d1\19\01\36\70\1a\01\6d\0e\1b\01\a4\ac\1b\01\db\4a\1c\01\12\e9\1c\01\49\87\1d\01\80\25\1e\01\b7\c3\1e\01\ee\61\1f\01\25\00\20\01\5c\9e\20\01\93\3c\21\01\ca\da\21\01\01\79\22\01\38\17\23\01\6f\b5\23\01\a6\53\24\01\dd\f1\24\01\14\90\25\01\4b\2e\26\01\82\cc\26\01\b9\6a\27\01\f0\08\28\01\27\a7\28\01\5e\45\29\01\95\e3\29\01\cc\81\2a\01\03\20\2b\01\3a\be\2b\01\71\5c\2c\01\a8\fa\2c\01\df\98\2d\01\16\37\2e\01\4d\d5\2e\01\84\73\2f\01\bb\11\30\01\f2\af\30\01\29\4e\31\01\60\ec\31\01\97\8a\32\01\ce\28\33\01\05\c7\33\01\3c\65\34\01\73\03\35\01\aa\a1\35\01\e1\3f\36\01\18\de\36\01\4f\7c\37\01\86\1a\38\01\bd\b8\38\01\f4\56\39\01\2b\f5\39\01\62\93\3a\01\99\31\3b\01\d0\cf\3b\01\07\6e\3c\01\3e\0c\3d\01\75\aa\3d\01\ac\48\3e\01\e3\e6\3e\01\1a\85\3f\01\51\23\40\01\88\c1\40\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
  (type $wr (func (param i32 f64)))
  (table 1 funcref)
  (elem (i32.const 0) $wr2)
  (func $wr2 (param $a i32) (param $x f64)
    (f64.store offset=0 (local.get $a) (local.get $x))
    (f64.store offset=8 (local.get $a) (f64.add (local.get $x) (f64.const 1))))
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
  (func $fs (param $a i32) (param $n i32) (result f64)
    (local $r f64) (local $j i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $r (f64.add (f64.add (local.get $r) (local.get $r))
          (f64.load (i32.add (local.get $a) (i32.shl (local.get $j) (i32.const 3))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $r))
  (func $scale2 (export "scale2") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=8 (local.get $p))))
    (f64.store offset=16 (local.get $p) (f64.mul (local.get $k) (f64.load offset=16 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2)))
  (func $add4 (export "add4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $copy4 (export "copy4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $p) (i32.load offset=68 (local.get $p)))
    (i32.store offset=8 (local.get $p) (i32.load offset=72 (local.get $p)))
    (i32.store offset=12 (local.get $p) (i32.load offset=76 (local.get $p)))
    (call $h (local.get $q) (i32.const 4)))
  (func $consts4 (export "consts4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.const 11))
    (i32.store offset=4 (local.get $p) (i32.const -7))
    (i32.store offset=8 (local.get $p) (i32.const 123456789))
    (i32.store offset=12 (local.get $p) (i32.const 0))
    (call $h (local.get $q) (i32.const 4)))
  (func $splat4 (export "splat4") (param $p i32) (param $q i32) (param $x i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (local.get $x) (i32.load offset=1792 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (local.get $x) (i32.load offset=1796 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (local.get $x) (i32.load offset=1800 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (local.get $x) (i32.load offset=1804 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $sub2 (export "sub2") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.sub (f64.load offset=256 (local.get $p)) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.sub (f64.load offset=264 (local.get $p)) (f64.load offset=520 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swapped (export "swapped") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.mul (f64.load offset=264 (local.get $p)) (local.get $k)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $chain4 (export "chain4") (param $p i32) (param $q i32) (param $k i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p)))))
    (i32.store offset=4 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p)))))
    (i32.store offset=8 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p)))))
    (i32.store offset=12 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p)))))
    (call $h (local.get $q) (i32.const 4)))
  (func $highoff (export "highoff") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=1272 (local.get $p) (f64.add (f64.load offset=264 (local.get $p)) (f64.load offset=520 (local.get $p))))
    (f64.store offset=1280 (local.get $p) (f64.add (f64.load offset=272 (local.get $p)) (f64.load offset=528 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 1272)) (i32.const 2)))
  (func $revorder (export "revorder") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=304 (local.get $p))))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=296 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $deep31 (export "deep31") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (i32.load offset=1920 (local.get $p))) (i32.load offset=1984 (local.get $p))) (i32.load offset=2048 (local.get $p))) (i32.load offset=2112 (local.get $p))) (i32.load offset=2176 (local.get $p))) (i32.load offset=2240 (local.get $p))) (i32.load offset=2304 (local.get $p))) (i32.load offset=2368 (local.get $p))) (i32.load offset=2432 (local.get $p))) (i32.load offset=2496 (local.get $p))) (i32.load offset=2560 (local.get $p))) (i32.load offset=2624 (local.get $p))) (i32.load offset=2688 (local.get $p))) (i32.load offset=2752 (local.get $p))) (i32.load offset=2816 (local.get $p))) (i32.load offset=2880 (local.get $p))) (i32.load offset=2944 (local.get $p))) (i32.load offset=3008 (local.get $p))) (i32.load offset=3072 (local.get $p))) (i32.load offset=3136 (local.get $p))) (i32.load offset=3200 (local.get $p))) (i32.load offset=3264 (local.get $p))) (i32.load offset=3328 (local.get $p))) (i32.load offset=3392 (local.get $p))) (i32.load offset=3456 (local.get $p))) (i32.load offset=3520 (local.get $p))) (i32.load offset=3584 (local.get $p))) (i32.load offset=3648 (local.get $p))) (i32.load offset=3712 (local.get $p))) (i32.load offset=3776 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))) (i32.load offset=1924 (local.get $p))) (i32.load offset=1988 (local.get $p))) (i32.load offset=2052 (local.get $p))) (i32.load offset=2116 (local.get $p))) (i32.load offset=2180 (local.get $p))) (i32.load offset=2244 (local.get $p))) (i32.load offset=2308 (local.get $p))) (i32.load offset=2372 (local.get $p))) (i32.load offset=2436 (local.get $p))) (i32.load offset=2500 (local.get $p))) (i32.load offset=2564 (local.get $p))) (i32.load offset=2628 (local.get $p))) (i32.load offset=2692 (local.get $p))) (i32.load offset=2756 (local.get $p))) (i32.load offset=2820 (local.get $p))) (i32.load offset=2884 (local.get $p))) (i32.load offset=2948 (local.get $p))) (i32.load offset=3012 (local.get $p))) (i32.load offset=3076 (local.get $p))) (i32.load offset=3140 (local.get $p))) (i32.load offset=3204 (local.get $p))) (i32.load offset=3268 (local.get $p))) (i32.load offset=3332 (local.get $p))) (i32.load offset=3396 (local.get $p))) (i32.load offset=3460 (local.get $p))) (i32.load offset=3524 (local.get $p))) (i32.load offset=3588 (local.get $p))) (i32.load offset=3652 (local.get $p))) (i32.load offset=3716 (local.get $p))) (i32.load offset=3780 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))) (i32.load offset=1928 (local.get $p))) (i32.load offset=1992 (local.get $p))) (i32.load offset=2056 (local.get $p))) (i32.load offset=2120 (local.get $p))) (i32.load offset=2184 (local.get $p))) (i32.load offset=2248 (local.get $p))) (i32.load offset=2312 (local.get $p))) (i32.load offset=2376 (local.get $p))) (i32.load offset=2440 (local.get $p))) (i32.load offset=2504 (local.get $p))) (i32.load offset=2568 (local.get $p))) (i32.load offset=2632 (local.get $p))) (i32.load offset=2696 (local.get $p))) (i32.load offset=2760 (local.get $p))) (i32.load offset=2824 (local.get $p))) (i32.load offset=2888 (local.get $p))) (i32.load offset=2952 (local.get $p))) (i32.load offset=3016 (local.get $p))) (i32.load offset=3080 (local.get $p))) (i32.load offset=3144 (local.get $p))) (i32.load offset=3208 (local.get $p))) (i32.load offset=3272 (local.get $p))) (i32.load offset=3336 (local.get $p))) (i32.load offset=3400 (local.get $p))) (i32.load offset=3464 (local.get $p))) (i32.load offset=3528 (local.get $p))) (i32.load offset=3592 (local.get $p))) (i32.load offset=3656 (local.get $p))) (i32.load offset=3720 (local.get $p))) (i32.load offset=3784 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (i32.load offset=1932 (local.get $p))) (i32.load offset=1996 (local.get $p))) (i32.load offset=2060 (local.get $p))) (i32.load offset=2124 (local.get $p))) (i32.load offset=2188 (local.get $p))) (i32.load offset=2252 (local.get $p))) (i32.load offset=2316 (local.get $p))) (i32.load offset=2380 (local.get $p))) (i32.load offset=2444 (local.get $p))) (i32.load offset=2508 (local.get $p))) (i32.load offset=2572 (local.get $p))) (i32.load offset=2636 (local.get $p))) (i32.load offset=2700 (local.get $p))) (i32.load offset=2764 (local.get $p))) (i32.load offset=2828 (local.get $p))) (i32.load offset=2892 (local.get $p))) (i32.load offset=2956 (local.get $p))) (i32.load offset=3020 (local.get $p))) (i32.load offset=3084 (local.get $p))) (i32.load offset=3148 (local.get $p))) (i32.load offset=3212 (local.get $p))) (i32.load offset=3276 (local.get $p))) (i32.load offset=3340 (local.get $p))) (i32.load offset=3404 (local.get $p))) (i32.load offset=3468 (local.get $p))) (i32.load offset=3532 (local.get $p))) (i32.load offset=3596 (local.get $p))) (i32.load offset=3660 (local.get $p))) (i32.load offset=3724 (local.get $p))) (i32.load offset=3788 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $deep32 (export "deep32") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (i32.load offset=1920 (local.get $p))) (i32.load offset=1984 (local.get $p))) (i32.load offset=2048 (local.get $p))) (i32.load offset=2112 (local.get $p))) (i32.load offset=2176 (local.get $p))) (i32.load offset=2240 (local.get $p))) (i32.load offset=2304 (local.get $p))) (i32.load offset=2368 (local.get $p))) (i32.load offset=2432 (local.get $p))) (i32.load offset=2496 (local.get $p))) (i32.load offset=2560 (local.get $p))) (i32.load offset=2624 (local.get $p))) (i32.load offset=2688 (local.get $p))) (i32.load offset=2752 (local.get $p))) (i32.load offset=2816 (local.get $p))) (i32.load offset=2880 (local.get $p))) (i32.load offset=2944 (local.get $p))) (i32.load offset=3008 (local.get $p))) (i32.load offset=3072 (local.get $p))) (i32.load offset=3136 (local.get $p))) (i32.load offset=3200 (local.get $p))) (i32.load offset=3264 (local.get $p))) (i32.load offset=3328 (local.get $p))) (i32.load offset=3392 (local.get $p))) (i32.load offset=3456 (local.get $p))) (i32.load offset=3520 (local.get $p))) (i32.load offset=3584 (local.get $p))) (i32.load offset=3648 (local.get $p))) (i32.load offset=3712 (local.get $p))) (i32.load offset=3776 (local.get $p))) (i32.load offset=3840 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))) (i32.load offset=1924 (local.get $p))) (i32.load offset=1988 (local.get $p))) (i32.load offset=2052 (local.get $p))) (i32.load offset=2116 (local.get $p))) (i32.load offset=2180 (local.get $p))) (i32.load offset=2244 (local.get $p))) (i32.load offset=2308 (local.get $p))) (i32.load offset=2372 (local.get $p))) (i32.load offset=2436 (local.get $p))) (i32.load offset=2500 (local.get $p))) (i32.load offset=2564 (local.get $p))) (i32.load offset=2628 (local.get $p))) (i32.load offset=2692 (local.get $p))) (i32.load offset=2756 (local.get $p))) (i32.load offset=2820 (local.get $p))) (i32.load offset=2884 (local.get $p))) (i32.load offset=2948 (local.get $p))) (i32.load offset=3012 (local.get $p))) (i32.load offset=3076 (local.get $p))) (i32.load offset=3140 (local.get $p))) (i32.load offset=3204 (local.get $p))) (i32.load offset=3268 (local.get $p))) (i32.load offset=3332 (local.get $p))) (i32.load offset=3396 (local.get $p))) (i32.load offset=3460 (local.get $p))) (i32.load offset=3524 (local.get $p))) (i32.load offset=3588 (local.get $p))) (i32.load offset=3652 (local.get $p))) (i32.load offset=3716 (local.get $p))) (i32.load offset=3780 (local.get $p))) (i32.load offset=3844 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))) (i32.load offset=1928 (local.get $p))) (i32.load offset=1992 (local.get $p))) (i32.load offset=2056 (local.get $p))) (i32.load offset=2120 (local.get $p))) (i32.load offset=2184 (local.get $p))) (i32.load offset=2248 (local.get $p))) (i32.load offset=2312 (local.get $p))) (i32.load offset=2376 (local.get $p))) (i32.load offset=2440 (local.get $p))) (i32.load offset=2504 (local.get $p))) (i32.load offset=2568 (local.get $p))) (i32.load offset=2632 (local.get $p))) (i32.load offset=2696 (local.get $p))) (i32.load offset=2760 (local.get $p))) (i32.load offset=2824 (local.get $p))) (i32.load offset=2888 (local.get $p))) (i32.load offset=2952 (local.get $p))) (i32.load offset=3016 (local.get $p))) (i32.load offset=3080 (local.get $p))) (i32.load offset=3144 (local.get $p))) (i32.load offset=3208 (local.get $p))) (i32.load offset=3272 (local.get $p))) (i32.load offset=3336 (local.get $p))) (i32.load offset=3400 (local.get $p))) (i32.load offset=3464 (local.get $p))) (i32.load offset=3528 (local.get $p))) (i32.load offset=3592 (local.get $p))) (i32.load offset=3656 (local.get $p))) (i32.load offset=3720 (local.get $p))) (i32.load offset=3784 (local.get $p))) (i32.load offset=3848 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (i32.load offset=1932 (local.get $p))) (i32.load offset=1996 (local.get $p))) (i32.load offset=2060 (local.get $p))) (i32.load offset=2124 (local.get $p))) (i32.load offset=2188 (local.get $p))) (i32.load offset=2252 (local.get $p))) (i32.load offset=2316 (local.get $p))) (i32.load offset=2380 (local.get $p))) (i32.load offset=2444 (local.get $p))) (i32.load offset=2508 (local.get $p))) (i32.load offset=2572 (local.get $p))) (i32.load offset=2636 (local.get $p))) (i32.load offset=2700 (local.get $p))) (i32.load offset=2764 (local.get $p))) (i32.load offset=2828 (local.get $p))) (i32.load offset=2892 (local.get $p))) (i32.load offset=2956 (local.get $p))) (i32.load offset=3020 (local.get $p))) (i32.load offset=3084 (local.get $p))) (i32.load offset=3148 (local.get $p))) (i32.load offset=3212 (local.get $p))) (i32.load offset=3276 (local.get $p))) (i32.load offset=3340 (local.get $p))) (i32.load offset=3404 (local.get $p))) (i32.load offset=3468 (local.get $p))) (i32.load offset=3532 (local.get $p))) (i32.load offset=3596 (local.get $p))) (i32.load offset=3660 (local.get $p))) (i32.load offset=3724 (local.get $p))) (i32.load offset=3788 (local.get $p))) (i32.load offset=3852 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $isub4 (export "isub4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.sub (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.sub (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.sub (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.sub (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $and4 (export "and4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.and (i32.load offset=1792 (local.get $p)) (i32.load offset=1920 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.and (i32.load offset=1796 (local.get $p)) (i32.load offset=1924 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.and (i32.load offset=1800 (local.get $p)) (i32.load offset=1928 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.and (i32.load offset=1804 (local.get $p)) (i32.load offset=1932 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $or4 (export "or4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.or (i32.load offset=1792 (local.get $p)) (i32.load offset=1920 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.or (i32.load offset=1796 (local.get $p)) (i32.load offset=1924 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.or (i32.load offset=1800 (local.get $p)) (i32.load offset=1928 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.or (i32.load offset=1804 (local.get $p)) (i32.load offset=1932 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $xor4 (export "xor4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.xor (i32.load offset=1792 (local.get $p)) (i32.load offset=1920 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.xor (i32.load offset=1796 (local.get $p)) (i32.load offset=1924 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.xor (i32.load offset=1800 (local.get $p)) (i32.load offset=1928 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.xor (i32.load offset=1804 (local.get $p)) (i32.load offset=1932 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $fdiv2 (export "fdiv2") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.div (f64.load offset=256 (local.get $p)) (f64.load offset=1024 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.div (f64.load offset=264 (local.get $p)) (f64.load offset=1032 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $fmin2 (export "fmin2") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.min (f64.load offset=256 (local.get $p)) (f64.load offset=1024 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.min (f64.load offset=264 (local.get $p)) (f64.load offset=1032 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $fmax2 (export "fmax2") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.max (f64.load offset=256 (local.get $p)) (f64.load offset=1024 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.max (f64.load offset=264 (local.get $p)) (f64.load offset=1032 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $bytes4 (export "bytes4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.load8_u offset=64 (local.get $p)) (i32.load offset=1792 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.load8_u offset=65 (local.get $p)) (i32.load offset=1796 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.load8_u offset=66 (local.get $p)) (i32.load offset=1800 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.load8_u offset=67 (local.get $p)) (i32.load offset=1804 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $twoload (export "twoload") (param $p i32) (param $q i32) (param $r i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local.set $a (i32.add (i32.load offset=1792 (local.get $q)) (i32.load offset=1856 (local.get $p))))
    (local.set $b (i32.add (i32.load offset=1796 (local.get $r)) (i32.load offset=1860 (local.get $p))))
    (local.set $c (i32.add (i32.load offset=1800 (local.get $q)) (i32.load offset=1864 (local.get $p))))
    (local.set $d (i32.add (i32.load offset=1804 (local.get $r)) (i32.load offset=1868 (local.get $p))))
    (i32.store offset=0 (local.get $p) (local.get $a))
    (i32.store offset=4 (local.get $p) (local.get $b))
    (i32.store offset=8 (local.get $p) (local.get $c))
    (i32.store offset=12 (local.get $p) (local.get $d))
    (call $h (local.get $p) (i32.const 4)))
  (func $strided (export "strided") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.add (f64.load offset=256 (local.get $p)) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (f64.load offset=272 (local.get $p)) (f64.load offset=520 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $crossblock (export "crossblock") (param $p i32) (param $q i32) (param $c i32) (result f64)
    (local $a f64) (local $b f64)
    (local.set $a (f64.load offset=256 (local.get $p)))
    (local.set $b (f64.load offset=264 (local.get $p)))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $copyshared (export "copyshared") (param $p i32) (param $q i32) (result f64)
    (local $a f64) (local $b f64)
    (local.set $a (f64.load offset=256 (local.get $p)))
    (local.set $b (f64.load offset=264 (local.get $p)))
    (f64.store offset=0 (local.get $p) (local.get $a))
    (f64.store offset=8 (local.get $p) (local.get $b))
    (f64.add (f64.mul (local.get $a) (local.get $b)) (call $fs (local.get $q) (i32.const 2))))
  (func $mul2 (export "mul2") (param $p i32) (param $q i32) (param $x i64) (result i32)
    (i64.store offset=0 (local.get $p) (i64.mul (local.get $x) (i64.load offset=256 (local.get $p))))
    (i64.store offset=8 (local.get $p) (i64.mul (local.get $x) (i64.load offset=264 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $subswap (export "subswap") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.sub (f64.load offset=256 (local.get $p)) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.sub (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $dupload (export "dupload") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $a f64) (local $b f64) (local $t f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $a (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (local.set $t (f64.load offset=264 (local.get $q)))
    (local.set $b (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.store offset=0 (local.get $p) (local.get $a))
    (f64.store offset=8 (local.get $p) (local.get $b))
    (f64.add (local.get $t) (call $fs (local.get $q) (i32.const 2))))
  (func $latewrite (export "latewrite") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=256 (local.get $p) (f64.const 9))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (f64.load offset=256 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $axpy2 (export "axpy2") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.add (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))) (f64.const 0.5)))
    (f64.store offset=8 (local.get $p) (f64.add (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))) (f64.const 0.5)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swap4 (export "swap4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1920 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1924 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.load offset=1928 (local.get $p)) (i32.load offset=1800 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1932 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $swapped2 (export "swapped2") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (f64.load offset=312 (local.get $p)) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=320 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swapsplat (export "swapsplat") (param $p i32) (param $q i32) (result f64)
    (local $s f64)
    (local.set $s (f64.load offset=600 (local.get $p)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $s) (f64.load offset=328 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.mul (f64.load offset=336 (local.get $p)) (local.get $s)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swapptr (export "swapptr") (param $p i32) (param $q i32) (result f64)
    (local $a f64) (local $b f64)
    (local.set $a (f64.add (f64.load offset=344 (local.get $p)) (f64.load offset=344 (local.get $q))))
    (local.set $b (f64.add (f64.load offset=352 (local.get $q)) (f64.load offset=352 (local.get $p))))
    (f64.store offset=0 (local.get $p) (local.get $a))
    (f64.store offset=8 (local.get $p) (local.get $b))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swapmul4 (export "swapmul4") (param $p i32) (param $q i32) (param $k i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.mul (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (local.get $k)))
    (i32.store offset=4 (local.get $p) (i32.add (i32.mul (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))) (local.get $k)))
    (i32.store offset=8 (local.get $p) (i32.add (local.get $k) (i32.mul (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p)))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.mul (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (local.get $k)))
    (call $h (local.get $q) (i32.const 4)))
  (func $swapdeep4 (export "swapdeep4") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.mul (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (i32.mul (i32.load offset=1920 (local.get $p)) (i32.load offset=1984 (local.get $p)))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.mul (i32.load offset=1860 (local.get $p)) (i32.load offset=1796 (local.get $p))) (i32.mul (i32.load offset=1924 (local.get $p)) (i32.load offset=1988 (local.get $p)))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.mul (i32.load offset=1928 (local.get $p)) (i32.load offset=1992 (local.get $p))) (i32.mul (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p)))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.mul (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (i32.mul (i32.load offset=1932 (local.get $p)) (i32.load offset=1996 (local.get $p)))))
    (call $h (local.get $q) (i32.const 4)))
  (func $swapsplat4 (export "swapsplat4") (param $p i32) (param $q i32) (result i32)
    (local $s i32)
    (local.set $s (i32.load offset=2000 (local.get $p)))
    (i32.store offset=0 (local.get $p) (i32.mul (local.get $s) (i32.load offset=1792 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.mul (local.get $s) (i32.load offset=1796 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.mul (i32.load offset=1800 (local.get $p)) (local.get $s)))
    (i32.store offset=12 (local.get $p) (i32.mul (local.get $s) (i32.load offset=1804 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $twoptr2 (export "twoptr2") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.const 21))
    (i32.store offset=4 (local.get $p) (i32.const 22))
    (i32.store offset=8 (local.get $p) (i32.const 23))
    (i32.store offset=12 (local.get $q) (i32.const 24))
    (i32.store offset=40 (local.get $q) (i32.const 25))
    (call $h (local.get $q) (i32.const 4)))
  (func $interleave (export "interleave") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=16 (local.get $p) (f64.add (local.get $k) (f64.load offset=272 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.store offset=24 (local.get $p) (f64.add (local.get $k) (f64.load offset=280 (local.get $p))))
    (call $fs (local.get $q) (i32.const 4)))
  (func $twogroups (export "twogroups") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (i32.store offset=512 (local.get $p) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))))
    (i32.store offset=516 (local.get $p) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))))
    (i32.store offset=520 (local.get $p) (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))))
    (i32.store offset=524 (local.get $p) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))))
    (local.set $k (f64.add (local.get $k) (f64.const 1)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (f64.convert_i32_s (call $h (i32.add (local.get $q) (i32.const 512)) (i32.const 4))) (call $fs (local.get $q) (i32.const 2))))
  (func $nested (export "nested") (param $p i32) (param $q i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local.set $a (i32.xor (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))))
    (local.set $b (i32.xor (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))))
    (local.set $c (i32.xor (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))))
    (local.set $d (i32.xor (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))))
    (i32.store offset=512 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=516 (local.get $p) (i32.load offset=68 (local.get $p)))
    (i32.store offset=520 (local.get $p) (i32.load offset=72 (local.get $p)))
    (i32.store offset=524 (local.get $p) (i32.const 99))
    (i32.store offset=524 (local.get $p) (i32.load offset=76 (local.get $p)))
    (i32.store offset=0 (local.get $p) (local.get $a))
    (i32.store offset=4 (local.get $p) (local.get $b))
    (i32.store offset=8 (local.get $p) (local.get $c))
    (i32.store offset=12 (local.get $p) (local.get $d))
    (i32.xor (call $h (local.get $q) (i32.const 4)) (call $h (i32.add (local.get $q) (i32.const 512)) (i32.const 4))))
  (func $clash (export "clash") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $a f64) (local $b f64) (local $c f64) (local $d f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $a (f64.load offset=256 (local.get $p)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $a)))
    (local.set $c (f64.load offset=0 (local.get $p)))
    (local.set $b (f64.load offset=264 (local.get $p)))
    (f64.store offset=16 (local.get $p) (f64.mul (local.get $k) (local.get $b)))
    (local.set $d (f64.load offset=8 (local.get $p)))
    (f64.store offset=1024 (local.get $p) (f64.add (local.get $k) (local.get $c)))
    (f64.store offset=1032 (local.get $p) (f64.add (local.get $k) (local.get $d)))
    (f64.add (f64.load offset=1032 (local.get $q)) (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2))))
  (func $addrc2 (export "addrc2") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store (i32.add (local.get $p) (i32.const 8)) (f64.mul (local.get $k) (f64.load (i32.add (local.get $p) (i32.const 392)))))
    (f64.store (i32.add (local.get $p) (i32.const 16)) (f64.mul (local.get $k) (f64.load (i32.add (local.get $p) (i32.const 400)))))
    (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2)))
  (func $addrmix (export "addrmix") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=4 (local.get $p) (i32.const 21))
    (i32.store (i32.add (local.get $p) (i32.const 8)) (i32.const -3))
    (i32.store (i32.add (local.get $p) (i32.const 12)) (i32.const 77777))
    (i32.store (i32.add (local.get $p) (i32.const 16)) (i32.const 5))
    (call $h (i32.add (local.get $q) (i32.const 4)) (i32.const 4)))
  (func $rowmat (export "rowmat") (param $p i32) (param $q i32) (param $n i32) (result f64)
    (local $m00 f64) (local $m01 f64) (local $m02 f64) (local $m03 f64) (local $m10 f64) (local $m11 f64) (local $m12 f64) (local $m13 f64) (local $x0 f64) (local $x1 f64) (local $o i32) (local $r i32)
    (local.set $m00 (f64.load offset=256 (local.get $q)))
    (local.set $m01 (f64.load offset=264 (local.get $q)))
    (local.set $m02 (f64.load offset=272 (local.get $q)))
    (local.set $m03 (f64.load offset=280 (local.get $q)))
    (local.set $m10 (f64.load offset=288 (local.get $q)))
    (local.set $m11 (f64.load offset=296 (local.get $q)))
    (local.set $m12 (f64.load offset=304 (local.get $q)))
    (local.set $m13 (f64.load offset=312 (local.get $q)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $r) (local.get $n)))
        (local.set $x0 (f64.load offset=512 (i32.add (local.get $q) (i32.shl (local.get $r) (i32.const 4)))))
        (local.set $x1 (f64.load offset=520 (i32.add (local.get $q) (i32.shl (local.get $r) (i32.const 4)))))
        (local.set $o (i32.add (local.get $p) (i32.shl (local.get $r) (i32.const 5))))
        (f64.store (local.get $o) (f64.add (f64.mul (local.get $x0) (local.get $m00)) (f64.mul (local.get $x1) (local.get $m10))))
        (f64.store (i32.add (local.get $o) (i32.const 8)) (f64.add (f64.mul (local.get $x0) (local.get $m01)) (f64.mul (local.get $x1) (local.get $m11))))
        (f64.store (i32.add (local.get $o) (i32.const 16)) (f64.add (f64.mul (local.get $x0) (local.get $m02)) (f64.mul (local.get $x1) (local.get $m12))))
        (f64.store (i32.add (local.get $o) (i32.const 24)) (f64.add (f64.mul (local.get $x0) (local.get $m03)) (f64.mul (local.get $x1) (local.get $m13))))
        (local.set $r (i32.add (local.get $r) (i32.const 1)))
        (br $l)))
    (call $fs (local.get $q) (i32.shl (local.get $n) (i32.const 2))))
  (func $ptrmix (export "ptrmix") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=504 (local.get $p))))
    (f64.store (i32.add (local.get $p) (i32.const 8)) (f64.mul (local.get $k) (f64.load offset=512 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $swapmem (export "swapmem") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.add (f64.load offset=488 (local.get $p)) (f64.load $b offset=488 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (f64.load $b offset=496 (local.get $p)) (f64.load offset=496 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $outer2 (export "outer2") (param $p i32) (param $q i32) (param $c i32) (param $ki i32) (result f64)
    (local $a1 f64) (local $b1 f64) (local $a2 f64) (local $b2 f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $a1 (f64.load offset=536 (local.get $p)))
    (local.set $b1 (f64.load offset=544 (local.get $p)))
    (f64.store offset=552 (local.get $q) (f64.const 11))
    (local.set $a2 (f64.load offset=552 (local.get $p)))
    (local.set $b2 (f64.load offset=560 (local.get $p)))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (f64.store offset=0 (local.get $p) (f64.add (local.get $a1) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $b1) (local.get $k)))
    (f64.store offset=16 (local.get $p) (f64.mul (local.get $a2) (local.get $k)))
    (f64.store offset=24 (local.get $p) (f64.mul (local.get $b2) (local.get $k)))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 4))))
  (func $sharesplat (export "sharesplat") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.add (f64.load offset=688 (local.get $p)) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.add (f64.load offset=696 (local.get $p)) (local.get $k)))
    (f64.store offset=16 (local.get $p) (f64.add (f64.load offset=704 (local.get $p)) (local.get $k)))
    (f64.store offset=24 (local.get $p) (f64.add (f64.load offset=712 (local.get $p)) (local.get $k)))
    (call $fs (local.get $q) (i32.const 4)))
  (func $sharedlanes (export "sharedlanes") (param $p i32) (param $q i32) (param $c i32) (param $ki i32) (result f64)
    (local $m0 f64) (local $m1 f64) (local $y f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $m0 (f64.load offset=704 (local.get $p)))
    (local.set $m1 (f64.load offset=712 (local.get $p)))
    (local.set $y (f64.load offset=1040 (local.get $q)))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (f64.store offset=0 (local.get $p) (f64.add (f64.mul (local.get $m0) (local.get $k)) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.add (f64.mul (local.get $m1) (local.get $k)) (local.get $k)))
    (f64.store offset=16 (local.get $p) (f64.sub (f64.mul (local.get $m0) (local.get $y)) (local.get $y)))
    (f64.store offset=24 (local.get $p) (f64.sub (f64.mul (local.get $m1) (local.get $y)) (local.get $y)))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 4))))
  (func $byelem4 (export "byelem4") (param $p i32) (param $q i32) (param $n i32) (result i32)
    (local $k f32)
    (local.set $k (f32.convert_i32_s (local.get $n)))
    (f32.store offset=0 (local.get $p) (f32.mul (f32.load offset=1300 (local.get $p)) (local.get $k)))
    (f32.store offset=4 (local.get $p) (f32.mul (f32.load offset=1304 (local.get $p)) (local.get $k)))
    (f32.store offset=8 (local.get $p) (f32.mul (f32.load offset=1308 (local.get $p)) (local.get $k)))
    (f32.store offset=12 (local.get $p) (f32.mul (f32.load offset=1312 (local.get $p)) (local.get $k)))
    (call $h (local.get $q) (i32.const 4)))
  (func $byelemadd (export "byelemadd") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.add (f64.mul (local.get $k) (f64.load offset=584 (local.get $p))) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.add (f64.mul (local.get $k) (f64.load offset=592 (local.get $p))) (local.get $k)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $byelemblock (export "byelemblock") (param $p i32) (param $q i32) (param $c i32) (param $ki i32) (result f64)
    (local $s v128)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $s (f64x2.splat (local.get $k)))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (v128.store (local.get $p) (f64x2.mul (local.get $s) (v128.load offset=600 (local.get $p))))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $byelemtwo (export "byelemtwo") (param $p i32) (param $q i32) (param $x i32) (result f64)
    (v128.store (local.get $p) (f64x2.mul (f64x2.splat (f64.load offset=616 (local.get $p)))
                                          (f64x2.splat (f64.convert_i32_s (local.get $x)))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $byelemlanes (export "byelemlanes") (param $p i32) (param $q i32) (result f64)
    (v128.store (local.get $p) (f64x2.mul (f32x4.splat (f32.load offset=1300 (local.get $p)))
                                          (v128.load offset=624 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $raw (export "raw") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=264 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=272 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 264)) (i32.const 2)))
  (func $multiuse (export "multiuse") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $t f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $t (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=0 (local.get $p) (local.get $t))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (local.get $t) (call $fs (local.get $q) (i32.const 2))))
  (func $fill (export "fill") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (memory.fill (i32.add (local.get $q) (i32.const 264)) (i32.const 0) (i32.const 8))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $loadpast (export "loadpast") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=264 (local.get $p) (f64.const 7))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $storeread (export "storeread") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=512 (local.get $p) (f64.load offset=0 (local.get $q)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (f64.load offset=512 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $waw (export "waw") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=0 (local.get $q) (f64.const 5))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $misalign (export "misalign") (param $p i32) (param $q i32) (result f64)
    (f64.store offset=0 (local.get $p) (f64.load offset=256 (local.get $p)))
    (f64.store offset=12 (local.get $p) (f64.load offset=264 (local.get $p)))
    (call $fs (local.get $q) (i32.const 3)))
  (func $mixedsize (export "mixedsize") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.const 5))
    (i32.store offset=4 (local.get $p) (i32.const 6))
    (i32.store offset=8 (local.get $p) (i32.const 7))
    (i64.store offset=12 (local.get $p) (i64.const 0x1122334455667788))
    (call $h (local.get $q) (i32.const 5)))
  (func $gap (export "gap") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $p) (i32.load offset=68 (local.get $p)))
    (i32.store offset=12 (local.get $p) (i32.load offset=76 (local.get $p)))
    (i32.store offset=16 (local.get $p) (i32.load offset=80 (local.get $p)))
    (call $h (local.get $q) (i32.const 5)))
  (func $twoptr (export "twoptr") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $q) (i32.load offset=68 (local.get $p)))
    (i32.store offset=8 (local.get $p) (i32.load offset=72 (local.get $p)))
    (i32.store offset=12 (local.get $q) (i32.load offset=76 (local.get $p)))
    (call $h (local.get $q) (i32.const 4)))
  (func $narrow (export "narrow") (param $p i32) (param $q i32) (param $x i64) (result i32)
    (i64.store32 offset=0 (local.get $p) (i64.add (local.get $x) (i64.const 1)))
    (i64.store32 offset=4 (local.get $p) (i64.add (local.get $x) (i64.const 2)))
    (i64.store32 offset=8 (local.get $p) (i64.add (local.get $x) (i64.const 3)))
    (i64.store32 offset=12 (local.get $p) (i64.add (local.get $x) (i64.const 4)))
    (call $h (local.get $q) (i32.const 4)))
  (func $consts2 (export "consts2") (param $p i32) (param $q i32) (result i32)
    (i64.store offset=0 (local.get $p) (i64.const 3))
    (i64.store offset=8 (local.get $p) (i64.const 9))
    (call $h (local.get $q) (i32.const 4)))
  (func $mixed (export "mixed") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2)))
  (func $wrapdown (export "wrapdown") (param $p i32) (param $q i32) (result i32)
    (i32.store (i32.add (local.get $p) (i32.const 8)) (i32.const 31))
    (i32.store offset=12 (local.get $p) (i32.const 32))
    (i32.store offset=16 (local.get $p) (i32.const 33))
    (i32.store offset=20 (local.get $p) (i32.const 34))
    (call $h (i32.add (local.get $q) (i32.const 8)) (i32.const 4)))
  (func $outerstore (export "outerstore") (param $p i32) (param $q i32) (param $c i32) (result f64)
    (local $a f64) (local $b f64)
    (local.set $a (f64.load offset=408 (local.get $p)))
    (f64.store offset=416 (local.get $q) (f64.const 5))
    (local.set $b (f64.load offset=416 (local.get $p)))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $opblock (export "opblock") (param $p i32) (param $q i32) (param $c i32) (param $ki i32) (result f64)
    (local $a f64) (local $b f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $a (f64.mul (local.get $k) (f64.load offset=424 (local.get $p))))
    (local.set $b (f64.mul (local.get $k) (f64.load offset=432 (local.get $p))))
    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))
    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))
    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2))))
  (func $lateaddr (export "lateaddr") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $a f64) (local $b f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $b (f64.load (i32.add (local.get $p) (i32.const 448))))
    (local.set $a (f64.load (i32.add (local.get $p) (i32.const 440))))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (local.get $a)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $b)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $twomem (export "twomem") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.const 41))
    (i32.store offset=4 (local.get $p) (i32.const 42))
    (i32.store $b offset=8 (local.get $p) (i32.const 43))
    (i32.store $b offset=12 (local.get $p) (i32.const 44))
    (i32.add (i32.mul (call $h (local.get $q) (i32.const 2)) (i32.const 961))
      (i32.add (i32.mul (i32.load $b offset=8 (local.get $q)) (i32.const 31)) (i32.load $b offset=12 (local.get $q)))))
  (func $narrowlane (export "narrowlane") (param $p i32) (param $q i32) (param $x i64) (result i32)
    (i64.store offset=0 (local.get $p) (i64.add (local.get $x) (i64.load offset=456 (local.get $p))))
    (i64.store offset=8 (local.get $p) (i64.add (local.get $x) (i64.load32_s offset=464 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $splitload (export "splitload") (param $p i32) (param $q i32) (param $c i32) (param $ki i32) (result f64)
    (local $a f64) (local $b f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $b (f64.load offset=480 (local.get $p)))
    (if (local.get $c) (then (f64.store offset=480 (local.get $q) (f64.const 7))))
    (local.set $a (f64.load offset=472 (local.get $p)))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (local.get $a)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $b)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $sharestore (export "sharestore") (param $p i32) (param $q i32) (param $ki i32) (result f64)
    (local $a1 f64) (local $b1 f64) (local $a2 f64) (local $b2 f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (local.set $a1 (f64.load offset=520 (local.get $p)))
    (local.set $b1 (f64.load offset=528 (local.get $p)))
    (f64.store offset=520 (local.get $q) (f64.const 9))
    (local.set $a2 (f64.load offset=520 (local.get $p)))
    (local.set $b2 (f64.load offset=528 (local.get $p)))
    (f64.store offset=0 (local.get $p) (f64.add (local.get $a1) (local.get $k)))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $b1) (local.get $k)))
    (f64.store offset=16 (local.get $p) (f64.mul (local.get $a2) (local.get $k)))
    (f64.store offset=24 (local.get $p) (f64.mul (local.get $b2) (local.get $k)))
    (call $fs (local.get $q) (i32.const 4)))
  (func $wide64 (export "wide64") (param $x i64) (result i32)
    (i32.store $w offset=0 (local.get $x) (i32.const 51))
    (i32.store $w offset=4 (local.get $x) (i32.const 52))
    (i32.store $w offset=8 (local.get $x) (i32.const 53))
    (i32.store $w (i64.add (local.get $x) (i64.const 0x10000000c)) (i32.const 54))
    (i32.load $w offset=4 (local.get $x)))
  (func $aftercall (export "aftercall") (param $p i32) (param $q i32) (param $s i32) (param $i i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (call_indirect (type $wr) (local.get $s) (local.get $k) (local.get $i))
    (f64.store offset=0 (local.get $p) (f64.load offset=0 (local.get $s)))
    (f64.store offset=8 (local.get $p) (f64.load offset=8 (local.get $s)))
    (call $fs (local.get $q) (i32.const 2)))
  (func $aftercall2 (export "aftercall2") (param $p i32) (param $q i32) (param $i i32) (param $ki i32) (result f64)
    (local $k f64)
    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))
    (call_indirect (type $wr) (i32.add (local.get $p) (i32.const 640)) (local.get $k) (local.get $i))
    (f64.store offset=0 (local.get $p) (f64.load offset=640 (local.get $p)))
    (f64.store offset=8 (local.get $p) (f64.load offset=648 (local.get $p)))
    (call $fs (local.get $q) (i32.const 2)))
)
