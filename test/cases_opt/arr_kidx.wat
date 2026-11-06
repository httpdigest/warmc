;; An array access at a constant index takes the element's offset -- the
;; header and the elements before it -- as its load's or store's immediate
;; (isel_core.inc arr_imm_index, isel.c imm_ok_a64, isel_ops.inc's array
;; get and set), and a bounds check compares the length with the index,
;; cmp len, #k and a b.ls to the trap, where the index made in a register
;; and an add of it took two more: luawasm's reads of its globals' table.
;;
;;   known    an immutable global's array of 4 i64s, element 2: [x, #0x18]
;;   checked  an array of n i64s, element 3: the length n compared with 3
;;   loaded   a mutable global's array, which a call may replace with one of
;;            k: its length read and compared with 3
;;   far      5000 i64s, element 4500 (proven in bounds): 8 + 36000 bytes,
;;            past a scaled immediate's reach, in a register
;;   bytes    5000 i8s, element 4000 (4008 fits) and 4090 (4098 does not)
;;   bigk     an index of 4096 with a bounds check: past cmp's 12 bits
;;   sizes    an i16, an i32, an f64 and a v128 element, set and read again
;;   lenarg   an array's length read, then its element 3: the length the
;;            check's operand, compared with 3
;;   nested   an element of an array that is an element of another, at
;;            constant indices both: the barrier's slot the inner array's
;;
;; (A b.ls more in checked and loaded: the stack's check.)  Answers worked
;; out by hand.
;;
;; shape: known ldr\s+x\d+, \[x\d+, #0x18\]=1
;; shape: known uxtw=0
;; shape: checked cmp\s+w\d+, #0x3=1
;; shape: checked b\.ls=2
;; shape: checked uxtw=0
;; shape: loaded ldr\s+w\d+, \[x\d+, #0x4\]=1
;; shape: loaded b\.ls=2
;; shape: far uxtw=2
;; shape: far mov\s+w\d+, #0x1194=1
;; shape: lenarg cmp\s+w\d+, #0x3=1
;; shape: lenarg ldr\s+w\d+, \[x\d+, #0x4\]=1
;; shape: bytes uxtw=2
;; shape: bigk uxtw=1
;; shape: sizes uxtw=0
;; shape: nested uxtw=0
;;
;; run: known 1 => i32:31
;; run: checked 4 => i32:5
;; run: checked 9 => i32:5
;; run: checked 3 => trap: out of bounds array access
;; run: checked 0 => trap: out of bounds array access
;; run: loaded 0 => i32:40
;; run: loaded 5 => i32:0
;; run: loaded 3 => trap: out of bounds array access
;; run: far 7 => i32:14
;; run: bytes 3 => i32:7
;; run: bigk 4097 => i32:33
;; run: bigk 4096 => trap: out of bounds array access
;; run: sizes 2 => i32:2046
;; run: nested 0 => i32:7
;; run: lenarg 5 => i32:507
;; run: lenarg 3 => trap: out of bounds array access
(module
  (type $i64s (array (mut i64)))
  (type $i8s (array (mut i8)))
  (type $i16s (array (mut i16)))
  (type $i32s (array (mut i32)))
  (type $f64s (array (mut f64)))
  (type $vs (array (mut v128)))
  (type $s (struct (field i32)))
  (type $refs (array (mut (ref null $s))))
  (type $outer (array (mut (ref null $refs))))
  (global $g (ref $i64s) (array.new_fixed $i64s 4 (i64.const 10) (i64.const 20) (i64.const 30) (i64.const 40)))
  (global $mg (mut (ref $i64s)) (array.new_fixed $i64s 4 (i64.const 10) (i64.const 20) (i64.const 30) (i64.const 40)))
  (global $go (ref $outer)
    (array.new_fixed $outer 1
      (array.new_fixed $refs 2 (ref.null $s) (struct.new $s (i32.const 7)))))
  (func $known (export "known") (param $n i32) (result i32)
    (i32.add (i32.wrap_i64 (array.get $i64s (global.get $g) (i32.const 2))) (local.get $n)))
  (func $checked (export "checked") (param $n i32) (result i32)
    (i32.wrap_i64 (array.get $i64s (array.new $i64s (i64.const 5) (local.get $n)) (i32.const 3))))
  (func $loaded (export "loaded") (param $k i32) (result i32)
    (if (local.get $k) (then (global.set $mg (array.new_default $i64s (local.get $k)))))
    (i32.wrap_i64 (array.get $i64s (global.get $mg) (i32.const 3))))
  (func $far (export "far") (param $n i32) (result i32)
    (local $a (ref $i64s))
    (local.set $a (array.new_default $i64s (i32.const 5000)))
    (array.set $i64s (local.get $a) (i32.const 4500) (i64.extend_i32_u (local.get $n)))
    (i32.mul (i32.wrap_i64 (array.get $i64s (local.get $a) (i32.const 4500))) (i32.const 2)))
  (func $bytes (export "bytes") (param $n i32) (result i32)
    (local $a (ref $i8s))
    (local.set $a (array.new_default $i8s (i32.const 5000)))
    (array.set $i8s (local.get $a) (i32.const 4000) (local.get $n))
    (array.set $i8s (local.get $a) (i32.const 4090) (i32.add (local.get $n) (i32.const 1)))
    (i32.add (array.get_u $i8s (local.get $a) (i32.const 4000))
             (array.get_u $i8s (local.get $a) (i32.const 4090))))
  (func $bigk (export "bigk") (param $n i32) (result i32)
    (local $a (ref $i8s))
    (local.set $a (array.new $i8s (i32.const 33) (local.get $n)))
    (array.get_u $i8s (local.get $a) (i32.const 4096)))
  (func $sizes (export "sizes") (param $n i32) (result i32)
    (local $h (ref $i16s)) (local $w (ref $i32s)) (local $d (ref $f64s)) (local $v (ref $vs))
    (local.set $h (array.new_default $i16s (i32.const 4)))
    (local.set $w (array.new_default $i32s (i32.const 4)))
    (local.set $d (array.new_default $f64s (i32.const 4)))
    (local.set $v (array.new_default $vs (i32.const 4)))
    (array.set $i16s (local.get $h) (i32.const 3) (i32.mul (local.get $n) (i32.const 1000)))
    (array.set $i32s (local.get $w) (i32.const 2) (i32.add (local.get $n) (i32.const 40)))
    (array.set $f64s (local.get $d) (i32.const 1) (f64.convert_i32_u (local.get $n)))
    (array.set $vs (local.get $v) (i32.const 2) (i32x4.splat (local.get $n)))
    (i32.add
      (i32.add (array.get_u $i16s (local.get $h) (i32.const 3))
               (array.get $i32s (local.get $w) (i32.const 2)))
      (i32.add (i32.trunc_f64_u (array.get $f64s (local.get $d) (i32.const 1)))
               (i32x4.extract_lane 3 (array.get $vs (local.get $v) (i32.const 2))))))
  (func $lenarg (export "lenarg") (param $n i32) (result i32)
    (local $a (ref $i64s)) (local $l i32)
    (local.set $a (array.new $i64s (i64.const 7) (local.get $n)))
    (local.set $l (array.len (local.get $a)))
    (i32.add (i32.mul (local.get $l) (i32.const 100))
             (i32.wrap_i64 (array.get $i64s (local.get $a) (i32.const 3)))))
  (func $nested (export "nested") (param $n i32) (result i32)
    (i32.add
      (struct.get $s 0 (array.get $refs (array.get $outer (global.get $go) (i32.const 0)) (i32.const 1)))
      (local.get $n)))
)
