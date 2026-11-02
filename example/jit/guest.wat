;; The module every host in example/jit/ compiles and runs through libwarmc:
;; a host function it calls with a string in its memory, a loop, a division
;; that traps when it divides by zero, and a load that traps past the end of
;; its memory -- a fault on the guard pages beyond it, which the runtime's
;; signal handler makes the trap, beside the host language's own.
(module
  ;; the host's: prints len bytes of memory from ptr
  (import "host" "log" (func $log (param i32 i32)))

  (memory (export "memory") 1)
  (data (i32.const 16) "Hello from wasm!")

  ;; calls the host with its greeting
  (func (export "greet")
    (call $log (i32.const 16) (i32.const 16)))

  ;; the n-th Fibonacci number
  (func (export "fib") (param $n i64) (result i64)
    (local $a i64) (local $b i64) (local $t i64)
    (local.set $b (i64.const 1))
    (block $done
      (loop $next
        (br_if $done (i64.eqz (local.get $n)))
        (local.set $t (i64.add (local.get $a) (local.get $b)))
        (local.set $a (local.get $b))
        (local.set $b (local.get $t))
        (local.set $n (i64.sub (local.get $n) (i64.const 1)))
        (br $next)))
    (local.get $a))

  ;; traps on a zero divisor
  (func (export "div") (param i32 i32) (result i32)
    (i32.div_s (local.get 0) (local.get 1)))

  ;; the byte at an address, which traps past the memory's one page
  (func (export "peek") (param i32) (result i32)
    (i32.load8_u (local.get 0))))
