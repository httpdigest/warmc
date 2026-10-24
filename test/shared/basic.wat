;; The module test/run_shared.sh builds as a shared library and calls from C
;; and from Python's ctypes: scalars of each type in and out, a trap, several
;; results, linear memory, a host import, recursion deep enough to reach the
;; stack limit, and the collector.
(module
  (type $node (struct (field i64) (field (ref null $node))))
  (import "env" "twice" (func $twice (param i64) (result i64)))
  (memory (export "memory") 1)
  (global $calls (mut i32) (i32.const 0))
  (global $list (mut (ref null $node)) (ref.null $node))

  (func (export "add") (param i32 i32) (result i32)
    (global.set $calls (i32.add (global.get $calls) (i32.const 1)))
    (i32.add (local.get 0) (local.get 1)))

  (func (export "calls") (result i32) (global.get $calls))

  (func $fib (export "fib") (param i32) (result i64)
    (if (result i64) (i32.lt_u (local.get 0) (i32.const 2))
      (then (i64.extend_i32_u (local.get 0)))
      (else (i64.add (call $fib (i32.sub (local.get 0) (i32.const 1)))
                     (call $fib (i32.sub (local.get 0) (i32.const 2)))))))

  ;; floats and integers interleaved, so each class's registers are counted
  ;; apart: 2.5 * 4 + 3 - 0.25 = 12.75
  (func (export "mix") (param f64 i32 f32 i64) (result f64)
    (f64.sub
      (f64.add (f64.mul (local.get 0) (f64.convert_i32_s (local.get 1)))
               (f64.convert_i64_s (local.get 3)))
      (f64.promote_f32 (local.get 2))))

  (func (export "half") (param f32) (result f32) (f32.mul (local.get 0) (f32.const 0.5)))

  (func (export "quot") (param i32 i32) (result i32) (i32.div_s (local.get 0) (local.get 1)))

  (func (export "divmod") (param i32 i32) (result i32 i64)
    (i32.div_u (local.get 0) (local.get 1))
    (i64.extend_i32_u (i32.rem_u (local.get 0) (local.get 1))))

  (func (export "seven") (param i64 i64 i64 i64 i64 i64 i64) (result i64)
    (i64.add (local.get 0)
      (i64.add (i64.mul (local.get 1) (i64.const 10))
        (i64.add (i64.mul (local.get 2) (i64.const 100))
          (i64.add (i64.mul (local.get 3) (i64.const 1000))
            (i64.add (i64.mul (local.get 4) (i64.const 10000))
              (i64.add (i64.mul (local.get 5) (i64.const 100000))
                       (i64.mul (local.get 6) (i64.const 1000000)))))))))

  (func (export "twice_plus_one") (param i64) (result i64)
    (i64.add (call $twice (local.get 0)) (i64.const 1)))

  ;; the sum of the bytes [p, p+n) of linear memory
  (func (export "sum") (param $p i32) (param $n i32) (result i32)
    (local $s i32)
    (block $done
      (loop $next
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $next)))
    (local.get $s))

  (func $deep (export "deep") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (i32.const 1) (call $deep (i32.sub (local.get 0) (i32.const 1)))))))

  ;; a list of n nodes, the last call's kept live in a global, summed:
  ;; n(n-1)/2, or a trap when the heap cannot hold it
  (func (export "list_sum") (param $n i32) (result i64)
    (local $l (ref null $node)) (local $i i32) (local $s i64)
    (block $built
      (loop $next
        (br_if $built (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $l (struct.new $node (i64.extend_i32_u (local.get $i)) (local.get $l)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (global.set $list (local.get $l))
    (block $done
      (loop $next
        (br_if $done (ref.is_null (local.get $l)))
        (local.set $s (i64.add (local.get $s) (struct.get $node 0 (local.get $l))))
        (local.set $l (struct.get $node 1 (local.get $l)))
        (br $next)))
    (local.get $s))

  ;; copies and a fill whose lengths are constants, done inline: one past
  ;; the end of memory by either range traps before it writes a byte
  (func (export "copy72") (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 72)))
  (func (export "copy12") (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 12)))
  (func (export "fill40") (param i32) (memory.fill (local.get 0) (i32.const 0xab) (i32.const 40)))

  ;; v128 cannot cross a C call: this one gets no symbol, with a warning
  (func (export "splat") (param i32) (result v128) (i32x4.splat (local.get 0)))

  ;; a name C cannot spell, and one that is a C keyword
  (func (export "add-one") (param i32) (result i32) (i32.add (local.get 0) (i32.const 1)))
  (func (export "int") (result i32) (i32.const 42)))
