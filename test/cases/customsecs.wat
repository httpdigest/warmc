;; Custom sections, which warmc carries into what it links: the name
;; section and DWARF's .debug_* left out by default, every other one kept
;; in the binary's order -- two of one name, an empty name, an empty
;; payload, one before the first section and one between two -- in a
;; read-only section of its own that the module descriptor points at.
;; test/check_macho.py, check_elf.py and check_pe.py read it back from each
;; format's image (test/custom_check.py); test/run_customs.py asks a shared
;; library for them by name.
(module
  (@custom "first" (before type) "1")
  (@custom "twice" "a")
  (@custom "" "nameless")
  (@custom "empty" "")
  (@custom "twice" (after type) "b")
  (@custom "name" "\00\02\01m")
  (@custom ".debug_info" "dwarf")
  (@custom "\u{2603}" "snow")
  (@producers (language "wat" "1") (processed-by "warmc" "test"))
  (type (func (result i32)))
  (func (export "answer") (type 0) (i32.const 42)))
