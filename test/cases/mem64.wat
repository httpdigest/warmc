;; 64-bit index spaces: a memory and a table addressed by an i64.
;;
;; The interesting part is not that an i64 address works for small numbers --
;; the low half of one looks exactly like an i32 -- but that the top half is
;; not thrown away.  Everything here that could be answered by truncating to
;; 32 bits is paired with something that cannot; the addresses past 2^32 are
;; in test/cases_traps/traps.wat, where the answer is a trap rather than a
;; value.
;;
;; A 32-bit memory and a 32-bit table sit alongside, because the index type is
;; per definition and the two must not be confused with each other.
(module
  (memory $w i64 2 4)
  (memory $n 1)
  (table $tw i64 4 funcref)
  (table $tn 4 funcref)
  ;; A table of its own for the call that has to stay a call: nothing else
  ;; writes it, and reaching $clob through it is what keeps the inliner from
  ;; removing the register pressure the case is about.
  (table $tc 1 funcref)
  (type $void_i32 (func (result i32)))
  (type $unary (func (param i32) (result i32)))

  (data (memory $w) (i64.const 0) "\01\02\03\04\05\06\07\08")
  (data (memory $n) (i32.const 0) "\11\12\13\14\15\16\17\18")
  (data $seg "wide-passive")

  (elem (table $tw) (i64.const 1) funcref (ref.func $ret7))
  (elem (table $tn) (i32.const 1) funcref (ref.func $ret9))
  (elem $eseg funcref (ref.func $ret7) (ref.func $ret9))
  (elem (table $tc) (i32.const 0) funcref (ref.func $clob))

  (func $ret7 (result i32) (i32.const 7))
  (func $ret9 (result i32) (i32.const 9))

  ;; ---- ordinary accesses through a 64-bit address ----
  (func (export "w32") (result i32) (i32.load $w (i64.const 0)))
  (func (export "n32") (result i32) (i32.load $n (i32.const 0)))
  (func (export "w64") (result i64) (i64.load $w (i64.const 0)))
  (func (export "w8s") (result i32) (i32.load8_s $w (i64.const 3)))
  (func (export "w16u") (result i32) (i32.load16_u $w (i64.const 2)))
  (func (export "wf64") (result f64) (f64.load $w (i64.const 0)))
  (func (export "wstore") (result i64)
    (i64.store $w (i64.const 64) (i64.const 0x0102030405060708))
    (i64.load $w (i64.const 64)))

  ;; An address held in a local, so nothing can fold it to a constant, and one
  ;; that is genuinely 64-bit arithmetic on the way in.
  (func (export "wdyn") (result i32) (local $p i64)
    (local.set $p (i64.mul (i64.const 0x10000) (i64.const 1)))
    (i32.store $w (local.get $p) (i32.const 0x11223344))
    (i32.load $w (i64.add (local.get $p) (i64.const 0))))

  ;; A large static offset with a small address: the sum is what is checked,
  ;; and both halves of it are 64-bit.
  (func (export "woff") (result i32)
    (i32.store $w offset=100000 (i64.const 4) (i32.const 0x55667788))
    (i32.load $w offset=100000 (i64.const 4)))

  ;; ---- size and growth are i64 here and i32 next door ----
  (func (export "wsize") (result i64) (memory.size $w))
  (func (export "nsize") (result i32) (memory.size $n))
  (func (export "wgrow") (result i64)
    (i64.add (i64.mul (memory.grow $w (i64.const 1)) (i64.const 1000))
             (memory.size $w)))
  ;; Past the declared maximum: all ones, at 64 bits wide rather than 32.
  (func (export "wgrow_fail") (result i64) (memory.grow $w (i64.const 100)))

  ;; ---- bulk operations with 64-bit lengths ----
  (func (export "wfill") (result i64)
    (memory.fill $w (i64.const 128) (i32.const 0x5a) (i64.const 8))
    (i64.load $w (i64.const 128)))
  (func (export "wcopy") (result i64)
    (memory.copy $w $w (i64.const 200) (i64.const 0) (i64.const 8))
    (i64.load $w (i64.const 200)))
  (func (export "winit") (result i32)
    (memory.init $w $seg (i64.const 300) (i32.const 0) (i32.const 4))
    (i32.load $w (i64.const 300)))

  ;; ---- a loop whose induction variable is the 64-bit address ----
  (func (export "wloop") (result i64) (local $i i64) (local $acc i64)
    (block $done (loop $l
      (br_if $done (i64.ge_u (local.get $i) (i64.const 8)))
      (i64.store8 $w (i64.add (local.get $i) (i64.const 400)) (local.get $i))
      (local.set $acc (i64.add (local.get $acc)
                               (i64.load8_u $w (local.get $i))))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br $l)))
    (i64.add (i64.mul (local.get $acc) (i64.const 1000))
             (i64.load8_u $w (i64.const 407))))

  ;; ---- a narrow index widened into a 64-bit address ----
  ;;
  ;; `[base, Wn, uxtw]` performs the widening for itself, so the
  ;; `i64.extend_i32_u` a producer writes down here is an instruction nobody
  ;; has to emit -- and folding it means the i32 underneath has to still be in
  ;; a register at the access rather than at the extension.  These are the
  ;; shapes where the difference between those two points is visible.
  (func $clob (param i32) (result i32)
    ;; Enough live values to make the caller's registers worth taking.
    (local $a i32) (local $b i32) (local $c i32)
    (local.set $a (i32.mul (local.get 0) (i32.const 7)))
    (local.set $b (i32.xor (local.get $a) (i32.const 0x5a5a)))
    (local.set $c (i32.add (local.get $a) (local.get $b)))
    (i32.store $n (i32.const 32) (local.get $c))
    (i32.add (local.get $c) (i32.load $n (i32.const 32))))
  (func (export "zx_addr") (result i32)
    (i32.store $w (i64.extend_i32_u (i32.and (i32.load $n (i32.const 0)) (i32.const 63)))
                  (i32.const 0x0badf00d))
    (i32.load $w (i64.extend_i32_u (i32.and (i32.load $n (i32.const 0)) (i32.const 63)))))
  ;; One extension, several accesses at different displacements: it folds into
  ;; every one of them, and the addressing mode re-does it free each time.
  (func (export "zx_shared") (result i32) (local $p i64)
    (local.set $p (i64.extend_i32_u (i32.and (i32.load $n (i32.const 0)) (i32.const 15))))
    (i32.store $w offset=512 (local.get $p) (i32.const 11))
    (i32.store $w offset=1024 (local.get $p) (i32.const 22))
    (i32.add (i32.load $w offset=512 (local.get $p))
             (i32.mul (i32.load $w offset=1024 (local.get $p)) (i32.const 3))))
  ;; A call stands between the extension and the access, so the value has to
  ;; survive it -- which it only does if the register allocator was told the
  ;; access reads the i32 and not the widened copy that is never emitted.
  ;; No static offset on either access, so the whole address folds into the
  ;; addressing mode and the i32 is what the load reads; with an offset the
  ;; sum is a value of its own and lives across the call instead, which tests
  ;; nothing about this.
  (func (export "zx_across_call") (result i32) (local $p i64)
    (local.set $p (i64.extend_i32_u
      (i32.or (i32.and (i32.load $n (i32.const 0)) (i32.const 31)) (i32.const 2048))))
    (i32.store $w (local.get $p) (i32.const 0x1234))
    (i32.add (call_indirect $tc (type $unary) (i32.const 5) (i32.const 0))
             (i32.load $w (local.get $p))))
  ;; The widened value is also used as a number, so it is not free any more
  ;; and must not be folded -- and the answer has to come out the same.
  (func (export "zx_also_arith") (result i64) (local $p i64)
    (local.set $p (i64.extend_i32_u (i32.and (i32.load $n (i32.const 0)) (i32.const 63))))
    (i32.store $w offset=4096 (local.get $p) (i32.const 77))
    (i64.add (i64.extend_i32_u (i32.load $w offset=4096 (local.get $p)))
             (i64.mul (local.get $p) (i64.const 1000))))
  ;; A widened index past the first page, so a fold that dropped bits above
  ;; sixteen would land somewhere else and say so.
  (func (export "zx_high") (result i32) (local $p i64)
    (local.set $p (i64.extend_i32_u
      (i32.or (i32.and (i32.load $n (i32.const 0)) (i32.const 0xfffc))
              (i32.const 0x10000))))
    (i32.store $w (local.get $p) (i32.const 0x600d))
    (i32.add (i32.load $w (local.get $p))
             (i32.wrap_i64 (i64.shr_u (local.get $p) (i64.const 12)))))

  ;; ---- a 64-bit table ----
  (func (export "twsize") (result i64) (table.size $tw))
  (func (export "tnsize") (result i32) (table.size $tn))
  (func (export "twcall") (result i32)
    (call_indirect $tw (type $void_i32) (i64.const 1)))
  (func (export "tncall") (result i32)
    (call_indirect $tn (type $void_i32) (i32.const 1)))
  (func (export "twset") (result i32)
    (table.set $tw (i64.const 3) (ref.func $ret9))
    (call_indirect $tw (type $void_i32) (i64.const 3)))
  (func (export "twnull") (result i32)
    (ref.is_null (table.get $tw (i64.const 0))))
  (func (export "twgrow") (result i64)
    (i64.add (i64.mul (table.grow $tw (ref.null func) (i64.const 2)) (i64.const 1000))
             (table.size $tw)))
  (func (export "twfill") (result i32)
    (table.fill $tw (i64.const 4) (ref.func $ret7) (i64.const 2))
    (call_indirect $tw (type $void_i32) (i64.const 5)))
  (func (export "twinit") (result i32)
    (table.init $tw $eseg (i64.const 2) (i32.const 1) (i32.const 1))
    (call_indirect $tw (type $void_i32) (i64.const 2)))
  ;; Across the two index widths, whose lengths are both host words.
  (func (export "twcopy") (result i32)
    (table.copy $tn $tw (i32.const 2) (i64.const 1) (i32.const 1))
    (call_indirect $tn (type $void_i32) (i32.const 2)))
)
