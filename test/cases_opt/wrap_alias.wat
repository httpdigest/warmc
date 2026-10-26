;; Wraps whose readers take the low half only (isel.c analyse_wrap_alias,
;; ir.c ir_reads_low32): no instruction, each reader reading the i64's own
;; register, its high half set, the i64 read again after.  A w-register op
;; (add), one in both arms of a branch, blocks past the wrap's (arms),
;; stores of 4 bytes and 1 and an address (mem), both extends (ext: the one
;; mov left is extendu's own), a compare and a select (sel), a shift's amount
;; and operand and a rotate (shifts), clz, ctz and popcnt (bits), conversions
;; to floats and an f32's bits (conv), a 4-byte global (glob), the i64 kept
;; across a call (across), an indirect call's table index, the high bits
;; of a function value as Go's closure calls take it (icall).  And the
;; wraps that are made: an argument, which
;; the callee reads as a zero-extended i32 (arg); a branch's own operand,
;; made well before it, which the i64 need not outlive (branch); and two
;; edges' arguments into a join that divides (edge).
;;
;; shape: add mov\tw\d+, w\d+$=0
;; shape: arms mov\tw\d+, w\d+$=0
;; shape: mem mov\tw\d+, w\d+$=0
;; shape: ext mov\tw\d+, w\d+$=1
;; shape: sel mov\tw\d+, w\d+$=0
;; shape: bits mov\tw\d+, w\d+$=0
;; shape: conv mov\tw\d+, w\d+$=0
;; shape: glob mov\tw\d+, w\d+$=0
;; shape: across mov\tw\d+, w\d+$=0
;; shape: arg mov\tw\d+, w\d+$=1
;; shape: edge mov\tw\d+, w\d+$=2
;; shape: icall mov\tw\d+, w\d+$=0
;;
;; run: add 1311768467463790320 1147797409030816545 => i64:2459565878212593732
;; run: add -5 3 => i64:4294967288
;; run: arms 9223372036854775793 1 => i64:-9223372032559808721
;; run: arms 9223372036854775793 0 => i64:-9223372032559808723
;; run: arms 0 0 => i64:4294967295
;; run: mem 1311768467463790320 => i64:1311769569571487440
;; run: mem -1 => i64:4294967549
;; run: ext 9223372030412324864 => i64:-9223372036854775806
;; run: ext 6442450942 => i64:12884901886
;; run: sel 6442450943 0 => i64:8589934590
;; run: sel 10737418240 5 => i64:10737418252
;; run: shifts 1311768467463790320 28 => i64:1311768502864368565
;; run: shifts -2147483647 -2 => i64:36238786563
;; run: bits 65536 => i64:-9223372032559731361
;; run: bits 0 => i64:-9223372032559805280
;; run: bits 4294967295 => i64:-9223372028264521217
;; run: conv 1078530011 => f64:0x41fc0db2f94243f7
;; run: conv 8070450535460765696 => f64:0x43dc0000008fa000
;; run: glob 4886718345 => i64:-3111465198
;; run: glob 0 => i64:-8589934592
;; run: arg 1311768467463790320 0 => i64:-57325811468633152
;; run: arg 1311768467463790320 1 => i64:-57325814064702256
;; run: across 1311768467463790320 0 => i64:-3361406767988917832
;; run: across 1311768467463790320 1 => i64:-3361406767988917837
;; run: branch 4294967296 0 => i64:0
;; run: branch 4294967296 3 => i64:25769803779
;; run: branch -7 7 => i64:287
;; run: edge 4886718345 1 => i64:22263837878
;; run: edge 4886718345 0 => i64:22526308102
;; run: icall -281474976636091 => i64:-281474976636081
;; run: icall 9223090561878065218 => i64:9223090561878065233
;; run: icall 131072 => trap: undefined element
(module
  (type $u (func (param i32) (result i64)))
  (memory 1)
  (global $g (mut i32) (i32.const 0))
  (table 2 funcref)
  (elem (i32.const 0) $triple $twice)
  ;; called through the table, so that no call is inlined: its argument
  ;; arrives as an i32 whose high half the callee takes for zero
  (func $triple (type $u) (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 3)))
  (func $twice (type $u) (i64.shl (i64.extend_i32_u (local.get 0)) (i64.const 1)))

  ;; a w-register op, the i64 read after
  (func $add (export "add") (param $a i64) (param $b i64) (result i64) (local $x i64)
    (local.set $x (i64.add (local.get $a) (local.get $b)))
    (i64.add (local.get $x)
      (i64.extend_i32_u (i32.mul (i32.wrap_i64 (local.get $x)) (i32.const 3)))))

  ;; read in the arms of a branch, blocks past the wrap's
  (func $arms (export "arms") (param $a i64) (param $p i64) (result i64) (local $x i64) (local $w i32) (local $r i32)
    (local.set $x (i64.mul (local.get $a) (i64.const 7)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (if (i64.ne (local.get $p) (i64.const 0))
      (then (local.set $r (i32.add (local.get $w) (i32.const 1))))
      (else (local.set $r (i32.sub (local.get $w) (i32.const 1)))))
    (i64.add (local.get $x) (i64.extend_i32_u (local.get $r))))

  ;; stored as 4 bytes and as 1, and an address whose high half is set
  (func $mem (export "mem") (param $a i64) (result i64) (local $x i64) (local $w i32)
    (local.set $x (i64.or (local.get $a) (i64.const 0x0000_0700_0000_0000)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (i32.store offset=4096 (i32.and (local.get $w) (i32.const 0xffc)) (local.get $w))
    (i32.store8 offset=8192 (i32.and (local.get $w) (i32.const 0xfff)) (local.get $w))
    (i64.add (local.get $x)
      (i64.add
        (i64.extend_i32_u (i32.load offset=4096 (i32.and (local.get $w) (i32.const 0xffc))))
        (i64.load8_u offset=8192 (i32.wrap_i64 (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0fff)))))))

  ;; extended back both ways
  (func $ext (export "ext") (param $a i64) (result i64) (local $x i64)
    (local.set $x (i64.add (local.get $a) (i64.const 0x1_0000_0001)))
    (i64.xor (local.get $x)
      (i64.xor (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))
               (i64.shl (i64.extend_i32_s (i32.wrap_i64 (local.get $x))) (i64.const 1)))))

  ;; a compare and a select
  (func $sel (export "sel") (param $a i64) (param $b i64) (result i64) (local $x i64) (local $w i32)
    (local.set $x (i64.add (local.get $a) (local.get $b)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (i64.add (local.get $x)
      (i64.extend_i32_u
        (select (local.get $w) (i32.const 7)
                (i32.lt_u (local.get $w) (i32.const 0x8000_0000))))))

  ;; a shift's amount and operand, a rotate
  (func $shifts (export "shifts") (param $a i64) (param $b i64) (result i64) (local $x i64) (local $y i64)
    (local.set $x (i64.add (local.get $a) (i64.const 0x5_0000_0000)))
    (local.set $y (i64.add (local.get $b) (i64.const 0x3_0000_0003)))
    (i64.add (i64.add (local.get $x) (local.get $y))
      (i64.extend_i32_u
        (i32.xor (i32.shl (i32.const 1) (i32.wrap_i64 (local.get $y)))
          (i32.xor (i32.shr_s (i32.wrap_i64 (local.get $x)) (i32.const 3))
                   (i32.rotl (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y))))))))

  ;; counts of bits a set high half would change
  (func $bits (export "bits") (param $a i64) (result i64) (local $x i64) (local $w i32)
    (local.set $x (i64.or (local.get $a) (i64.const 0x8000_0001_0000_0000)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (i64.add (local.get $x)
      (i64.extend_i32_u
        (i32.add (i32.clz (local.get $w))
          (i32.add (i32.mul (i32.ctz (local.get $w)) (i32.const 100))
                   (i32.mul (i32.popcnt (local.get $w)) (i32.const 10000)))))))

  ;; to floats, unsigned and signed, and the bits as an f32
  (func $conv (export "conv") (param $a i64) (result f64) (local $x i64) (local $w i32)
    (local.set $x (i64.add (local.get $a) (i64.const 0x1_0000_0000)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (f64.add (f64.convert_i64_s (local.get $x))
      (f64.add (f64.convert_i32_u (local.get $w))
        (f64.add (f64.convert_i32_s (local.get $w))
                 (f64.promote_f32 (f32.reinterpret_i32 (local.get $w)))))))

  ;; a global of 4 bytes
  (func $glob (export "glob") (param $a i64) (result i64) (local $x i64)
    (local.set $x (i64.sub (local.get $a) (i64.const 0x2_0000_0000)))
    (global.set $g (i32.wrap_i64 (local.get $x)))
    (i64.add (local.get $x) (i64.extend_i32_u (global.get $g))))

  ;; an argument, read for all 64 bits: the wrap is made
  (func $arg (export "arg") (param $a i64) (param $f i32) (result i64) (local $x i64)
    (local.set $x (i64.or (local.get $a) (i64.const 0xff00_0000_0000_0000)))
    (i64.add (local.get $x)
      (call_indirect (type $u) (i32.wrap_i64 (local.get $x)) (local.get $f))))

  ;; the i64 across a call, the wrap read after it
  (func $across (export "across") (param $a i64) (param $f i32) (result i64) (local $x i64) (local $w i32) (local $c i64)
    (local.set $x (i64.mul (local.get $a) (i64.const 0x1_0000_0003)))
    (local.set $w (i32.wrap_i64 (local.get $x)))
    (local.set $c (call_indirect (type $u) (i32.const 5) (local.get $f)))
    (i64.add (i64.add (local.get $x) (local.get $c))
      (i64.extend_i32_u (i32.add (local.get $w) (i32.const 9)))))

  ;; a branch's own operand, made early: the branch reads the wrap, which
  ;; is no alias, so the i64 needs no register up to it
  (func $branch (export "branch") (param $a i64) (param $b i64) (result i64) (local $w i32) (local $s i64)
    (local.set $w (i32.wrap_i64 (i64.add (local.get $a) (local.get $b))))
    (local.set $s (i64.mul (local.get $a) (local.get $b)))
    (i64.store (i32.const 0) (i64.add (local.get $s) (i64.const 1)))
    (i64.store (i32.const 8) (i64.add (local.get $s) (i64.const 2)))
    (i64.store (i32.const 16) (i64.mul (local.get $s) (local.get $a)))
    (i64.store (i32.const 24) (i64.xor (local.get $s) (local.get $b)))
    (block $out
      (br_if $out (local.get $w))
      (return (i64.add (i64.load (i32.const 16)) (i64.load (i32.const 24)))))
    (i64.add (i64.load (i32.const 0)) (i64.load (i32.const 8))))

  ;; a block's result, an edge's argument, divided after: made (a store
  ;; keeps the branch from becoming a select of the two)
  (func $edge (export "edge") (param $a i64) (param $p i32) (result i64) (local $x i64)
    (local.set $x (i64.add (local.get $a) (i64.const 0x4_0000_0000)))
    (i64.add (local.get $x)
      (i64.extend_i32_u
        (i32.div_u
          (block $j (result i32)
            (br_if $j (i32.wrap_i64 (local.get $x)) (local.get $p))
            (drop)
            (i64.store (i32.const 32) (local.get $x))
            (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 4))))
          (i32.const 3)))))

  ;; an indirect call's index, the function value's bits from 16 up, as Go
  ;; makes it: the wrap is the i64's register, x read after the call
  (func $icall (export "icall") (param $x i64) (result i64)
    (i64.add (local.get $x)
      (call_indirect (type $u) (i32.const 5)
        (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 16))))))
)
