;; A host call the adapter has no meaning for stops the program with exit
;; status 69 and its name on stderr, after what was printed before it.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "teavmJso" "callMethod2" (func $callMethod2 (param externref externref externref externref) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (call $putchar (i32.const 111)) (call $putchar (i32.const 107))
    (drop (call $callMethod2 (local.get $args) (ref.null extern) (ref.null extern) (ref.null extern)))
    (call $putchar (i32.const 33))))
