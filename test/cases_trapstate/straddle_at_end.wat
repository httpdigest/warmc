;; A store wider than a byte, or a constant-length memory.fill or copy, that
;; runs past the end of memory traps and writes nothing.  Under guard pages
;; nothing tests them: the hardware faults at the page.  AArch64 lets a
;; store that crosses into the page write its bytes before it when it faults,
;; so with -fexact-trap they are tested first.  Apple's cores write nothing
;; there either way, which is why the stats line asks that the tests be made
;; (at -O2, with the flag): exact_store_checks for the stores and
;; exact_memop_checks for the bulk ops.  r reads the two bytes at 65534 that
;; each would write first; they hold 0xaaaa.
;; stats: exact_store_checks>0 exact_memop_checks>0
;; run: s:65534 r => trap 43690
;; run: d:65532 r => trap 43690
;; run: v:65528 r => trap 43690
;; run: f:65530 r => trap 43690
;; run: c:65530 r => trap 43690
;; run: s:16 q => 0 13124
(module
  (memory (export "memory") 1)
  (data (i32.const 65528) "\aa\aa\aa\aa\aa\aa\aa\aa")
  (func (export "s") (param $p i32) (result i32)
    (i32.store (local.get $p) (i32.const 0x11223344))
    (i32.const 0))
  (func (export "d") (param $p i32) (result i32)
    (i64.store (local.get $p) (i64.const 0x1122334455667788))
    (i32.const 0))
  (func (export "v") (param $p i32) (result i32)
    (v128.store (local.get $p) (v128.const i32x4 1 2 3 4))
    (i32.const 0))
  (func (export "f") (param $p i32) (result i32)
    (memory.fill (local.get $p) (i32.const 0x55) (i32.const 16))
    (i32.const 0))
  (func (export "c") (param $p i32) (result i32)
    (memory.copy (local.get $p) (i32.const 0) (i32.const 16))
    (i32.const 0))
  (func (export "r") (result i32) (i32.load16_u (i32.const 65534)))
  (func (export "q") (result i32) (i32.load16_u (i32.const 16)))
)
