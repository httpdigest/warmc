;; The main class need not be Main: test/run_teavm.py adapts this one with
;; App, and with Main, which it has no entry point for.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $App::main_$caller$exported$0 (param $args externref)
    (call $putchar (i32.const 65)) (call $putchar (i32.const 10))))
