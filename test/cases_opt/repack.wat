;; An i64 put back together from its halves, x & 0xffffffff00000000 or'd
;; with the extended wrap of x, either way round, is x (opt.c repacked):
;; an inlined function of Go's returns the i64 a call of its gave it so,
;; once gosp.c packs the stack pointer in (go_resume.wat has that).  Not
;; when the low half is another value's, the mask leaves a bit out, the wrap
;; is extended with its sign, what is extended is no wrap (an add of one, an
;; eqz of x), or the high half is or'd or xor'd with the mask.
;;
;; shape: same orr=0
;; shape: same and=0
;; shape: swapped orr=0
;; shape: swapped and=0
;;
;; run: same 1311768467463790320 1147797409030816545 => i64:2459565876494606865
;; run: same 6442450944 7 => i64:6442450951
;; run: same -1 0 => i64:-1
;; run: swapped 1311768467463790320 1147797409030816545 => i64:163971058432973775
;; run: swapped 6442450944 7 => i64:6442450937
;; run: swapped -1 0 => i64:-1
;; run: other 1311768467463790320 1147797409030816545 => i64:1311768467139281697
;; run: other 6442450944 7 => i64:4294967303
;; run: other -1 0 => i64:-4294967296
;; run: mask 1311768467463790320 1147797409030816545 => i64:1311768467463790320
;; run: mask 6442450944 7 => i64:2147483648
;; run: mask -1 0 => i64:-4294967297
;; run: sext 1311768467463790320 1147797409030816545 => i64:-1698898192
;; run: sext 6442450944 7 => i64:-2147483648
;; run: sext -1 0 => i64:-1
;; run: notwrap 1311768467463790320 1147797409030816545 => i64:1311768467463790321
;; run: notwrap 6442450944 7 => i64:6442450945
;; run: notwrap -1 0 => i64:-4294967296
;; run: orhi 1311768467463790320 1147797409030816545 => i64:-1698898192
;; run: orhi 6442450944 7 => i64:-2147483648
;; run: orhi -1 0 => i64:-1
;; run: xorhi 1311768467463790320 1147797409030816545 => i64:-1311768466566619408
;; run: xorhi 6442450944 7 => i64:-6442450944
;; run: xorhi -1 0 => i64:4294967295
;; run: eqz 1311768467463790320 1147797409030816545 => i64:1311768464867721216
;; run: eqz 6442450944 7 => i64:4294967296
;; run: eqz -1 0 => i64:-4294967296
;; run: eqz 0 0 => i64:1
(module
  ;; the halves put back together, x, either way round; plus or minus y
  (func $same (export "same") (param $x i64) (param $y i64) (result i64)
    (i64.add (local.get $y)
      (i64.or (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000))
              (i64.extend_i32_u (i32.wrap_i64 (local.get $x))))))
  (func $swapped (export "swapped") (param $x i64) (param $y i64) (result i64)
    (i64.sub
      (i64.or (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))
              (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000)))
      (local.get $y)))
  ;; another value's low half
  (func $other (export "other") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_u (i32.wrap_i64 (local.get $y)))))
  ;; a mask that leaves bit 32 out
  (func $mask (export "mask") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.and (local.get $x) (i64.const 0xffff_fffe_0000_0000))
            (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))))
  ;; the low half extended with its sign
  (func $sext (export "sext") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_s (i32.wrap_i64 (local.get $x)))))
  ;; an extended i32 that is no wrap
  (func $notwrap (export "notwrap") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_u (i32.add (i32.wrap_i64 (local.get $x)) (i32.const 1)))))
  ;; the high half or'd or xor'd with the mask, not masked
  (func $orhi (export "orhi") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.or (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))))
  (func $xorhi (export "xorhi") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.xor (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))))
  ;; an i32 made from x that is no wrap of it
  (func $eqz (export "eqz") (param $x i64) (param $y i64) (result i64)
    (i64.or (i64.and (local.get $x) (i64.const 0xffff_ffff_0000_0000))
            (i64.extend_i32_u (i64.eqz (local.get $x)))))
)
