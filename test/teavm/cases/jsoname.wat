;; A JavaScript global other than `performance` names itself on stderr and
;; stops the program with exit status 69.
(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "teavmJso" "global" (func $global (param externref) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (call $putchar (i32.const 10))
    (drop (call $global (call $fromArray
      (array.new_fixed $chars 6 (i32.const 119) (i32.const 105) (i32.const 110) (i32.const 100) (i32.const 111) (i32.const 119))
      (i32.const 0) (i32.const 6))))))
