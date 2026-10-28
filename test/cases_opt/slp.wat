;; A block's stores to adjacent addresses, as many as fill a vector, whose
;; values are alike lane by lane -- one operation of operands themselves
;; alike: loads of adjacent addresses, one value in every lane, constants --
;; are one vector computation and one vector store (opt.c pass_slp): a pair
;; of doubles scaled by k, four words added, subtracted, and'ed, or'd,
;; xor'd, copied or set to constants, a scalar added to four words, a
;; difference, quotient, minimum and maximum of doubles, a sum times k, k
;; times x plus a constant, a sum five adds deep, offsets past what ldr q
;; encodes, stores in either order, a load of a lane's address between, a
;; store into a lane's source after it is read; and a commutative
;; operation's operands turned round in a lane, found by which of them fits
;; lane 0's first -- a factor, a loaded scalar, a load through either of two
;; pointers, a product, a sum of products.  Not a sum six adds deep, nor
;; where a lane reads what another lane stored, nor a lane's value or load
;; is read elsewhere, nor where a memory.fill, a store or a load that may
;; touch a lane stands between, nor across a gap, a misaligned offset or two
;; pointers, nor for loads narrower than their lanes, from two pointers, a
;; stride apart or in another block, nor for stores narrower than their
;; values or of two widths, nor two constants a store pair writes as
;; cheaply, nor lanes of different operations or a difference turned round
;; in one, nor a 64-bit multiply, which has no vector instruction.  At -O2
;; -fno-mem too, where a lane's load of what another lane stored is not
;; forwarded the value but read, after the store.  Each function stores
;; through $p and reads back through $q, the same address, which forwarding
;; cannot see.  Made by test/gen_slp.py, which works the answers out on a
;; model of the memory.
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
;; shape: deep5 \sadd\.4s\s=5
;; shape: deep6 \sadd\.4s\s=0
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
;; run: deep5 0 0 => i32:-394940232
;; run: deep6 0 0 => i32:-394940232
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

