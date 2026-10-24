;; A loop whose preheader ends in a call that can raise (gloop, opt.c
;; pass_global_loop): $callee, inlined inside the try_table, calls through
;; the table and then loops setting a global.  Promoting the global reads it
;; at the preheader's end, which a raising call ends instead, and a
;; promotion given up left that read behind: the IR failed to verify.
;; run: f 5 => i32:5
(module
  (type $u (func (param i32) (result i32)))
  (table 2 funcref)
  (elem (i32.const 0) $other $other)
  (global $g (mut i32) (i32.const 0))
  (func $other (param i32) (result i32) (i32.mul (local.get 0) (i32.const 3)))
  (func $callee (param $n i32)
    (local $i i32)
    (drop (call_indirect (type $u) (local.get $n) (i32.and (local.get $n) (i32.const 1))))
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
  (func (export "f") (param $n i32) (result i32)
    (block $out
      (try_table (catch_all $out)
        (call $callee (local.get $n))))
    (global.get $g))
)
