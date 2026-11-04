;; What jump threading must not take for a fact (opt.c kt_way, kt_ev,
;; thread_path): a value a loop makes again each time round, tested on one
;; way through a merge and again past it (loopfact, samelp: the tests of the
;; time round before are of another value); a flag the loop flips, whose
;; branch at the end must not decide the test at the next time's top
;; (stale); a select of a reference and a null, which may be null though its
;; first operand cannot (selnull, seli31 -- the select typed by its first
;; operand alone was taken to have no null); a path whose two blocks make
;; values read past them both (pathmerge); and a test of a value loaded in
;; the block before, which no way decides (pathload); and a merge's tests
;; of i31 constants, the merge reached through a block that only hands it
;; its value, its constants not to be put in that block, which the threaded
;; way bypasses (fwdconst: mandelbrot_float's f1).  Answers by wasmtime.
;;
;; run: loopfact 0 => i32:101
;; run: loopfact 1 => i32:101
;; run: loopfact 7 => i32:212
;; run: loopfact 64 => i32:1712
;; run: samelp 0 => i32:101
;; run: samelp 1 => i32:101
;; run: samelp 8 => i32:313
;; run: samelp 100 => i32:4061
;; run: stale 0 => i32:1
;; run: stale 1 => i32:1
;; run: stale 2 => i32:11
;; run: stale 9 => i32:45
;; run: selnull 0 => i32:-1
;; run: selnull 1 => i32:4
;; run: selnull 9 => i32:4
;; run: seli31 0 => i32:-1
;; run: seli31 1 => i32:3
;; run: seli31 -3 => i32:3
;; run: pathmerge 1 5 => i32:1015
;; run: pathmerge 1 500 => i32:13
;; run: pathmerge 0 1 => i32:1015
;; run: pathmerge 0 7 => i32:99
;; run: pathmerge 0 300 => i32:4199
;; run: pathmerge 0 -2 => i32:-27
;; run: fwdconst 1 0 => i32:100
;; run: fwdconst 0 3 => i32:100
;; run: fwdconst 0 4 => i32:4
;; run: fwdconst 0 -7 => i32:-7
;; run: fwdconst 5 9 => i32:100
;; run: pathload 0 0 => i32:20
;; run: pathload 1 0 => i32:10
;; run: pathload 0 1 => i32:10
;; run: pathload 1 1 => i32:20
(module
  (type $s (struct (field (mut i32))))
  (global $sink (mut i32) (i32.const 0))
  (memory 1)

  (func $loopfact (export "loopfact") (param $n i32) (result i32)
    (local $i i32) (local $x i32) (local $r i32)
    (loop $top
      (local.set $x (i32.and (local.get $i) (i32.const 3)))
      (if (i32.eqz (local.get $x))
        (then (local.set $r (i32.add (local.get $r) (i32.const 1))))
        (else (local.set $r (i32.add (local.get $r) (i32.const 2)))))
      (global.set $sink (local.get $r))
      (if (i32.eqz (local.get $x))
        (then (local.set $r (i32.add (local.get $r) (i32.const 100)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $top (i32.lt_s (local.get $i) (local.get $n))))
    (local.get $r))

  (func $samelp (export "samelp") (param $n i32) (result i32)
    (local $i i32) (local $r i32)
    (loop $top
      (if (i32.lt_u (i32.and (local.get $i) (i32.const 7)) (i32.const 3))
        (then (local.set $r (i32.add (local.get $r) (i32.const 1))))
        (else (local.set $r (i32.add (local.get $r) (i32.const 2)))))
      (global.set $sink (local.get $r))
      (if (i32.lt_u (i32.and (local.get $i) (i32.const 7)) (i32.const 3))
        (then (local.set $r (i32.add (local.get $r) (i32.const 100)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $top (i32.lt_s (local.get $i) (local.get $n))))
    (local.get $r))

  (func $stale (export "stale") (param $n i32) (result i32)
    (local $i i32) (local $f i32) (local $r i32)
    (loop $top
      (if (local.get $f)
        (then (local.set $r (i32.add (local.get $r) (i32.const 10)))))
      (global.set $sink (local.get $r))
      (local.set $f (i32.eqz (local.get $f)))
      (if (local.get $f)
        (then (local.set $r (i32.add (local.get $r) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $top (i32.lt_s (local.get $i) (local.get $n))))
    (local.get $r))

  (func $selnull (export "selnull") (param $k i32) (result i32)
    (local $v (ref null $s))
    (local.set $v (select (result (ref null $s))
                    (struct.new $s (i32.const 4)) (ref.null $s) (local.get $k)))
    (global.set $sink (local.get $k))
    (if (result i32) (ref.is_null (local.get $v))
      (then (i32.const -1))
      (else (struct.get $s 0 (local.get $v)))))

  (func $seli31 (export "seli31") (param $k i32) (result i32)
    (local $v eqref)
    (local.set $v (select (result eqref)
                    (ref.i31 (i32.const 3)) (ref.null eq) (local.get $k)))
    (global.set $sink (local.get $k))
    (if (result i32) (ref.is_null (local.get $v))
      (then (i32.const -1))
      (else (i31.get_s (ref.cast (ref i31) (local.get $v))))))

  (func $pathmerge (export "pathmerge") (param $k i32) (param $x i32) (result i32)
    (local $kind i32) (local $r i32) (local $t i32)
    (if (local.get $k)
      (then (local.set $kind (i32.const 1)))
      (else (local.set $kind (local.get $x))))
    (local.set $t (i32.mul (local.get $kind) (i32.const 7)))
    (if (i32.lt_s (local.get $x) (i32.const 100))
      (then
        (local.set $r (i32.add (local.get $t) (i32.const 1)))
        (if (i32.eq (local.get $kind) (i32.const 1))
          (then (local.set $r (i32.add (local.get $r) (i32.const 1000))))))
      (else (local.set $r (i32.sub (local.get $t) (i32.const 1)))))
    (i32.add (local.get $r) (local.get $t)))

  (func $fwdconst (export "fwdconst") (param $k i32) (param $x i32) (result i32)
    (local $t eqref)
    (if (local.get $k)
      (then (local.set $t (ref.i31 (i32.const 3))))
      (else (local.set $t (ref.i31 (local.get $x)))))
    (block $b
      (if (i32.and (ref.eq (local.get $t) (ref.i31 (i32.const 3)))
                   (ref.eq (local.get $t) (ref.i31 (i32.const 3))))
        (then (return (i32.const 100)))))
    (global.set $sink (i32.const 1))
    (i31.get_s (ref.cast (ref i31) (local.get $t))))

  (func $pathload (export "pathload") (param $k i32) (param $x i32) (result i32)
    (local $v i32)
    (i32.store (i32.const 16) (local.get $x))
    (if (local.get $k)
      (then (local.set $v (i32.const 1)))
      (else (local.set $v (i32.const 0))))
    (if (local.get $k) (then (i32.store (i32.const 16) (i32.eqz (local.get $x)))))
    (global.set $sink (local.get $v))
    (if (result i32) (i32.eq (i32.load (i32.const 16)) (i32.const 1))
      (then (i32.const 10))
      (else (i32.const 20)))))