(module
  (memory 1)
  (data (i32.const 0) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\39\30\00\00\ea\a9\37\1e\9b\23\6f\3c\4c\9d\a6\5a\fd\16\de\78\ae\90\15\17\5f\0a\4d\35\10\84\84\53\c1\fd\bb\71\72\77\f3\0f\23\f1\2a\2e\d4\6a\62\4c\85\e4\99\6a\36\5e\d1\08\e7\d7\08\27\98\51\40\45\49\cb\77\63\fa\44\af\01\ab\be\e6\1f\5c\38\1e\3e\0d\b2\55\5c\be\2b\8d\7a\6f\a5\c4\18\20\1f\fc\36\d1\98\33\55\82\12\6b\73\33\8c\a2\11\e4\05\da\2f\95\7f\11\4e\46\f9\48\6c\f7\72\80\0a\a8\ec\b7\28\59\66\ef\46\0a\e0\26\65\bb\59\5e\03\6c\d3\95\21\1d\4d\cd\3f\ce\c6\04\5e\7f\40\3c\7c\30\ba\73\1a\e1\33\ab\38\92\ad\e2\56\43\27\1a\75\f4\a0\51\13\a5\1a\89\31\56\94\c0\4f\07\0e\f8\6d\b8\87\2f\0c\00\00\00\00\00\00\0a\c0\00\00\00\00\00\00\f6\3f\00\00\00\00\00\00\18\40\00\00\00\00\00\00\00\c0\00\00\00\00\00\00\05\40\00\00\00\00\00\00\1d\40\00\00\00\00\00\00\e8\bf\00\00\00\00\00\00\0f\40\00\00\00\00\00\00\21\40\00\00\00\00\00\00\e0\3f\00\00\00\00\00\80\14\40\00\00\00\00\00\00\07\c0\00\00\00\00\00\00\fc\3f\00\00\00\00\00\80\19\40\00\00\00\00\00\00\fa\bf\00\00\00\00\00\00\08\40\00\00\00\00\00\80\1e\40\00\00\00\00\00\00\d8\bf\00\00\00\00\00\00\11\40\00\00\00\00\00\c0\21\40\00\00\00\00\00\00\ec\3f\00\00\00\00\00\00\16\40\00\00\00\00\00\00\04\c0\00\00\00\00\00\00\01\40\00\00\00\00\00\00\1b\40\00\00\00\00\00\00\f4\bf\00\00\00\00\00\00\0b\40\00\00\00\00\00\00\20\40\00\00\00\00\00\00\00\00\00\00\00\00\00\80\12\40\00\00\00\00\00\80\22\40\00\00\00\00\00\00\f4\3f\00\00\00\00\00\80\17\40\00\00\00\00\00\00\01\c0\00\00\00\00\00\00\04\40\00\00\00\00\00\80\1c\40\00\00\00\00\00\00\ec\bf\00\00\00\00\00\00\0e\40\00\00\00\00\00\c0\20\40\00\00\00\00\00\00\d8\3f\00\00\00\00\00\00\14\40\00\00\00\00\00\00\08\c0\00\00\00\00\00\00\fa\3f\00\00\00\00\00\00\19\40\00\00\00\00\00\00\fc\bf\00\00\00\00\00\00\07\40\00\00\00\00\00\00\1e\40\00\00\00\00\00\00\e0\bf\00\00\00\00\00\80\10\40\00\00\00\00\00\80\21\40\00\00\00\00\00\00\e8\3f\00\00\00\00\00\80\15\40\00\00\00\00\00\00\05\c0\00\00\00\00\00\00\00\40\00\00\00\00\00\80\1a\40\00\00\00\00\00\00\f6\bf\00\00\00\00\00\00\0a\40\00\00\00\00\00\80\1f\40\00\00\00\00\00\00\c0\bf\00\00\00\00\00\00\12\40\00\00\00\00\00\40\22\40\00\00\00\00\00\00\f2\3f\00\00\00\00\00\00\17\40\00\00\00\00\00\00\02\c0\00\00\00\00\00\00\03\40\00\00\00\00\00\00\1c\40\00\00\00\00\00\00\f0\bf\00\00\00\00\00\00\0d\40\00\00\00\00\00\80\20\40\00\00\00\00\00\00\d0\3f\00\00\00\00\00\80\13\40\00\00\00\00\00\00\09\c0\00\00\00\00\00\00\f8\3f\00\00\00\00\00\80\18\40\00\00\00\00\00\00\fe\bf\00\00\00\00\00\00\06\40\00\00\00\00\00\80\1d\40\00\00\00\00\00\00\e4\bf\00\00\00\00\00\00\10\40\00\00\00\00\00\40\21\40\00\00\00\00\00\00\e4\3f\00\00\00\00\00\00\15\40\00\00\00\00\00\00\06\c0\00\00\00\00\00\00\fe\3f\00\00\00\00\00\00\1a\40\00\00\00\00\00\00\f8\bf\00\00\00\00\00\00\09\40\00\00\00\00\00\00\1f\40\00\00\00\00\00\00\d0\bf\00\00\00\00\00\80\11\40\00\00\00\00\00\00\22\40\00\00\00\00\00\00\f0\3f\00\00\00\00\00\80\16\40\00\00\00\00\00\00\03\c0\00\00\00\00\00\00\02\40\00\00\00\00\00\80\1b\40\00\00\00\00\00\00\f2\bf\00\00\00\00\00\00\0c\40\00\00\00\00\00\40\20\40\00\00\00\00\00\00\c0\3f\00\00\00\00\00\00\13\40\00\00\00\00\00\00\0a\c0\00\00\00\00\00\00\f6\3f\00\00\00\00\00\00\18\40\00\00\00\00\00\00\00\c0\00\00\00\00\00\00\05\40\00\00\00\00\00\00\1d\40\00\00\00\00\00\00\e8\bf\00\00\00\00\00\00\0f\40\00\00\00\00\00\00\21\40\00\00\00\00\00\00\e0\3f\00\00\00\00\00\80\14\40\00\00\00\00\00\00\07\c0\00\00\00\00\00\00\fc\3f\00\00\00\00\00\80\19\40\00\00\00\00\00\00\fa\bf\00\00\00\00\00\00\08\40\00\00\00\00\00\80\1e\40\00\00\00\00\00\00\d8\bf\00\00\00\00\00\00\11\40\00\00\00\00\00\c0\21\40\00\00\00\00\00\00\ec\3f\00\00\00\00\00\00\16\40\00\00\00\00\00\00\04\c0\00\00\00\00\00\00\01\40\00\00\00\00\00\00\1b\40\00\00\00\00\00\00\f4\bf\00\00\00\00\00\00\0b\40\00\00\00\00\00\00\20\40\00\00\00\00\00\00\00\00\00\00\00\00\00\80\12\40\00\00\00\00\00\80\22\40\00\00\00\00\00\00\f4\3f\00\00\00\00\00\80\17\40\00\00\00\00\00\00\01\c0\00\00\00\00\00\00\04\40\00\00\00\00\00\80\1c\40\00\00\00\00\00\00\ec\bf\00\00\00\00\00\00\0e\40\00\00\00\00\00\c0\20\40\00\00\00\00\00\00\d8\3f\00\00\00\00\00\00\14\40\00\00\00\00\00\00\08\c0\00\00\00\00\00\00\fa\3f\00\00\00\00\00\00\19\40\00\00\00\00\00\00\fc\bf\00\00\00\00\00\00\07\40\00\00\00\00\00\00\1e\40\00\00\00\00\00\00\e0\bf\00\00\00\00\00\80\10\40\00\00\00\00\00\80\21\40\00\00\00\00\00\00\e8\3f\00\00\00\00\00\80\15\40\00\00\00\00\00\00\05\c0\00\00\00\00\00\00\00\40\00\00\00\00\00\80\1a\40\00\00\00\00\00\00\f6\bf\00\00\00\00\00\00\0a\40\00\00\00\00\00\80\1f\40\00\00\00\00\00\00\c0\bf\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\07\00\00\00\3e\9e\00\00\75\3c\01\00\ac\da\01\00\e3\78\02\00\1a\17\03\00\51\b5\03\00\88\53\04\00\bf\f1\04\00\f6\8f\05\00\2d\2e\06\00\64\cc\06\00\9b\6a\07\00\d2\08\08\00\09\a7\08\00\40\45\09\00\77\e3\09\00\ae\81\0a\00\e5\1f\0b\00\1c\be\0b\00\53\5c\0c\00\8a\fa\0c\00\c1\98\0d\00\f8\36\0e\00\2f\d5\0e\00\66\73\0f\00\9d\11\10\00\d4\af\10\00\0b\4e\11\00\42\ec\11\00\79\8a\12\00\b0\28\13\00\e7\c6\13\00\1e\65\14\00\55\03\15\00\8c\a1\15\00\c3\3f\16\00\fa\dd\16\00\31\7c\17\00\68\1a\18\00\9f\b8\18\00\d6\56\19\00\0d\f5\19\00\44\93\1a\00\7b\31\1b\00\b2\cf\1b\00\e9\6d\1c\00\20\0c\1d\00\57\aa\1d\00\8e\48\1e\00\c5\e6\1e\00\fc\84\1f\00\33\23\20\00\6a\c1\20\00\a1\5f\21\00\d8\fd\21\00\0f\9c\22\00\46\3a\23\00\7d\d8\23\00\b4\76\24\00\eb\14\25\00\22\b3\25\00\59\51\26\00\90\ef\26\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
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
  (func $deep5 (export "deep5") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (i32.load offset=1920 (local.get $p))) (i32.load offset=1984 (local.get $p))) (i32.load offset=2048 (local.get $p))) (i32.load offset=2112 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))) (i32.load offset=1924 (local.get $p))) (i32.load offset=1988 (local.get $p))) (i32.load offset=2052 (local.get $p))) (i32.load offset=2116 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))) (i32.load offset=1928 (local.get $p))) (i32.load offset=1992 (local.get $p))) (i32.load offset=2056 (local.get $p))) (i32.load offset=2120 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (i32.load offset=1932 (local.get $p))) (i32.load offset=1996 (local.get $p))) (i32.load offset=2060 (local.get $p))) (i32.load offset=2124 (local.get $p))))
    (call $h (local.get $q) (i32.const 4)))
  (func $deep6 (export "deep6") (param $p i32) (param $q i32) (result i32)
    (i32.store offset=0 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))) (i32.load offset=1920 (local.get $p))) (i32.load offset=1984 (local.get $p))) (i32.load offset=2048 (local.get $p))) (i32.load offset=2112 (local.get $p))) (i32.load offset=2176 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))) (i32.load offset=1924 (local.get $p))) (i32.load offset=1988 (local.get $p))) (i32.load offset=2052 (local.get $p))) (i32.load offset=2116 (local.get $p))) (i32.load offset=2180 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))) (i32.load offset=1928 (local.get $p))) (i32.load offset=1992 (local.get $p))) (i32.load offset=2056 (local.get $p))) (i32.load offset=2120 (local.get $p))) (i32.load offset=2184 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))) (i32.load offset=1932 (local.get $p))) (i32.load offset=1996 (local.get $p))) (i32.load offset=2060 (local.get $p))) (i32.load offset=2124 (local.get $p))) (i32.load offset=2188 (local.get $p))))
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
)
