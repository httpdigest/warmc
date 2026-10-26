;; An i32 parameter's high half is whatever its caller's register held:
;; the C conventions leave it to the caller, and warmc_invoke takes each
;; argument as 64 bits, which an embedder fills from an int32_t sign-extended
;; as readily as zero-extended.  The command line passes an argument's whole
;; 64 bits, so each export here runs twice, once with its arguments as they
;; are and once with 0x5a5a in their high halves, and must answer the same.
;; What reads an i32 reads its low half: a runtime helper whose parameter is
;; a u64, for a memory's or a table's 64-bit index too, gets it zero-extended
;; (isel.c emit_call_args_core's zext) -- memory.grow, the bulk operations of
;; another memory than the first, memory.init, every table operation and
;; array.new -- of floats too, whose value comes ahead of the length --
;; failed or trapped with the high half -- and on x64, whose code
;; keeps every i32 zero-extended, a function C can reach makes its i32
;; parameters so on entry (isel_x64.c), where loads, call_indirect, arrays
;; and unsigned conversions read the whole register.  On AArch64 only an i32
;; a w-register write did not make takes the mov that makes its high half
;; zero: grow's parameter does, growadd's sum does not.
;;
;; shape: grow mov\tw(\d+), w\1$=1
;; shape: growadd mov\tw(\d+), w\1$=0
;;
;; run: load8 100 => i32:42
;; run: load8 99342593556580 => i32:42
;; run: load32 100 => i32:757869354
;; run: load32 99342593556580 => i32:757869354
;; run: loadoff 96 => i32:42
;; run: loadoff 99342593556576 => i32:42
;; run: store 100 => i32:7
;; run: store 99342593556580 => i32:7
;; run: loadm1 0 => i32:0
;; run: loadm1 99342593556480 => i32:0
;; run: grow 1 => i32:2
;; run: grow 99342593556481 => i32:2
;; run: growadd 0 => i32:2
;; run: growadd 99342593556480 => i32:2
;; run: grow1 1 => i32:1
;; run: grow1 99342593556481 => i32:1
;; run: copy 0 100 4 => i32:42
;; run: copy 99342593556480 99342593556580 99342593556484 => i32:42
;; run: copy1 0 1 4 => i32:0
;; run: copy1 99342593556480 99342593556481 99342593556484 => i32:0
;; run: fill 0 9 4 => i32:9
;; run: fill 99342593556480 99342593556489 99342593556484 => i32:9
;; run: fill1 0 9 4 => i32:0
;; run: fill1 99342593556480 99342593556489 99342593556484 => i32:0
;; run: init 0 1 2 => i32:2
;; run: init 99342593556480 99342593556481 99342593556482 => i32:2
;; run: callind 1 => i32:201
;; run: callind 99342593556481 => i32:201
;; run: tget 1 => i32:0
;; run: tget 99342593556481 => i32:0
;; run: tset 1 => i32:0
;; run: tset 99342593556481 => i32:0
;; run: tgrow 1 => i32:4
;; run: tgrow 99342593556481 => i32:4
;; run: tfill 1 2 => i32:0
;; run: tfill 99342593556481 99342593556482 => i32:0
;; run: tcopy 0 1 2 => i32:0
;; run: tcopy 99342593556480 99342593556481 99342593556482 => i32:0
;; run: tinit 0 0 2 => i32:0
;; run: tinit 99342593556480 99342593556480 99342593556482 => i32:0
;; run: anew 5 => i32:5
;; run: anew 99342593556485 => i32:5
;; run: anewf 5 => i32:5
;; run: anewf 99342593556485 => i32:5
;; run: aget 1 => i32:9
;; run: aget 99342593556481 => i32:9
;; run: aset 1 => i32:5
;; run: aset 99342593556481 => i32:5
;; run: afill 1 2 => i32:5
;; run: afill 99342593556481 99342593556482 => i32:5
;; run: brtable 1 => i32:11
;; run: brtable 99342593556481 => i32:11
;; run: sel 0 => i32:2
;; run: sel 99342593556480 => i32:2
;; run: divu 10 3 => i32:3
;; run: divu 99342593556490 99342593556483 => i32:3
;; run: remu 10 3 => i32:1
;; run: remu 99342593556490 99342593556483 => i32:1
;; run: shr 256 4 => i32:16
;; run: shr 99342593556736 99342593556484 => i32:16
;; run: rotr 1 1 => i32:-2147483648
;; run: rotr 99342593556481 99342593556481 => i32:-2147483648
;; run: clz 1 => i32:31
;; run: clz 99342593556481 => i32:31
;; run: ctz 0 => i32:32
;; run: ctz 99342593556480 => i32:32
;; run: popcnt 3 => i32:2
;; run: popcnt 99342593556483 => i32:2
;; run: cvtu 5 => f64:0x4014000000000000
;; run: cvtu 99342593556485 => f64:0x4014000000000000
;; run: cvtfu 5 => f32:0x40a00000
;; run: cvtfu 99342593556485 => f32:0x40a00000
;; run: ltu 5 7 => i32:1
;; run: ltu 99342593556485 99342593556487 => i32:1
;; run: eqz 0 => i32:1
;; run: eqz 99342593556480 => i32:1
;; run: brif 0 => i32:1
;; run: brif 99342593556480 => i32:1
;; run: ifz 0 => i32:2
;; run: ifz 99342593556480 => i32:2
;; run: glob 5 => i64:5
;; run: glob 99342593556485 => i64:5
;; run: splat 5 => i64:21474836485
;; run: splat 99342593556485 => i64:21474836485
;; run: ret 5 => i32:5
;; run: ret 99342593556485 => i32:5
;; run: retx 5 => i64:5
;; run: retx 99342593556485 => i64:5
(module
  (type $ft (func (param i32) (result i32)))
  (type $arr (array (mut i32)))
  (type $farr (array (mut f64)))
  (memory 2)
  (memory $m1 1)
  (table $t 4 funcref)
  (table $t2 4 funcref)
  (elem (table $t) (i32.const 0) func $f0 $f1)
  (elem $pe func $f0 $f1)
  (data $pd "\01\02\03\04")
  (data (i32.const 100) "\2a\2b\2c\2d\2e\2f\30\31")
  (func $f0 (type $ft) (i32.add (local.get 0) (i32.const 100)))
  (func $f1 (type $ft) (i32.add (local.get 0) (i32.const 200)))
  (func (export "load8") (param $a i32) (result i32) (i32.load8_u (local.get $a)))
  (func (export "load32") (param $a i32) (result i32) (i32.load (local.get $a)))
  (func (export "loadoff") (param $a i32) (result i32) (i32.load8_u offset=4 (local.get $a)))
  (func (export "store") (param $a i32) (result i32) (i32.store8 (local.get $a) (i32.const 7)) (i32.load8_u (i32.const 100)))
  (func (export "loadm1") (param $a i32) (result i32) (i32.load8_u $m1 (local.get $a)))
  (func $grow (export "grow") (param $n i32) (result i32) (memory.grow (local.get $n)))
  (func $growadd (export "growadd") (param $n i32) (result i32)
    (memory.grow (i32.add (local.get $n) (i32.const 1))))
  (func (export "grow1") (param $n i32) (result i32) (memory.grow $m1 (local.get $n)))
  (func (export "copy") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy (local.get $d) (local.get $s) (local.get $n)) (i32.load8_u (i32.const 0)))
  (func (export "copy1") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy $m1 $m1 (local.get $d) (local.get $s) (local.get $n)) (i32.const 0))
  (func (export "fill") (param $d i32) (param $v i32) (param $n i32) (result i32)
    (memory.fill (local.get $d) (local.get $v) (local.get $n)) (i32.load8_u (i32.const 0)))
  (func (export "fill1") (param $d i32) (param $v i32) (param $n i32) (result i32)
    (memory.fill $m1 (local.get $d) (local.get $v) (local.get $n)) (i32.const 0))
  (func (export "init") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.init $pd (local.get $d) (local.get $s) (local.get $n)) (i32.load8_u (i32.const 0)))
  (func (export "callind") (param $i i32) (result i32) (call_indirect $t (type $ft) (i32.const 1) (local.get $i)))
  (func (export "tget") (param $i i32) (result i32) (ref.is_null (table.get $t (local.get $i))))
  (func (export "tset") (param $i i32) (result i32) (table.set $t (local.get $i) (ref.null func)) (i32.const 0))
  (func (export "tgrow") (param $n i32) (result i32) (table.grow $t (ref.null func) (local.get $n)))
  (func (export "tfill") (param $i i32) (param $n i32) (result i32) (table.fill $t (local.get $i) (ref.null func) (local.get $n)) (i32.const 0))
  (func (export "tcopy") (param $d i32) (param $s i32) (param $n i32) (result i32) (table.copy $t2 $t (local.get $d) (local.get $s) (local.get $n)) (i32.const 0))
  (func (export "tinit") (param $d i32) (param $s i32) (param $n i32) (result i32) (table.init $t2 $pe (local.get $d) (local.get $s) (local.get $n)) (i32.const 0))
  (func (export "anew") (param $n i32) (result i32) (array.len (array.new $arr (i32.const 3) (local.get $n))))
  (func (export "anewf") (param $n i32) (result i32) (array.len (array.new $farr (f64.const 1.5) (local.get $n))))
  (func (export "aget") (param $i i32) (result i32) (array.get $arr (array.new $arr (i32.const 9) (i32.const 4)) (local.get $i)))
  (func (export "aset") (param $i i32) (result i32) (local $a (ref $arr)) (local.set $a (array.new $arr (i32.const 9) (i32.const 4))) (array.set $arr (local.get $a) (local.get $i) (i32.const 5)) (array.get $arr (local.get $a) (i32.const 1)))
  (func (export "afill") (param $i i32) (param $n i32) (result i32) (local $a (ref $arr)) (local.set $a (array.new $arr (i32.const 9) (i32.const 4))) (array.fill $arr (local.get $a) (local.get $i) (i32.const 5) (local.get $n)) (array.get $arr (local.get $a) (i32.const 1)))
  (func (export "brtable") (param $i i32) (result i32)
    (block $b2 (block $b1 (block $b0 (br_table $b0 $b1 $b2 (local.get $i))) (return (i32.const 10))) (return (i32.const 11))) (i32.const 12))
  (func (export "sel") (param $c i32) (result i32) (select (i32.const 1) (i32.const 2) (local.get $c)))
  (func (export "divu") (param $a i32) (param $b i32) (result i32) (i32.div_u (local.get $a) (local.get $b)))
  (func (export "remu") (param $a i32) (param $b i32) (result i32) (i32.rem_u (local.get $a) (local.get $b)))
  (func (export "shr") (param $a i32) (param $b i32) (result i32) (i32.shr_u (local.get $a) (local.get $b)))
  (func (export "rotr") (param $a i32) (param $b i32) (result i32) (i32.rotr (local.get $a) (local.get $b)))
  (func (export "clz") (param $a i32) (result i32) (i32.clz (local.get $a)))
  (func (export "ctz") (param $a i32) (result i32) (i32.ctz (local.get $a)))
  (func (export "popcnt") (param $a i32) (result i32) (i32.popcnt (local.get $a)))
  (func (export "cvtu") (param $a i32) (result f64) (f64.convert_i32_u (local.get $a)))
  (func (export "cvtfu") (param $a i32) (result f32) (f32.convert_i32_u (local.get $a)))
  (func (export "ltu") (param $a i32) (param $b i32) (result i32) (i32.lt_u (local.get $a) (local.get $b)))
  (func (export "eqz") (param $a i32) (result i32) (i32.eqz (local.get $a)))
  (func (export "brif") (param $a i32) (result i32) (block (br_if 0 (local.get $a)) (return (i32.const 1))) (i32.const 2))
  (func (export "ifz") (param $a i32) (result i32) (if (result i32) (local.get $a) (then (i32.const 1)) (else (i32.const 2))))
  (func (export "glob") (param $a i32) (result i64) (global.set $g (local.get $a)) (i64.extend_i32_u (global.get $g)))
  (global $g (mut i32) (i32.const 0))
  (func (export "splat") (param $a i32) (result i64) (i64x2.extract_lane 0 (i32x4.splat (local.get $a))))
  (func (export "ret") (param $a i32) (result i32) (local.get $a))
  (func (export "retx") (param $a i32) (result i64) (i64.extend_i32_u (call $id (local.get $a))))
  (func $id (param i32) (result i32) (local.get 0))
)
