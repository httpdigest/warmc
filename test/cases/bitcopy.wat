;; Floats copied as integer bits, as the C ABI copies a struct's doubles with
;; i64.load and i64.store into a frame and the callee reads them back as
;; f64: forwarded, each read is a reinterpret of the integer load, and each
;; float stored back for the caller to copy is a reinterpret the other way.
;; The loads become float loads, the stores store the float, and a
;; reinterpret undone is the value it started from.  NaNs with payloads go
;; through every path that only moves them, since loads and stores move bits
;; exactly; arithmetic is only on the other values, as a NaN it makes may
;; carry any payload.  Some shapes must stay as they are: a load also read
;; as an integer, a narrow store of a reinterpreted value, a load passed to
;; another block, and one used as an address.

(module
  (memory 1)
  (data (i32.const 0)
    ;; three doubles: 1.5, a signalling NaN with a payload, -0.0
    "\00\00\00\00\00\00\f8\3f"
    "\23\01\00\00\00\00\f4\7f"
    "\00\00\00\00\00\00\00\80"
    ;; three floats: 2.5, a NaN with a payload, a denormal
    "\00\00\20\40" "\42\00\a0\ff" "\01\00\00\00")

  (func $mix (param $h i64) (param $x i64) (result i64)
    (i64.add (i64.rotl (local.get $h) (i64.const 7)) (local.get $x)))

  ;; a V3 copied into a frame slot, scaled there by a callee inlined into
  ;; place, and the result copied back out, all through i64
  (func (export "v3_copy_scale") (result i64)
    (local $i i32) (local $h i64)
    (loop $l
      ;; argument copy: 0..24 -> 256..280
      (i64.store offset=256 (i32.const 0) (i64.load offset=0 (i32.const 0)))
      (i64.store offset=264 (i32.const 0) (i64.load offset=8 (i32.const 0)))
      (i64.store offset=272 (i32.const 0) (i64.load offset=16 (i32.const 0)))
      ;; the callee: result at 288..312
      (f64.store offset=288 (i32.const 0)
        (f64.mul (f64.load offset=256 (i32.const 0)) (f64.convert_i32_u (local.get $i))))
      (f64.store offset=296 (i32.const 0) (f64.load offset=264 (i32.const 0)))
      (f64.store offset=304 (i32.const 0)
        (f64.mul (f64.load offset=272 (i32.const 0)) (f64.const 2)))
      ;; result copied to 320..344 and read as bits
      (i64.store offset=320 (i32.const 0) (i64.load offset=288 (i32.const 0)))
      (i64.store offset=328 (i32.const 0) (i64.load offset=296 (i32.const 0)))
      (i64.store offset=336 (i32.const 0) (i64.load offset=304 (i32.const 0)))
      (local.set $h (call $mix (local.get $h) (i64.load offset=320 (i32.const 0))))
      (local.set $h (call $mix (local.get $h) (i64.load offset=328 (i32.const 0))))
      (local.set $h (call $mix (local.get $h) (i64.load offset=336 (i32.const 0))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 50))))
    (local.get $h))

  ;; the same in 32 bits
  (func (export "f32_copy") (result i32)
    (local $i i32) (local $h i32)
    (loop $l
      (i32.store offset=400 (i32.const 0) (i32.load offset=24 (i32.const 0)))
      (i32.store offset=404 (i32.const 0) (i32.load offset=28 (i32.const 0)))
      (i32.store offset=408 (i32.const 0) (i32.load offset=32 (i32.const 0)))
      (f32.store offset=412 (i32.const 0)
        (f32.add (f32.load offset=400 (i32.const 0)) (f32.convert_i32_u (local.get $i))))
      (i32.store offset=416 (i32.const 0) (i32.reinterpret_f32 (f32.load offset=404 (i32.const 0))))
      (i32.store offset=420 (i32.const 0) (i32.reinterpret_f32 (f32.load offset=408 (i32.const 0))))
      (local.set $h (i32.add (i32.rotl (local.get $h) (i32.const 5))
        (i32.xor (i32.load offset=412 (i32.const 0))
          (i32.xor (i32.load offset=416 (i32.const 0)) (i32.load offset=420 (i32.const 0))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 50))))
    (local.get $h))

  ;; a float's bits stored as an integer, and taken there and back
  (func (export "store_reinterpreted") (result i64)
    (local $x f64) (local $y f32)
    (local.set $x (f64.load offset=8 (i32.const 0)))
    (local.set $y (f32.load offset=28 (i32.const 0)))
    (i64.store offset=512 (i32.const 0) (i64.reinterpret_f64 (local.get $x)))
    (i32.store offset=520 (i32.const 0) (i32.reinterpret_f32 (local.get $y)))
    (f64.store offset=528 (i32.const 0)
      (f64.reinterpret_i64 (i64.reinterpret_f64 (f64.load offset=16 (i32.const 0)))))
    (i64.add (i64.load offset=512 (i32.const 0))
      (i64.add (i64.extend_i32_u (i32.load offset=520 (i32.const 0)))
        (i64.load offset=528 (i32.const 0)))))

  ;; only the low half of a double's bits is stored
  (func (export "narrow_store") (result i64)
    (i64.store offset=600 (i32.const 0) (i64.const -1))
    (i64.store32 offset=600 (i32.const 0) (i64.reinterpret_f64 (f64.load offset=8 (i32.const 0))))
    (i64.store16 offset=604 (i32.const 0) (i64.reinterpret_f64 (f64.load offset=0 (i32.const 0))))
    (i64.load offset=600 (i32.const 0)))

  ;; the loaded bits are read as an integer too
  (func (export "mixed_reader") (result i64)
    (local $b i64)
    (local.set $b (i64.load offset=0 (i32.const 0)))
    (i64.store offset=640 (i32.const 0) (local.get $b))
    (i64.xor
      (i64.reinterpret_f64 (f64.add (f64.reinterpret_i64 (local.get $b)) (f64.const 1)))
      (i64.shr_u (local.get $b) (i64.const 3))))

  ;; the loaded bits are passed to another block, then reinterpreted
  (func (export "across_blocks") (param) (result i64)
    (local $b i64) (local $i i32) (local $s f64)
    (loop $l
      (local.set $b (i64.load offset=0 (i32.shl (i32.and (local.get $i) (i32.const 1)) (i32.const 4))))
      (if (i32.and (local.get $i) (i32.const 2))
        (then (i64.store offset=680 (i32.const 0) (local.get $b)))
        (else (local.set $s (f64.add (local.get $s) (f64.reinterpret_i64 (local.get $b))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 9))))
    (i64.add (i64.reinterpret_f64 (local.get $s)) (i64.load offset=680 (i32.const 0))))

  ;; a loaded value stored as the address of another store
  (func (export "loaded_address") (result i64)
    (i64.store offset=720 (i32.const 0) (i64.const 740))
    (i64.store (i32.wrap_i64 (i64.load offset=720 (i32.const 0)))
      (i64.reinterpret_f64 (f64.reinterpret_i64 (i64.load offset=720 (i32.const 0)))))
    (i64.add (i64.load offset=740 (i32.const 0))
      (i64.reinterpret_f64 (f64.add (f64.reinterpret_i64 (i64.load offset=720 (i32.const 0)))
                                    (f64.const 0)))))
)
