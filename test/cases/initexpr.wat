;; Extended constant expressions: an initialiser is a sequence now, not a
;; single instruction.  Every value below is produced by the runtime's
;; evaluator at instantiation and read back by a function, so a wrong answer
;; is a wrong constant pool, a wrong evaluation order, or arithmetic that
;; forgot which width it was in.
;;
;; What is *not* here is a `global.get` naming a global this module defines --
;; the other half of the 3.0 relaxation.  wabt 1.0.41 still enforces the wasm
;; 2.0 rule that an initialiser may only read an import, in its text parser and
;; its binary reader both, so it cannot be the reference for that shape; it is
;; hand-encoded in test/gen_binary_cases.py instead.
(module
  (memory 1)
  (table 8 funcref)
  (type $void_i32 (func (result i32)))

  (global $a i32 (i32.const 100))
  (global $b i64 (i64.const 1000))

  ;; Composed arithmetic, nested on both sides.
  (global $c i32 (i32.add (i32.const 100) (i32.const 23)))
  (global $d i32 (i32.mul (i32.sub (i32.const 123) (i32.const 3)) (i32.const 2)))
  (global $e i64 (i64.mul (i64.const 1000) (i64.const -3)))
  (global $f i64 (i64.sub (i64.add (i64.const -3000) (i64.const 7)) (i64.const 1000)))
  (global $deep i32 (i32.add (i32.add (i32.add (i32.const 1) (i32.const 2))
                                      (i32.add (i32.const 4) (i32.const 8)))
                             (i32.mul (i32.const 16) (i32.const 2))))

  ;; Modular, and in the width the operation names: an i32 multiply that keeps
  ;; the top half of a 64-bit register is the classic way to get this wrong.
  (global $wrap i32 (i32.mul (i32.const 0x40000000) (i32.const 4)))
  (global $neg  i32 (i32.sub (i32.const 0) (i32.const 1)))
  (global $near i32 (i32.mul (i32.const 0xffffffff) (i32.const 0xffffffff)))
  (global $big  i64 (i64.mul (i64.const 0x100000000) (i64.const 0x100000000)))
  (global $bneg i64 (i64.sub (i64.const 0) (i64.const 1)))

  ;; A funcref global, and a data segment whose offset is computed: 100 + 4.
  (global $fr funcref (ref.func $ret7))
  (data (offset (i32.add (i32.const 100) (i32.const 4))) "\aa\bb\cc\dd")

  ;; An element segment with a computed offset (100 - 98 = 2) and items that
  ;; are constant expressions rather than bare function indices.
  (elem (offset (i32.sub (i32.const 100) (i32.const 98))) funcref
        (ref.func $ret7) (ref.null func) (ref.func $ret9))

  ;; The same two offsets with `(offset ...)` left off.  A single *folded*
  ;; instruction may stand for the whole `(offset ...)` group, and folding says
  ;; nothing about how many instructions it unfolds to: warmc recorded the
  ;; tokens inside the parentheses, so `(i32.add (i32.const 8) (i32.const 8))`
  ;; arrived as the flat sequence `i32.add (i32.const 8) (i32.const 8)` and was
  ;; refused for having no operands.  Only a nested one shows it -- the offset
  ;; every other fixture writes is `(i32.const n)`, which is the same tokens
  ;; either way.  Both oracles accept these, so run_wat.py checks the bytes.
  (data (i32.add (i32.const 200) (i32.const 8)) "\11\22\33\44")
  (elem (i32.sub (i32.const 10) (i32.const 4)) funcref (ref.func $ret9))

  (func $ret7 (result i32) (i32.const 7))
  (func $ret9 (result i32) (i32.const 9))

  (func (export "a") (result i32) (global.get $a))
  (func (export "c") (result i32) (global.get $c))
  (func (export "d") (result i32) (global.get $d))
  (func (export "e") (result i64) (global.get $e))
  (func (export "f") (result i64) (global.get $f))
  (func (export "deep") (result i32) (global.get $deep))
  (func (export "wrap") (result i32) (global.get $wrap))
  (func (export "neg") (result i32) (global.get $neg))
  (func (export "near") (result i32) (global.get $near))
  (func (export "big") (result i64) (global.get $big))
  (func (export "bneg") (result i64) (global.get $bneg))
  (func (export "fr_null") (result i32) (ref.is_null (global.get $fr)))

  (func (export "data_at_104") (result i32) (i32.load (i32.const 104)))
  (func (export "data_at_100") (result i32) (i32.load (i32.const 100)))

  (func (export "elem2") (result i32)
    (call_indirect (type $void_i32) (i32.const 2)))
  (func (export "elem4") (result i32)
    (call_indirect (type $void_i32) (i32.const 4)))
  (func (export "elem3_null") (result i32)
    (ref.is_null (table.get (i32.const 3))))

  (func (export "data_at_208") (result i32) (i32.load (i32.const 208)))
  (func (export "elem6") (result i32)
    (call_indirect (type $void_i32) (i32.const 6)))
)
