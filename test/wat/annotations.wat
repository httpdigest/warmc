;; Annotations, and the lexical corners that only text reaches.
;;
;; This exists because the round-trip half of test/run_wat.py cannot reach any
;; of it.  That half prints a *binary* module back to text, so it only ever
;; produces what a printer emits -- and a printer emits no annotations, no
;; nested block comments and no `\u{...}` escape.  Those rules would then be
;; carried entirely by code with no test, which is the shape that lets a rule
;; be wrong for as long as nobody writes one by hand.
;;
;; Every annotation here is one *both* assemblers ignore.  `(@custom ...)` is
;; deliberately not among them: wabt with --enable-annotations turns that one
;; into a custom section and warmc drops it, so including it would make this
;; file differ for a reason that is not a bug.

(module (@module-note "ignored")
  (; A block comment holding a ;; line comment marker, and
       (; a nested one, whose close must not end the outer ;)
     still inside the outer comment. ;)

  (type $unary (@type-note) (func (param i32) (result i32)))

  (memory 1)
  ;; \u{...} produces UTF-8 bytes; the three lengths plus a bare \hh byte.
  (data (i32.const 0) "\u{24}\u{a2}\u{20ac}\u{10348}\ff")

  (global $g (@global-note "x") i32 (i32.const 7))

  (func $id (@func-note) (type $unary)
    (local.get 0))

  ;; Annotations between a folded instruction and its operands, and inside a
  ;; block's body.
  (func (export "annotated") (result i32)
    (block $b (@block-note) (result i32)
      (i32.add (@op-note)
        (call $id (i32.const 35))
        (global.get $g))))

  ;; Identifiers may hold every idchar, and a label may shadow an outer one:
  ;; `br $l` takes the innermost, so this returns from the inner block.
  (func (export "shadowed") (result i32)
    (block $l (result i32)
      (block $l (result i32)
        (br $l (i32.const 1)))))

  ;; The numeric spellings a printer never emits: separators, hex floats, a
  ;; NaN with a payload, and the two forms of the same 32-bit pattern.
  (func (export "literals") (result i32)
    (i32.add
      (i32.const 1_000_000)
      (i32.add
        (i32.const 0xffff_ffff)          ;; -1, written unsigned
        (i32.add
          (i32.trunc_f64_s (f64.const 0x1.8p+1))
          (i32.reinterpret_f32 (f32.const nan:0x20_0000))))))
)
