;; A counter run down as `while (n--)`: the loop tests the old count and
;; carries the decremented one.  pass_postdec_test tests the new count against
;; -1 instead, so the old one dies at the subtraction.  Each export runs such
;; loops from counts 0, 1, 2, 7 and 300, in the shapes that must give the same
;; answers:
;;
;;   * the test as an if around the body (AssemblyScript's String.__eq) and as
;;     a br_if of eqz out of the loop;
;;   * the decrement as a sub of 1 and as an add of -1;
;;   * i32 and i64 counters, tested by ne and by eqz;
;;   * the count read after the loop, where it is -1;
;;   * the old count also read in the body, which is left alone;
;;   * a body that leaves early, so the count stops part way.

(module
  (memory 1)
  (data (i32.const 0) "\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66")

  ;; if (n--) { ... } as a loop, the count read after it.
  (func $if_form (param $n i32) (result i32)
    (local $t i32) (local $p i32) (local $s i32)
    (loop $l
      (local.set $t (local.get $n))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (if (local.get $t)
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
                                 (i32.load16_u (i32.and (local.get $p) (i32.const 127)))))
          (local.set $p (i32.add (local.get $p) (i32.const 2)))
          (br $l))))
    (i32.add (local.get $s) (i32.mul (local.get $n) (i32.const 1000))))

  ;; br_if out on eqz of the old count, decremented by adding -1.
  (func $eqz_form (param $n i32) (result i32)
    (local $t i32) (local $p i32) (local $s i32)
    (block $out
      (loop $l
        (local.set $t (local.get $n))
        (local.set $n (i32.add (local.get $n) (i32.const -1)))
        (br_if $out (i32.eqz (local.get $t)))
        (local.set $s (i32.xor (i32.rotl (local.get $s) (i32.const 5))
                               (i32.load8_u (i32.and (local.get $p) (i32.const 127)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $n)))

  (func $i64_form (param $n i64) (result i64)
    (local $t i64) (local $p i32) (local $s i64)
    (loop $l
      (local.set $t (local.get $n))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (if (i64.ne (local.get $t) (i64.const 0))
        (then
          (local.set $s (i64.add (i64.mul (local.get $s) (i64.const 0x100000001b3))
                                 (i64.load8_u (i32.and (local.get $p) (i32.const 127)))))
          (local.set $p (i32.add (local.get $p) (i32.const 3)))
          (br $l))))
    (i64.xor (local.get $s) (local.get $n)))

  ;; The old count is read in the body as well.
  (func $old_read (param $n i32) (result i32)
    (local $t i32) (local $s i32)
    (loop $l
      (local.set $t (local.get $n))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (if (local.get $t)
        (then
          (local.set $s (i32.add (local.get $s)
                                 (i32.mul (local.get $t)
                                          (i32.load8_u (i32.and (local.get $t) (i32.const 127))))))
          (br $l))))
    (i32.add (local.get $s) (local.get $n)))

  ;; Stops at the first byte above 0xf0, with the count left where it was.
  (func $early (param $n i32) (result i32)
    (local $t i32) (local $p i32) (local $s i32)
    (block $found
      (loop $l
        (local.set $t (local.get $n))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (if (local.get $t)
          (then
            (local.set $s (i32.load8_u (i32.and (local.get $p) (i32.const 127))))
            (br_if $found (i32.gt_u (local.get $s) (i32.const 0xf0)))
            (local.set $p (i32.add (local.get $p) (i32.const 1)))
            (br $l)))))
    (i32.add (i32.mul (local.get $p) (i32.const 65536)) (local.get $n)))

  (func (export "if_form") (result i32)
    (i32.xor (i32.xor (call $if_form (i32.const 0)) (i32.rotl (call $if_form (i32.const 1)) (i32.const 7)))
             (i32.xor (i32.rotl (call $if_form (i32.const 2)) (i32.const 13))
                      (i32.xor (i32.rotl (call $if_form (i32.const 7)) (i32.const 19))
                               (i32.rotl (call $if_form (i32.const 300)) (i32.const 25))))))

  (func (export "eqz_form") (result i32)
    (i32.xor (i32.xor (call $eqz_form (i32.const 0)) (i32.rotl (call $eqz_form (i32.const 1)) (i32.const 7)))
             (i32.xor (i32.rotl (call $eqz_form (i32.const 2)) (i32.const 13))
                      (i32.xor (i32.rotl (call $eqz_form (i32.const 7)) (i32.const 19))
                               (i32.rotl (call $eqz_form (i32.const 300)) (i32.const 25))))))

  (func (export "i64_form") (result i64)
    (i64.xor (i64.xor (call $i64_form (i64.const 0)) (i64.rotl (call $i64_form (i64.const 1)) (i64.const 7)))
             (i64.xor (i64.rotl (call $i64_form (i64.const 2)) (i64.const 13))
                      (i64.xor (i64.rotl (call $i64_form (i64.const 7)) (i64.const 19))
                               (i64.rotl (call $i64_form (i64.const 300)) (i64.const 25))))))

  (func $i64_eqz (param $n i64) (result i64)
    (local $t i64) (local $p i32) (local $s i64)
    (block $out
      (loop $l
        (local.set $t (local.get $n))
        (local.set $n (i64.add (local.get $n) (i64.const -1)))
        (br_if $out (i64.eqz (local.get $t)))
        (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 9))
                               (i64.load16_s (i32.and (local.get $p) (i32.const 126)))))
        (local.set $p (i32.add (local.get $p) (i32.const 6)))
        (br $l)))
    (i64.add (local.get $s) (i64.shl (local.get $n) (i64.const 40))))

  (func (export "i64_eqz") (result i64)
    (i64.xor (i64.xor (call $i64_eqz (i64.const 0)) (i64.rotl (call $i64_eqz (i64.const 1)) (i64.const 7)))
             (i64.xor (i64.rotl (call $i64_eqz (i64.const 2)) (i64.const 13))
                      (i64.xor (i64.rotl (call $i64_eqz (i64.const 7)) (i64.const 19))
                               (i64.rotl (call $i64_eqz (i64.const 300)) (i64.const 25))))))

  (func (export "old_read") (result i32)
    (i32.xor (i32.xor (call $old_read (i32.const 0)) (i32.rotl (call $old_read (i32.const 1)) (i32.const 7)))
             (i32.xor (i32.rotl (call $old_read (i32.const 2)) (i32.const 13))
                      (i32.xor (i32.rotl (call $old_read (i32.const 7)) (i32.const 19))
                               (i32.rotl (call $old_read (i32.const 300)) (i32.const 25))))))

  (func (export "early") (result i32)
    (i32.xor (i32.xor (call $early (i32.const 0)) (i32.rotl (call $early (i32.const 1)) (i32.const 7)))
             (i32.xor (i32.rotl (call $early (i32.const 2)) (i32.const 13))
                      (i32.xor (i32.rotl (call $early (i32.const 7)) (i32.const 19))
                               (i32.rotl (call $early (i32.const 300)) (i32.const 25)))))))
