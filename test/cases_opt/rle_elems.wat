;; An array's element read again (opt.c pass_gc_rle): the read of a[i]
;; after a read or a write of a[i] -- the same array, the same index -- with
;; no write between of an element of that kind to any array, no array.copy
;; or fill, and no call, is the value read or written.  The kind is the
;; element's size and whether it is a reference: two arrays of one kind may
;; be one array, two indices one index.
;;
;;   twice     a[i] + a[i]: one read
;;   setget    a[i] = v, then a[i] + 1: the value stored, no read
;;   alias     a[i] read, b[i] written, a[i] read again; b is a where k is
;;             not 0, so the second read is made, and gives v there
;;   otherkind a[i] read, an i64 array's element written and read back, a[i]
;;             read again: the i64 store ends no i32 element, and its own
;;             element is the value stored -- two reads gone
;;   sameidx   a[i] read, a[j] written, a[i] read again: j may be i
;;   setset    a[i] = v, a[j] = w, then a[i]: w where j is i
;;   refs      a reference array's element read twice, an i64 array's element
;;             written between (the same size, no reference): one read
;;   refkill   the same with a reference written to it between at j
;;   filled    a[i] read, the array filled, a[i] read again
;;   copied    the same with an array.copy over it
;;   called    the same with a call that adds 1 to a[i]
;;   looped    a[1] read ahead of a loop that reads it every other time
;;             round and writes only an i64 array: the loop's read is the
;;             one ahead
;;   loopkill  the same, the loop writing a[j]: read each time round
;;   loopfill  the same, the loop filling a with n: read each time round
;;   fresh     a[i] read, a new array made of two values (its first stores
;;             end nothing: no array read before is the new one), a[i] read
;;             again and the new array's second element: no read of either
;;   packed    an i8 element written 0x1ff and read back unsigned and
;;             signed: 255 and -1, neither the value stored nor each other
;;   packed2   the same read unsigned twice: one read
;;   diamond   a[i] read, an i64 store on either arm of an if, a[i] read
;;             again where the arms meet: one read
;;   diakill   the same, one arm writing a[0]: read again
;;   fieldkeep a struct's field read, an element of the field's size written,
;;             the field read again: an element's write ends no field's read
;;   coldway   a[i] read, a call that writes it on a way a branch hint calls
;;             rare, a[i] read where the ways meet, of the array read
;;             before: read again there (a field's read would be made on
;;             the rare way and merged; an element's is not)
;;
;; The arrays: a = 10, 20, 30, 40; b = 1, 2, 3, 4; the references null,
;; i31 7, a struct, i31 9.  Answers worked out by hand.
;;
;; stats -O2: gc_elems_reused=11 gc_loads_reused=1
;;
;; shape: twice uxtw #2=1
;; shape: setget uxtw #2=1
;; shape: alias uxtw #2=3
;; shape: sameidx uxtw #2=3
;;
;; run: twice 1 => i32:40
;; run: twice 3 => i32:80
;; run: setget 2 41 => i32:42
;; run: alias 0 1 7 => i32:20020
;; run: alias 1 1 7 => i32:20007
;; run: otherkind 2 5 => i32:65
;; run: sameidx 1 2 9 => i32:20020
;; run: sameidx 1 1 9 => i32:20009
;; run: setset 1 1 5 6 => i32:6
;; run: setset 1 2 5 6 => i32:5
;; run: refs 0 => i32:1
;; run: refs 1 => i32:2
;; run: refs 2 => i32:0
;; run: refkill 0 0 => i32:10
;; run: refkill 0 1 => i32:11
;; run: filled 1 3 => i32:20003
;; run: copied 1 => i32:20002
;; run: called 1 => i32:20021
;; run: looped 3 => i32:40
;; run: looped 6 => i32:80
;; run: loopkill 3 1 => i32:100
;; run: loopkill 3 0 => i32:80
;; run: loopfill 2 => i32:42
;; run: loopfill 3 => i32:46
;; run: fresh 1 5 => i32:46
;; run: packed 1 511 => i32:-745
;; run: packed2 1 511 => i32:510
;; run: diamond 2 0 => i32:60
;; run: diamond 2 1 => i32:60
;; run: diakill 0 1 => i32:10005
;; run: diakill 0 0 => i32:10010
;; run: setcell 9 => i32:9
;; run: fieldkeep 1 5 => i32:145
;; run: coldway 1 0 => i32:20020
;; run: coldway 1 1 => i32:20021
(module
  (type $ints (array (mut i32)))
  (type $longs (array (mut i64)))
  (type $refs (array (mut eqref)))
  (type $bytes (array (mut i8)))
  (type $box (struct (field i32)))
  (global $ga (mut (ref $ints))
    (array.new_fixed $ints 4 (i32.const 10) (i32.const 20) (i32.const 30) (i32.const 40)))
  (global $gb (mut (ref $ints))
    (array.new_fixed $ints 4 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)))
  (global $gl (mut (ref $longs)) (array.new_default $longs (i32.const 4)))
  (global $gr (mut (ref $refs))
    (array.new_fixed $refs 4 (ref.null eq) (ref.i31 (i32.const 7))
      (struct.new $box (i32.const 5)) (ref.i31 (i32.const 9))))
  (global $g8 (mut (ref $bytes)) (array.new_default $bytes (i32.const 4)))
  (type $cell (struct (field (mut i32))))
  (global $gc (mut (ref $cell)) (struct.new $cell (i32.const 70)))
  ;; adds 1 to a[i], recursively so that it is not inlined -- and more than
  ;; the few instructions that are, whatever the call's block: it notes i
  ;; and d in the i64 array too
  (func $bump (param $i i32) (param $d i32)
    (array.set $longs (global.get $gl) (i32.const 2) (i64.extend_i32_u (local.get $i)))
    (array.set $longs (global.get $gl) (i32.const 3) (i64.extend_i32_u (local.get $d)))
    (if (local.get $d)
      (then (call $bump (local.get $i) (i32.sub (local.get $d) (i32.const 1))))
      (else
        (array.set $ints (global.get $ga) (local.get $i)
          (i32.add (array.get $ints (global.get $ga) (local.get $i)) (i32.const 1))))))
  (func $twice (export "twice") (param $i i32) (result i32)
    (i32.add (array.get $ints (global.get $ga) (local.get $i))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $setget (export "setget") (param $i i32) (param $v i32) (result i32)
    (array.set $ints (global.get $ga) (local.get $i) (local.get $v))
    (i32.add (array.get $ints (global.get $ga) (local.get $i)) (i32.const 1)))
  (func $alias (export "alias") (param $k i32) (param $i i32) (param $v i32) (result i32)
    (local $x i32)
    (if (local.get $k) (then (global.set $gb (global.get $ga))))
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (array.set $ints (global.get $gb) (local.get $i) (local.get $v))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $otherkind (export "otherkind") (param $i i32) (param $v i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (array.set $longs (global.get $gl) (local.get $i) (i64.extend_i32_u (local.get $v)))
    (i32.add (i32.add (local.get $x) (array.get $ints (global.get $ga) (local.get $i)))
             (i32.wrap_i64 (array.get $longs (global.get $gl) (local.get $i)))))
  (func $sameidx (export "sameidx") (param $i i32) (param $j i32) (param $v i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (array.set $ints (global.get $ga) (local.get $j) (local.get $v))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $setset (export "setset") (param $i i32) (param $j i32) (param $v i32) (param $w i32)
    (result i32)
    (array.set $ints (global.get $ga) (local.get $i) (local.get $v))
    (array.set $ints (global.get $ga) (local.get $j) (local.get $w))
    (array.get $ints (global.get $ga) (local.get $i)))
  (func $refs (export "refs") (param $i i32) (result i32)
    (local $n i32)
    (local.set $n (ref.is_null (array.get $refs (global.get $gr) (local.get $i))))
    (array.set $longs (global.get $gl) (i32.const 0) (i64.const 1))
    (i32.add (local.get $n)
      (i32.mul (i32.const 2)
        (ref.test (ref i31) (array.get $refs (global.get $gr) (local.get $i))))))
  (func $refkill (export "refkill") (param $i i32) (param $j i32) (result i32)
    (local $n i32)
    (local.set $n (ref.is_null (array.get $refs (global.get $gr) (local.get $i))))
    (array.set $refs (global.get $gr) (local.get $j) (ref.i31 (i32.const 1)))
    (i32.add (i32.mul (local.get $n) (i32.const 10))
             (ref.is_null (array.get $refs (global.get $gr) (local.get $i)))))
  (func $filled (export "filled") (param $i i32) (param $v i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (array.fill $ints (global.get $ga) (i32.const 0) (local.get $v) (i32.const 4))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $copied (export "copied") (param $i i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (array.copy $ints $ints (global.get $ga) (i32.const 0) (global.get $gb) (i32.const 0)
      (i32.const 4))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $called (export "called") (param $i i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (call $bump (local.get $i) (i32.const 2))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $looped (export "looped") (param $n i32) (result i32)
    (local $x i32) (local $s i32) (local $c i32)
    (local.set $x (array.get $ints (global.get $ga) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $c) (local.get $n)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then
            (local.set $s (i32.add (local.get $s) (array.get $ints (global.get $ga) (i32.const 1))))))
        (array.set $longs (global.get $gl) (i32.and (local.get $c) (i32.const 3))
          (i64.extend_i32_u (local.get $s)))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $x)))
  (func $loopkill (export "loopkill") (param $n i32) (param $j i32) (result i32)
    (local $x i32) (local $s i32) (local $c i32)
    (local.set $x (array.get $ints (global.get $ga) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $c) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (array.get $ints (global.get $ga) (i32.const 1))))
        (array.set $ints (global.get $ga) (local.get $j) (local.get $s))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $x)))
  (func $loopfill (export "loopfill") (param $n i32) (result i32)
    (local $x i32) (local $s i32) (local $c i32)
    (local.set $x (array.get $ints (global.get $ga) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $c) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (array.get $ints (global.get $ga) (i32.const 1))))
        (array.fill $ints (global.get $ga) (i32.const 0) (local.get $n) (i32.const 4))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $x)))
  (func $fresh (export "fresh") (param $i i32) (param $v i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (global.set $gb
      (array.new_fixed $ints 2 (local.get $v) (i32.add (local.get $v) (i32.const 1))))
    (i32.add (i32.add (local.get $x) (array.get $ints (global.get $ga) (local.get $i)))
             (array.get $ints (global.get $gb) (i32.const 1))))
  (func $packed (export "packed") (param $i i32) (param $v i32) (result i32)
    (array.set $bytes (global.get $g8) (local.get $i) (local.get $v))
    (i32.add (array.get_u $bytes (global.get $g8) (local.get $i))
             (i32.mul (array.get_s $bytes (global.get $g8) (local.get $i)) (i32.const 1000))))
  (func $packed2 (export "packed2") (param $i i32) (param $v i32) (result i32)
    (array.set $bytes (global.get $g8) (local.get $i) (local.get $v))
    (i32.add (array.get_u $bytes (global.get $g8) (local.get $i))
             (array.get_u $bytes (global.get $g8) (local.get $i))))
  (func $diamond (export "diamond") (param $i i32) (param $c i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (if (local.get $c)
      (then (array.set $longs (global.get $gl) (i32.const 0) (i64.const 1)))
      (else (array.set $longs (global.get $gl) (i32.const 1) (i64.const 2))))
    (i32.add (local.get $x) (array.get $ints (global.get $ga) (local.get $i))))
  (func $diakill (export "diakill") (param $i i32) (param $c i32) (result i32)
    (local $x i32)
    (local.set $x (array.get $ints (global.get $ga) (local.get $i)))
    (if (local.get $c)
      (then (array.set $ints (global.get $ga) (i32.const 0) (i32.const 5)))
      (else (array.set $longs (global.get $gl) (i32.const 1) (i64.const 2))))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (global.get $ga) (local.get $i))))
  ;; (the field is written somewhere: its reads are no constant's)
  (func $setcell (export "setcell") (param $v i32) (result i32)
    (struct.set $cell 0 (global.get $gc) (local.get $v))
    (struct.get $cell 0 (global.get $gc)))
  (func $fieldkeep (export "fieldkeep") (param $i i32) (param $v i32) (result i32)
    (local $x i32) (local $c (ref null $cell))
    (local.set $c (global.get $gc))
    (local.set $x (struct.get $cell 0 (local.get $c)))
    (array.set $ints (global.get $ga) (local.get $i) (local.get $v))
    (i32.add (i32.add (local.get $x) (struct.get $cell 0 (local.get $c)))
             (array.get $ints (global.get $ga) (local.get $i))))
  (func $coldway (export "coldway") (param $i i32) (param $c i32) (result i32)
    (local $x i32) (local $a (ref null $ints))
    (local.set $a (global.get $ga))
    (local.set $x (array.get $ints (local.get $a) (local.get $i)))
    (@metadata.code.branch_hint "\00") (if (local.get $c)
      (then (call $bump (local.get $i) (i32.const 2))))
    (i32.add (i32.mul (local.get $x) (i32.const 1000))
             (array.get $ints (local.get $a) (local.get $i)))))
