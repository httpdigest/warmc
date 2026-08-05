;; Spellings whose encoding rule nothing else here pins.
;;
;; Each of these was found by round-tripping the spec corpus, and each was a
;; bug: without a fixture the rule is carried only by code, and ablating it
;; produced no failure in the default run.  They live in one file rather than
;; in test/cases because none of them is a *valid* module -- they are all
;; shapes `assert_invalid` asserts about -- and because run_wat.py is the only
;; suite that reads test/wat: it assembles and compares, and never runs.
;;
;; wabt refuses the file whole, because wat2wasm validates as it assembles.
;; That is the point of wasm-tools being the primary oracle: the assembler's
;; job is to encode what the text says and let the validator judge it, and the
;; corner cases of that rule are exactly where the two tools part company.

(module
  ;; `select (result)` is the *typed* opcode 0x1c with an empty type vector,
  ;; not the untyped 0x1b.  What decides it is whether a `(result ...)` was
  ;; written, not whether it holds anything.  select.wast asserts on this.
  (func $empty_select (result i32)
    nop
    nop
    i32.const 1
    select (result))

  ;; The data count section is required as soon as the code names a segment,
  ;; even when the module declares none: without it `data.drop` is malformed
  ;; rather than invalid, so the count is emitted and it is zero.
  (func $drop_nothing
    data.drop 0)

  ;; A constant expression has no locals, so this names one that cannot exist.
  ;; It still has an encoding, and global.wast asserts the verdict is invalid
  ;; -- which is the validator's answer to give, not the assembler's.
  (global f32 local.get 0)

  ;; An offset that is not an integer at all.  The abbreviated active form
  ;; takes the first parenthesised group as the offset, and `(ref.null func)`
  ;; is an instruction rather than a reference type, so it is one -- elem.wast
  ;; asserts that the resulting module is invalid.
  (table 1 funcref)
  (elem (ref.null func) func)
)
