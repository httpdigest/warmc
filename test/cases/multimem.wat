;; Several linear memories in one module.
;;
;; Memory 0 keeps the treatment it always had -- base and size cached in
;; x20/x21 for the whole function -- and every other memory loads what it needs
;; at the access.  The interesting failures are not "the wrong bytes come back"
;; but the ones where an analysis forgets that two memories share nothing: a
;; load forwarded from a store to a different memory, a bounds check reused
;; across memories of different sizes, a shared base pointer built for one and
;; used by the other.
(module
  (memory $a 2)
  (memory $b 1 4)
  (memory $c 1)

  (data (memory $a) (i32.const 0) "\01\02\03\04\05\06\07\08")
  (data (memory $b) (i32.const 0) "\11\12\13\14\15\16\17\18")
  (data (memory $c) (i32.const 0) "\21\22\23\24\25\26\27\28")
  (data $seg "passive")

  ;; ---- the same address in each memory holds something different ----
  (func (export "a32") (result i32) (i32.load $a (i32.const 0)))
  (func (export "b32") (result i32) (i32.load $b (i32.const 0)))
  (func (export "c32") (result i32) (i32.load $c (i32.const 0)))
  (func (export "a64") (result i64) (i64.load $a (i32.const 0)))
  (func (export "b64") (result i64) (i64.load $b (i32.const 0)))
  (func (export "a8s") (result i32) (i32.load8_s $a (i32.const 3)))
  (func (export "b8u") (result i32) (i32.load8_u $b (i32.const 3)))
  (func (export "c16s") (result i32) (i32.load16_s $c (i32.const 2)))
  (func (export "af32") (result f32) (f32.load $a (i32.const 0)))
  (func (export "bf64") (result f64) (f64.load $b (i32.const 0)))

  ;; ---- a store to one memory is not a store to another ----
  ;;
  ;; Both of these write byte 64, then read byte 64 back out of the *other*
  ;; memory.  A forwarding rule that compares addresses without comparing
  ;; memories answers with the value just stored.
  (func (export "no_forward_ab") (result i32)
    (i32.store $a (i32.const 64) (i32.const 0xAAAAAAAA))
    (i32.store $b (i32.const 64) (i32.const 0xBBBBBBBB))
    (i32.load $a (i32.const 64)))
  (func (export "no_forward_ba") (result i32)
    (i32.store $b (i32.const 68) (i32.const 0xBBBBBBBB))
    (i32.store $a (i32.const 68) (i32.const 0xAAAAAAAA))
    (i32.load $b (i32.const 68)))

  ;; A dead-store rule that ignores the memory would delete the first of these.
  (func (export "not_dead") (result i32)
    (i32.store $a (i32.const 72) (i32.const 111))
    (i32.store $b (i32.const 72) (i32.const 222))
    (i32.add (i32.load $a (i32.const 72)) (i32.load $b (i32.const 72))))

  ;; ---- sizes and growth are per memory ----
  (func (export "sizes") (result i32)
    (i32.add (i32.mul (memory.size $a) (i32.const 100))
             (i32.add (i32.mul (memory.size $b) (i32.const 10))
                      (memory.size $c))))
  (func (export "grow_b") (result i32)
    (i32.add (i32.mul (memory.grow $b (i32.const 2)) (i32.const 1000))
             (i32.add (i32.mul (memory.size $a) (i32.const 100))
                      (i32.add (i32.mul (memory.size $b) (i32.const 10))
                               (memory.size $c)))))
  ;; Past its maximum: the grow fails and nothing moves.
  (func (export "grow_b_fail") (result i32)
    (i32.add (memory.grow $b (i32.const 99)) (memory.size $b)))

  ;; ---- bulk operations name their memories ----
  (func (export "fill_b") (result i32)
    (memory.fill $b (i32.const 128) (i32.const 0x5a) (i32.const 4))
    (i32.add (i32.load $b (i32.const 128)) (i32.load $a (i32.const 128))))
  (func (export "copy_across") (result i32)
    (memory.fill $c (i32.const 160) (i32.const 0x3c) (i32.const 4))
    (memory.copy $a $c (i32.const 160) (i32.const 160) (i32.const 4))
    (i32.add (i32.load $a (i32.const 160)) (i32.load $c (i32.const 160))))
  ;; One memory index given, so the source is the default -- memory 0.  That
  ;; the two differ is the whole point: the bytes that arrive are $a's.
  (func (export "copy_b_from_a") (result i32)
    (memory.copy $b $b (i32.const 200) (i32.const 0) (i32.const 8))
    (i32.load $b (i32.const 200)))
  (func (export "init_c") (result i32)
    (memory.init $c $seg (i32.const 240) (i32.const 0) (i32.const 7))
    (i32.add (i32.load $c (i32.const 240)) (i32.load $a (i32.const 240))))

  ;; ---- a loop over one memory with a store to another inside it ----
  ;;
  ;; The store cannot be what the load reads, but only a rule that knows the
  ;; two memories are different can say so; the answer is the same either way,
  ;; which is why this one also checks the *bytes* rather than only running.
  (func (export "loop_mix") (result i32) (local $i i32) (local $acc i32)
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (i32.const 8)))
      (i32.store8 $c (i32.add (local.get $i) (i32.const 300)) (local.get $i))
      (local.set $acc (i32.add (local.get $acc)
                               (i32.load8_u $a (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (i32.add (i32.mul (local.get $acc) (i32.const 1000))
             (i32.load8_u $c (i32.const 307))))

  ;; ---- the same address value used against two memories ----
  ;;
  ;; One address computation, two memories: whatever is shared between the
  ;; accesses, the base cannot be.
  (func (export "shared_addr") (param) (result i32) (local $p i32)
    (local.set $p (i32.const 400))
    (i32.store $a (local.get $p) (i32.const 7))
    (i32.store $b (local.get $p) (i32.const 9))
    (i32.add (i32.mul (i32.load $a (local.get $p)) (i32.const 10))
             (i32.load $b (local.get $p))))

  ;; A large static offset, which is the shape that gets named as a constant
  ;; or turned into a shared base pointer when there is only one memory.
  (func (export "big_offset") (result i32)
    (i32.store $b offset=70000 (i32.const 0) (i32.const 0x1234))
    (i32.store $a offset=70000 (i32.const 0) (i32.const 0x5678))
    (i32.add (i32.mul (i32.load $b offset=70000 (i32.const 0)) (i32.const 3))
             (i32.load $a offset=70000 (i32.const 0))))
)
