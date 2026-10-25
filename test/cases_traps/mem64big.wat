;; A 64-bit memory past 4 GiB: 65537 pages, one more than a 32-bit memory
;; could ever address.  Each export checks its own answer -- a wrong one is
;; `unreachable` -- or makes the access past the end it is named for, which
;; must trap out of bounds.  Its memory costs address space, not memory: only
;; the few pages these touch are ever written.
(module
  (memory i64 65537)
  (data (i64.const 0x1_0000_0800) "above")
  (data $d "warmc-mem64")

  (func $check (param i32) (if (i32.eqz (local.get 0)) (then unreachable)))

  ;; memory.size is the page count, past 2^16
  (func (export "big_size")
    (call $check (i64.eq (memory.size) (i64.const 65537))))

  ;; every width, stored and loaded back just past 4 GiB, at an address that
  ;; is not taken mod 2^32: the bytes at 0x10 stay as they were
  (func (export "big_widths")
    (local $a i64)
    (local.set $a (i64.const 0x1_0000_0010))
    (i64.store8 (local.get $a) (i64.const 0x81))
    (call $check (i64.eq (i64.load8_u (local.get $a)) (i64.const 0x81)))
    (call $check (i64.eq (i64.load8_s (local.get $a)) (i64.const -127)))
    (i32.store16 offset=2 (local.get $a) (i32.const 0xbeef))
    (call $check (i32.eq (i32.load16_u offset=2 (local.get $a)) (i32.const 0xbeef)))
    (i32.store offset=4 (local.get $a) (i32.const 0x12345678))
    (call $check (i32.eq (i32.load offset=4 (local.get $a)) (i32.const 0x12345678)))
    (i64.store offset=8 (local.get $a) (i64.const 0x0102030405060708))
    (call $check (i64.eq (i64.load offset=8 (local.get $a)) (i64.const 0x0102030405060708)))
    (f32.store offset=16 (local.get $a) (f32.const 1.5))
    (call $check (f32.eq (f32.load offset=16 (local.get $a)) (f32.const 1.5)))
    (f64.store offset=24 (local.get $a) (f64.const -2.25))
    (call $check (f64.eq (f64.load offset=24 (local.get $a)) (f64.const -2.25)))
    (v128.store offset=32 (local.get $a) (v128.const i32x4 1 2 3 4))
    (call $check (i32.eq (i32x4.extract_lane 3 (v128.load offset=32 (local.get $a)))
                         (i32.const 4)))
    (call $check (i64.eqz (i64.load (i64.const 0x10))))
    (call $check (i64.eqz (i64.load (i64.const 0x18)))))

  ;; an offset past 2^32 on a small address
  (func (export "big_offset")
    (i32.store offset=0x1_0000_0100 (i64.const 4) (i32.const 77))
    (call $check (i32.eq (i32.load (i64.const 0x1_0000_0104)) (i32.const 77)))
    (call $check (i32.eqz (i32.load (i64.const 0x104)))))

  ;; an active segment above 4 GiB, written at instantiation
  (func (export "big_active")
    (call $check (i32.eq (i32.load8_u (i64.const 0x1_0000_0800)) (i32.const 0x61)))
    (call $check (i32.eq (i32.load8_u (i64.const 0x1_0000_0804)) (i32.const 0x65))))

  ;; the memory's last four bytes, and accesses one byte past them through
  ;; the address, through the offset, and through an address the compiler
  ;; cannot see
  (func (export "big_last")
    (i32.store (i64.const 0x1_0000_fffc) (i32.const 5))
    (call $check (i32.eq (i32.load (i64.const 0x1_0000_fffc)) (i32.const 5))))
  (func (export "big_past")
    (drop (i32.load (i64.const 0x1_0000_fffd))))
  (func (export "big_past_offset")
    (drop (i64.load offset=0x1_0000_fff9 (i64.const 0))))
  (func $at (param i64) (result i64) (local.get 0))
  (func (export "big_past_dyn")
    (i32.store8 (call $at (i64.const 0x1_0001_0000)) (i32.const 1)))

  ;; growing past 65537 pages: the new page is there, and the old size is
  ;; what grow returns
  (func (export "big_grow")
    (call $check (i64.eq (memory.grow (i64.const 1)) (i64.const 65537)))
    (call $check (i64.eq (memory.size) (i64.const 65538)))
    (i32.store (i64.const 0x1_0001_fffc) (i32.const 9))
    (call $check (i32.eq (i32.load (i64.const 0x1_0001_fffc)) (i32.const 9))))

  ;; and with no maximum declared, not past the 2^20 pages (64 GiB) the
  ;; runtime reserves for it: grow answers -1
  (func (export "big_grow_cap")
    (call $check (i64.eq (memory.grow (i64.const 983040)) (i64.const -1)))
    (call $check (i64.eq (memory.size) (i64.const 65537))))

  ;; bulk operations across the 4 GiB line, and one byte past the end
  (func (export "big_fill")
    (memory.fill (i64.const 0xffff_fff8) (i32.const 0xab) (i64.const 16))
    (call $check (i64.eq (i64.load (i64.const 0xffff_fff8)) (i64.const 0xabababababababab)))
    (call $check (i64.eq (i64.load (i64.const 0x1_0000_0000)) (i64.const 0xabababababababab)))
    (call $check (i32.eqz (i32.load8_u (i64.const 0x1_0000_0008)))))
  (func (export "big_copy")
    (i64.store (i64.const 0x100) (i64.const 0x1122334455667788))
    (memory.copy (i64.const 0x1_0000_0200) (i64.const 0x100) (i64.const 8))
    (call $check (i64.eq (i64.load (i64.const 0x1_0000_0200)) (i64.const 0x1122334455667788)))
    (memory.copy (i64.const 0x300) (i64.const 0x1_0000_0200) (i64.const 8))
    (call $check (i64.eq (i64.load (i64.const 0x300)) (i64.const 0x1122334455667788))))
  (func (export "big_init")
    (memory.init $d (i64.const 0x1_0000_0400) (i32.const 0) (i32.const 11))
    (call $check (i32.eq (i32.load8_u (i64.const 0x1_0000_0400)) (i32.const 0x77)))
    (call $check (i32.eq (i32.load8_u (i64.const 0x1_0000_040a)) (i32.const 0x34))))
  (func (export "big_fill_past")
    (memory.fill (i64.const 0x1_0000_fff0) (i32.const 1) (i64.const 17)))
  (func (export "big_copy_past")
    (memory.copy (i64.const 0) (i64.const 0x1_0000_fff0) (i64.const 17)))

  ;; a byte loop either side of the line, the vectoriser's shape, and one
  ;; that runs a byte past the end
  (func $sum (param $from i64) (param $to i64) (result i32)
    (local $s i32)
    (loop $l
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $from))))
      (local.set $from (i64.add (local.get $from) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $from) (local.get $to))))
    (local.get $s))
  (func (export "big_loop")
    (memory.fill (i64.const 0xffff_fc00) (i32.const 1) (i64.const 2048))
    (call $check (i32.eq (call $sum (i64.const 0xffff_fc00) (i64.const 0x1_0000_0400))
                         (i32.const 2048))))
  (func (export "big_loop_past")
    (drop (call $sum (i64.const 0x1_0000_fc00) (i64.const 0x1_0001_0001)))))
