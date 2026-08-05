;; A block parameter that nothing reads.
;;
;; `(drop (if (result T) ...))` gives the join block a parameter written by
;; both edges and read by nobody.  Its live range is a single point, and the
;; allocator's interference test treats ranges as half-open -- so a
;; zero-length one conflicts with nothing at all, and it coalesces into the
;; web of the parameter beside it, which is the phi for the loop counter.  One
;; register, two parameters of the same block: the edge that writes the dead
;; one overwrites the counter, and the loop never ends.
;;
;; It needs the *else* arm to be the one taken -- the `then` arm here carries
;; the counter itself, so writing it changes nothing -- which is why the
;; condition is a byte of memory rather than a constant, and why byte zero of
;; the data below is zero.  Found by random module generation, as a compiled
;; module that did not answer.
;;
;; Every function is the same shape at a different width, because the rule
;; being pinned is about *positions*, not about types: two values defined at
;; the same point cannot be one register however short their ranges are.

(module
  (memory 1)
  (data (i32.const 0)
    "\00\02\03\04\05\06\07\08\f0\e1\d2\c3\b4\a5\96\87")

  (func (export "dead_join_i32") (result i32)
    (local $z i32) (local $v v128) (local $i i32)
    (block $b (loop $l
      (drop (if (result i32)
              (i32.load8_s (i32.and (i32.rem_u (local.get $i)
                                               (i32.or (local.get $z) (i32.const 1)))
                                    (i32.const 0x3f8)))
              (then (local.get $i))
              (else (i8x16.extract_lane_u 8 (local.get $v)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 5)))))
    (local.get $i))

  ;; A wide dead parameter, so the rule cannot be one about four-byte values,
  ;; and a second counter live across the same join.
  (func (export "dead_join_i64") (result i32)
    (local $z i32) (local $v v128) (local $i i32) (local $j i32)
    (block $b (loop $l
      (drop (if (result i64)
              (i32.load8_s (i32.and (i32.rem_u (local.get $i)
                                               (i32.or (local.get $z) (i32.const 1)))
                                    (i32.const 0x3f8)))
              (then (i64.extend_i32_u (local.get $i)))
              (else (i64x2.extract_lane 1 (local.get $v)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 3)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 6)))))
    (i32.add (local.get $i) (i32.mul (local.get $j) (i32.const 256))))

  ;; The dead parameter in the vector file, beside a live float.
  (func (export "dead_join_f64") (result i32)
    (local $z i32) (local $v v128) (local $i i32) (local $a f64)
    (local.set $a (f64.const 1.5))
    (block $b (loop $l
      (drop (if (result f64)
              (i32.load8_s (i32.and (i32.rem_u (local.get $i)
                                               (i32.or (local.get $z) (i32.const 1)))
                                    (i32.const 0x3f8)))
              (then (local.get $a))
              (else (f64x2.extract_lane 0 (local.get $v)))))
      (local.set $a (f64.add (local.get $a) (f64.const 2.0)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 4)))))
    (i32.add (local.get $i) (i32.trunc_f64_s (local.get $a))))

  ;; Two dead parameters in one body, so a fix that only looks at the first is
  ;; not enough.
  (func (export "dead_join_two") (result i32)
    (local $z i32) (local $v v128) (local $i i32) (local $j i32)
    (block $b (loop $l
      (drop (if (result i32)
              (i32.load8_s (i32.and (i32.rem_u (local.get $i)
                                               (i32.or (local.get $z) (i32.const 1)))
                                    (i32.const 0x3f8)))
              (then (local.get $i))
              (else (i8x16.extract_lane_u 8 (local.get $v)))))
      (drop (if (result i32)
              (i32.load8_s (i32.and (i32.rem_u (local.get $j)
                                               (i32.or (local.get $z) (i32.const 1)))
                                    (i32.const 0x3f8)))
              (then (local.get $j))
              (else (i8x16.extract_lane_u 4 (local.get $v)))))
      (local.set $j (i32.add (local.get $j) (i32.const 2)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 5)))))
    (i32.add (local.get $i) (i32.mul (local.get $j) (i32.const 256))))
)
