;; An exception with no message, and a module whose exceptionMessage is
;; missing: the line has no message, and the exit status is still 1.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (call $putchar (i32.const 10))
    (throw $exn (ref.null $Object))))
