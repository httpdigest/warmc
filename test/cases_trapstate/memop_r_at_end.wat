;; A memory.copy or memory.fill of memory 0 on AArch64 that saves nothing
;; round it (isel.c emit_memop_r) checks both ranges before it moves a byte,
;; so one that runs a byte past the end of memory traps and leaves memory as
;; it was, whichever way it is made: a constant length inline, whose tests
;; branch to the trap itself (counted in exact_memop_checks); a variable one
;; up to 16 inline; and warmc_memmove_r's and warmc_memset_r's short shapes
;; and loops, and libSystem's that they hand a long one to on Darwin.  seed
;; writes a hash of each address into the whole memory and r hashes the
;; memory back; c's source, read past the end, traps the same.
;; stats: exact_memop_checks>0
;; run: seed r => 0 1580150027
;; run: seed c:131056,0,17 r => 0 trap 1580150027
;; run: seed c:131057,0,16 r => 0 trap 1580150027
;; run: seed c:131009,0,64 r => 0 trap 1580150027
;; run: seed c:130073,0,1000 r => 0 trap 1580150027
;; run: seed c:65537,0,65536 r => 0 trap 1580150027
;; run: seed c:0,130073,1000 r => 0 trap 1580150027
;; run: seed c:0,65537,65536 r => 0 trap 1580150027
;; run: seed f:131057,90,16 r => 0 trap 1580150027
;; run: seed f:131009,90,64 r => 0 trap 1580150027
;; run: seed f:130873,90,200 r => 0 trap 1580150027
;; run: seed f:130049,90,1024 r => 0 trap 1580150027
;; run: seed c96:130977 r => 0 trap 1580150027
;; run: seed c96s:130977 r => 0 trap 1580150027
;; run: seed f96:130977 r => 0 trap 1580150027
;; run: seed c97:130976 r => 0 trap 1580150027
;; run: seed f97:130976 r => 0 trap 1580150027
;; run: seed c:130072,0,1000 c:0,130072,1000 r => 0 0 0 1454636566
(module
  (memory 2)
  (func (export "seed") (result i32)
    (local $a i32)
    (loop $l
      (i32.store8 (local.get $a)
                  (i32.shr_u (i32.mul (local.get $a) (i32.const 0x9e3779b1)) (i32.const 24)))
      (br_if $l (i32.lt_u (local.tee $a (i32.add (local.get $a) (i32.const 1)))
                          (i32.const 131072))))
    (i32.const 0))
  (func (export "r") (result i32)
    (local $a i32) (local $h i32)
    (local.set $h (i32.const 0x811c9dc5))
    (loop $l
      (local.set $h (i32.mul (i32.xor (local.get $h) (i32.load8_u (local.get $a)))
                             (i32.const 0x01000193)))
      (br_if $l (i32.lt_u (local.tee $a (i32.add (local.get $a) (i32.const 1)))
                          (i32.const 131072))))
    (local.get $h))
  (func (export "c") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy (local.get $d) (local.get $s) (local.get $n))
    (i32.const 0))
  (func (export "f") (param $d i32) (param $v i32) (param $n i32) (result i32)
    (memory.fill (local.get $d) (local.get $v) (local.get $n))
    (i32.const 0))
  (func (export "c96") (param $d i32) (result i32)
    (memory.copy (local.get $d) (i32.const 0) (i32.const 96))
    (i32.const 0))
  (func (export "c96s") (param $s i32) (result i32)
    (memory.copy (i32.const 0) (local.get $s) (i32.const 96))
    (i32.const 0))
  (func (export "f96") (param $d i32) (result i32)
    (memory.fill (local.get $d) (i32.const 90) (i32.const 96))
    (i32.const 0))
  (func (export "c97") (param $d i32) (result i32)
    (memory.copy (local.get $d) (i32.const 0) (i32.const 97))
    (i32.const 0))
  (func (export "f97") (param $d i32) (result i32)
    (memory.fill (local.get $d) (i32.const 90) (i32.const 97))
    (i32.const 0))
)
