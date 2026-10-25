;; Math.random seeded from random_get: test/run_teavm.py runs this twice and
;; wants the two first draws, printed as bits, to differ.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "teavmMath" "random" (func $random (result f64)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (local $bits i64) (local $i i32)
    (local.set $bits (i64.reinterpret_f64 (call $random)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 16)))
        (call $putchar (i32.add (i32.const 97)
          (i32.wrap_i64 (i64.and (i64.shr_u (local.get $bits) (i64.extend_i32_u (i32.shl (local.get $i) (i32.const 2))))
                                 (i64.const 15)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $putchar (i32.const 10))))
