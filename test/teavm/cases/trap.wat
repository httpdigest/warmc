;; A line is written when it ends, as console.log writes it, so a trap after
;; it leaves it on stdout; what follows on the same line is lost with the trap.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (call $putchar (i32.const 111)) (call $putchar (i32.const 107)) (call $putchar (i32.const 10))
    (call $putchar (i32.const 33))
    (unreachable)))
