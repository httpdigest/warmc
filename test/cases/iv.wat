(module
  ;; Advancing an address instead of recomputing it.
  ;;
  ;; `base + (i*n + k) * 8` inside the k loop is rewritten into a pointer that
  ;; the loop advances.  That rewrite is exact in Z/2^32 -- `(x+y)*c` is
  ;; `x*c + y*c` and `x << c` is `x * 2^c` whatever wraps -- so the shapes
  ;; below are here to hold the algebra to that claim rather than to a weaker
  ;; one.  Each is compared against wabt's interpreter, which rewrites nothing.

  (memory 1)
  ;; Distinct bytes, so an address that is off by one step reads a
  ;; different value rather than the same zero.
  (data (i32.const 0) "\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\a5\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0\c5\ea\0f\34\59\7e\a3\c8\ed\12\37\5c\81\a6\cb\f0\15\3a\5f\84\a9\ce\f3\18\3d\62\87\ac\d1\f6\1b\40\65\8a\af\d4\f9\1e\43\68\8d\b2\d7\fc\21\46\6b\90\b5\da\ff\24\49\6e\93\b8\dd\02\27\4c\71\96\bb\e0\05\2a\4f\74\99\be\e3\08\2d\52\77\9c\c1\e6\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\a5\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0\c5\ea\0f\34\59\7e\a3\c8\ed\12\37\5c\81\a6\cb\f0\15\3a\5f\84\a9\ce\f3\18\3d\62\87\ac\d1\f6\1b\40\65\8a\af\d4\f9\1e\43\68\8d\b2\d7\fc\21\46\6b\90\b5\da\ff\24\49\6e\93\b8\dd\02\27\4c\71\96\bb\e0\05\2a\4f\74\99\be\e3\08\2d\52\77\9c\c1\e6\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\a5\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0\c5\ea\0f\34\59\7e\a3\c8\ed\12\37\5c\81\a6\cb\f0\15\3a\5f\84\a9\ce\f3\18\3d\62\87\ac\d1\f6\1b\40\65\8a\af\d4\f9\1e\43\68\8d\b2\d7\fc\21\46\6b\90\b5\da\ff\24\49\6e\93\b8\dd\02\27\4c\71\96\bb\e0\05\2a\4f\74\99\be\e3\08\2d\52\77\9c\c1\e6\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\a5\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0\c5\ea\0f\34\59\7e\a3\c8\ed\12\37\5c\81\a6\cb\f0\15\3a\5f\84\a9\ce\f3\18\3d\62\87\ac\d1\f6\1b\40\65\8a\af\d4\f9\1e\43\68\8d\b2\d7\fc\21\46\6b\90\b5\da\ff\24\49\6e\93\b8\dd\02\27\4c\71\96\bb\e0\05\2a\4f\74\99\be\e3\08\2d\52\77\9c\c1\e6")
  (global $n (mut i32) (i32.const 5))

  ;; The ordinary case: two arrays walked at different strides, the second
  ;; scaled by a value that is invariant but not constant, which is the shape
  ;; the step cannot be found for symbolically.
  (func (export "iv_two_arrays") (result i32)
    (local $k i32) (local $s i32) (local $n i32) (local $t i32)
    (local.set $n (global.get $n))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 6)))
      (local.set $t (i32.shl (local.get $k) (i32.const 2)))
      (i32.store (i32.add (i32.const 200) (local.get $t))
                 (i32.add (local.get $k) (i32.const 100)))
      (local.set $t (i32.shl (i32.mul (local.get $k) (local.get $n))
                             (i32.const 2)))
      (i32.store (i32.add (i32.const 400) (local.get $t))
                 (i32.add (local.get $k) (i32.const 200)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (local.set $k (i32.const 0))
    (block $d2 (loop $l2
      (br_if $d2 (i32.ge_u (local.get $k) (i32.const 6)))
      (local.set $t (i32.shl (local.get $k) (i32.const 2)))
      (local.set $s (i32.add (local.get $s)
                             (i32.load (i32.add (i32.const 200)
                                                (local.get $t)))))
      (local.set $t (i32.shl (i32.mul (local.get $k) (local.get $n))
                             (i32.const 2)))
      (local.set $s (i32.add (local.get $s)
                             (i32.load (i32.add (i32.const 400)
                                                (local.get $t)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l2)))
    (local.get $s))

  ;; The address arithmetic really does wrap, every iteration.  The counter
  ;; starts just below 2^32/4 so `k*4` crosses zero; the closed form and the
  ;; advanced form have to agree on the wrapped value, not merely while
  ;; nothing overflows.
  (func (export "iv_wraps") (result i32)
    (local $k i32) (local $s i32)
    (local.set $k (i32.const 0x3ffffffe))
    (block $done (loop $again
      (br_if $done (i32.eq (local.get $k) (i32.const 0x40000002)))
      (local.set $s (i32.add (local.get $s)
        (i32.load8_u (i32.add (i32.const 600)
                              (i32.and (i32.shl (local.get $k) (i32.const 2))
                                       (i32.const 63))))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (local.get $s))

  ;; A shift by more than the width: wasm takes the amount modulo 32, so the
  ;; step is k*2^(35 mod 32), not k*2^35.
  (func (export "iv_shift_wide") (result i32)
    (local $k i32) (local $s i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 4)))
      (i32.store8 (i32.add (i32.const 700)
                           (i32.shl (local.get $k) (i32.const 35)))
                  (i32.add (local.get $k) (i32.const 1)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (local.set $k (i32.const 0))
    (block $d2 (loop $l2
      (br_if $d2 (i32.ge_u (local.get $k) (i32.const 4)))
      (local.set $s (i32.add (local.get $s)
        (i32.load8_u (i32.add (i32.const 700)
                              (i32.shl (local.get $k) (i32.const 35))))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l2)))
    (local.get $s))

  ;; A counter that runs down, so the step is negative.  The chain is three
  ;; deep on purpose: two is not enough for the rewrite to be worth doing, and
  ;; a shape the rewrite declines to touch tests nothing.
  (func (export "iv_down") (result i32)
    (local $k i32) (local $s i32) (local $t i32)
    (local.set $k (i32.const 8))
    (block $done (loop $again
      (br_if $done (i32.eqz (local.get $k)))
      (local.set $k (i32.sub (local.get $k) (i32.const 1)))
      (local.set $t (i32.add (i32.const 32)
                             (i32.shl (i32.add (local.get $k) (i32.const 1))
                                      (i32.const 1))))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $t))))
      (br $again)))
    (local.get $s))

  ;; The index is quadratic in the counter, so no single step advances it.
  ;; A rule that mistook `k*k` for affine would set a step from the first two
  ;; iterations and keep using it.
  (func (export "iv_quadratic") (result i32)
    (local $k i32) (local $s i32) (local $t i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 6)))
      (local.set $t (i32.add (i32.const 8)
                             (i32.shl (i32.mul (local.get $k) (local.get $k))
                                      (i32.const 1))))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $t))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (local.get $s))

  ;; The counter is a *shift amount*, so the index is 2^k, not k times
  ;; anything.  Affine in the exponent is not affine.
  (func (export "iv_shift_by_counter") (result i32)
    (local $k i32) (local $s i32) (local $t i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 5)))
      (local.set $t (i32.add (i32.const 8)
                             (i32.shl (i32.add (i32.const 1) (i32.const 0))
                                      (local.get $k))))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $t))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (local.get $s))

  ;; Zero trips: the pointer setup runs and the loop does not, so nothing the
  ;; setup computes may be observable.
  (func (export "iv_zero_trips") (result i32)
    (local $k i32) (local $s i32)
    (local.set $k (i32.const 9))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 4)))
      (local.set $s (i32.add (local.get $s)
        (i32.load8_u (i32.add (i32.const 800)
                              (i32.shl (local.get $k) (i32.const 2))))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (i32.add (local.get $s) (i32.const 7)))

  ;; A loop whose header is its own latch, so the increment has to go into the
  ;; header itself.
  (func (export "iv_self_loop") (result i32)
    (local $k i32) (local $s i32)
    (loop $again
      (local.set $s (i32.add (local.get $s)
        (i32.load8_u (i32.add (i32.const 16)
                              (i32.shl (local.get $k) (i32.const 1))))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $again (i32.lt_u (local.get $k) (i32.const 5))))
    (local.get $s))

  ;; The address is read after the loop as well as inside it.
  (func (export "iv_used_after") (result i32)
    (local $k i32) (local $p i32) (local $s i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $k) (i32.const 4)))
      (local.set $p (i32.add (i32.const 900)
                             (i32.shl (local.get $k) (i32.const 2))))
      (i32.store (local.get $p) (i32.add (local.get $k) (i32.const 30)))
      (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $again)))
    (i32.add (local.get $s) (local.get $p)))
)
