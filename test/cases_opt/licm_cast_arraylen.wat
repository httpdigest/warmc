;; A GC read out of a loop (LICM, opt.c gc_checks_hold_at): array.len of a
;; ref.cast's result, in an arm the loop takes only on one trip.  The cast's
;; conversion is pure and moves on its own; its test does not, so the read
;; may not move above it -- the object is an i31 or a struct, never the
;; array it would be read as, unless the test passed first.
;; run: f 0 0 10 => i32:45
;; run: f 1 0 10 => i32:45
;; run: f 0 3 10 => trap: cast failure
(module
  (type $arr (array (mut i32)))
  (type $st (struct (field i32) (field i32)))
  ;; x is an i31 or a struct, never an array when $k == 0; the cast in the
  ;; loop only runs when i == k - 1 (never for k == 0)
  (func (export "f") (param $p i32) (param $k i32) (param $n i32) (result i32)
    (local $x (ref any)) (local $i i32) (local $s i32)
    (local.set $x (if (result (ref any)) (local.get $p)
                    (then (struct.new $st (i32.const 7) (i32.const 8)))
                    (else (ref.i31 (i32.const 5)))))
    (loop $l
      (if (i32.eq (local.get $i) (i32.sub (local.get $k) (i32.const 1)))
        (then (local.set $s (i32.add (local.get $s)
                (array.len (ref.cast (ref $arr) (local.get $x)))))))
      (local.set $s (i32.add (local.get $s) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
