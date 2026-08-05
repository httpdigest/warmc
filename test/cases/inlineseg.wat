;; The two abbreviations that write a segment inside the definition it fills.
;;
;; `(memory (data ...))` and `(table T (elem ...))` are shorthand for a memory
;; or table sized to the contents plus an active segment at offset zero.  The
;; parser built that segment with an *empty* offset expression and left the
;; emitter to supply the zero, and the emitter had the code to do it --
;; `zero_offset` writes `i32.const 0; end` -- reached through a test for an
;; empty encoding.  But the encoder was asked to terminate the empty range,
;; and terminating it produces one byte, the `end` opcode.  So the encoding
;; was never empty, the fallback never ran, and both abbreviations emitted an
;; offset expression consisting of nothing but its own terminator.
;;
;; warmc then refused its own output -- "a constant expression must leave one
;; value, not 0" -- while `--emit-wasm` exited 0 and wrote a module wasm-tools
;; rejects.  Nothing under test/ used either abbreviation, and test/run_wat.py
;; compares every .wat here against wasm-tools byte for byte, so this file is
;; both the case and the check.
;;
;; The memory half only.  The table half is checked for acceptance in
;; test/run_wat.py instead: the three assemblers expand `(table T (elem $f))`
;; to different -- all valid -- encodings, warmc to the `func x*` form the
;; abbreviation is defined as and wabt to the expression form, so a
;; byte-identity fixture there would be asserting an encoder's taste.
(module
  (memory (data "hello"))

  ;; The data landed at offset zero, which is the whole content of the
  ;; abbreviation: an offset expression that produced no value could not have
  ;; put it anywhere.
  (func (export "data_at_zero") (result i32)
    (i32.load8_u (i32.const 0)))
  (func (export "data_tail") (result i32)
    (i32.load8_u (i32.const 4)))

  ;; The memory the abbreviation sized: one page, because the contents fit in
  ;; one and the shorthand rounds up to whole pages.
  (func (export "mem_pages") (result i32) (memory.size))
)
