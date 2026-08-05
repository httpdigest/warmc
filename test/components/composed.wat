;; Two component instances, two memories, and an adapter between them.
;;
;; This is the shape composition produces, and it is the only shape a fused
;; lift-and-lower adapter has.  A `canon lower` of a `canon lift` inside *one*
;; component instance is not a smaller version of it: the Component Model
;; forbids re-entering an instance, so wasmtime traps such a call with "cannot
;; enter component instance" before any conversion happens.  An adapter
;; therefore always spans two instances -- and two instances have two
;; memories, which is what makes the conversion a copy between them rather
;; than within one.
;;
;; The graph resolves completely: the provider's exported instance is aliased
;; out, handed to the consumer as an instantiation argument, and the
;; consumer's `alias export` lands on the provider's own lifted function.
;; The adapter between them is the one warmc emits, and the export at the end
;; of it is `(param n: u32) -> u32`, which lifts as the identity -- so this is
;; the smallest component that exercises one.
;;
;; The answer depends on the bytes that crossed and not only on the length
;; beside them.  That is not fussiness: the length of the string `make` builds
;; is `n` again, so `len(5)` answers 5 whether the adapter copied the string
;; into the caller's memory, returned the callee's pointer unchanged, or
;; copied nothing at all.  Reading *through* the pointer, in the caller's own
;; memory, is what tells those apart.
(component
  ;; The provider: its own memory, exports an interface returning a string.
  (component $provider
    (core module $m
      (memory (export "mem") 1)
      (global $bump (mut i32) (i32.const 1024))
      (func (export "realloc") (param i32 i32 i32 i32) (result i32)
        global.get $bump
        global.get $bump local.get 3 i32.add global.set $bump)
      ;; `a`, `b`, `c`, ... wrapping at 26 so the result is ASCII for any n and
      ;; the string is valid UTF-8 whatever it is called with.  Distinct bytes
      ;; rather than one repeated one: the first and the last are what the
      ;; caller checks, and they have to differ from each other.
      (func (export "make") (param $n i32) (result i32)
        (local $p i32) (local $i i32)
        (local.set $p (call 0 (i32.const 0) (i32.const 0) (i32.const 1) (local.get $n)))
        (block $done (loop $l
          (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i32.store8 (i32.add (local.get $p) (local.get $i))
                      (i32.add (i32.const 97)
                               (i32.rem_u (local.get $i) (i32.const 26))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l)))
        (i32.store (i32.const 256) (local.get $p))
        (i32.store offset=4 (i32.const 256) (local.get $n))
        (i32.const 256)))
    (core instance $i (instantiate $m))
    (func $mk (param "n" u32) (result string)
      (canon lift (core func $i "make") (memory $i "mem") (realloc (func $i "realloc"))))
    (instance $iface (export "make" (func $mk)))
    (export "test:fuse/gen" (instance $iface)))

  ;; The consumer: a memory of its own, imports the interface.
  (component $consumer
    (import "test:fuse/gen" (instance $g
      (export "make" (func (param "n" u32) (result string)))))
    (alias export $g "make" (func $mk))
    (core module $mem
      (memory (export "mem") 1)
      (global $bump (mut i32) (i32.const 2048))
      (func (export "realloc") (param i32 i32 i32 i32) (result i32)
        global.get $bump
        global.get $bump local.get 3 i32.add global.set $bump))
    (core instance $mi (instantiate $mem))
    (core func $low (canon lower (func $mk)
      (memory $mi "mem") (realloc (func $mi "realloc"))))
    (core module $c
      (import "" "mk" (func $mk (param i32 i32)))
      (import "" "mem" (memory 1))
      ;; The first byte, the last byte and the length, in three fields of one
      ;; number, so each way of getting it wrong is a different answer.  An
      ;; empty string has no bytes to read and answers 0xABCD instead --
      ;; distinct from zero, which is what an unwritten cell would give.
      (func (export "len") (param $n i32) (result i32)
        (local $p i32) (local $l i32)
        (call $mk (local.get $n) (i32.const 512))
        (local.set $p (i32.load (i32.const 512)))
        (local.set $l (i32.load offset=4 (i32.const 512)))
        ;; And it has to have been allocated by *this* component's allocator,
        ;; which never hands out anything below where its own cursor starts.
        ;; Nothing else here can tell the two allocators apart: whatever
        ;; address one of them returns is where the copy is written and where
        ;; it is read back from, so an adapter that allocated the caller's
        ;; copy out of the callee's allocator answers correctly -- both
        ;; cursors are in range in both memories.  This is the only place the
        ;; identity of the allocator is observable at all.
        (if (i32.lt_u (local.get $p) (i32.const 2048))
          (then (return (i32.const 57005))))
        (if (result i32) (local.get $l)
          (then (i32.or
                  (i32.or
                    (i32.shl (i32.load8_u (local.get $p)) (i32.const 16))
                    (i32.shl (i32.load8_u (i32.add (local.get $p)
                                                   (i32.sub (local.get $l) (i32.const 1))))
                             (i32.const 8)))
                  (local.get $l)))
          (else (i32.const 43981)))))
    (core instance $ci (instantiate $c
      (with "" (instance (export "mk" (func $low)) (export "mem" (memory $mi "mem"))))))
    (func (export "len") (param "n" u32) (result u32)
      (canon lift (core func $ci "len"))))

  (instance $pi (instantiate $provider))
  (alias export $pi "test:fuse/gen" (instance $g))
  (instance $ci (instantiate $consumer (with "test:fuse/gen" (instance $g))))
  (alias export $ci "len" (func $len))
  (export "len" (func $len))
)
