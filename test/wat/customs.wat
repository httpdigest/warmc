;; Custom sections in the text: (@custom ...), (@producers ...) and
;; (@dylink.0 ...), which test/run_wat.py compares byte for byte with what
;; wasm-tools writes for them.
;;
;; @custom at each place a section can be named: before the first section
;; and after the last, and before and after each section's place, whether
;; the module has that section or not -- this one has no import, table, tag,
;; start or elem, and the custom sections anchored to them stand where those
;; would.  Several at one place stay in the order of the text, a datastring
;; may be several strings or none, and a name may be empty or any UTF-8.
;; @producers writes its fields as language, sdk, processed-by whatever
;; order the text gives them in, and @dylink.0 makes one subsection of each
;; run of export-info or import-info entries, its flags given by number or
;; by name.  An annotation that is no custom section is white space here as
;; everywhere (test/wat/annotations.wat).

(module (@module-note "ignored")
  (@custom "first" (before first) "1" "\00\ff")
  (@custom "last by default" "tail")
  (@dylink.0
    (mem-info (memory 16 4) (table 2 0))
    (needed "libc.so" "libm.so")
    (export-info "f" binding-weak exported)
    (export-info "g" 0x20 tls)
    (import-info "env" "h" undefined)
    (runtime-path "/a" "/b")
    (target-arch "wasm32")
    (export-info "late" no-strip visibility-hidden))
  (type (func (result i32)))
  (@custom "after type" (after type) "t")
  (@custom "before import" (before import) "i")
  (@custom "after import" (after import) "i2")
  (@custom "before func" (before func) "f")
  (@custom "after table" (after table) "no table here")
  (@custom "before memory" (before memory) "m")
  (@custom "before tag" (before tag) "")
  (@custom "after tag" (after tag))
  (@custom "before global" (before global) "g")
  (@custom "after global" (after global) "g2")
  (@custom "before export" (before export) "x")
  (@custom "after export" (after export) "x2")
  (@custom "before start" (before start))
  (@custom "after start" (after start) "s")
  (@custom "before elem" (before elem) "e")
  (@custom "after elem" (after elem) "e2")
  (@custom "before code" (before code) "c")
  (@custom "after code" (after code) "c2")
  (@custom "go:buildid" (after code) "two" "strings")
  (@custom "before data" (before data) "d")
  (@custom "after data" (after data) "d2")
  (@custom "after last" (after last) "L")
  (@producers
    (processed-by "warmc" "1.0")
    (language "wat" "1")
    (sdk "none" "0")
    (language "c" "17"))
  (@custom "" "")
  (@custom "\u{2603}" "snow")
  (memory 1)
  (global $g i32 (i32.const 7))
  (func (@func-note) (export "f") (type 0)
    (global.get $g))
  (data (i32.const 0) "hi"))
