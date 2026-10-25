;; JSO asks for a property of main's argument array other than its length:
;; the adapter names it and stops.
(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "teavmJso" "getProperty" (func $getProperty (param externref externref) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":0, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (drop (call $getProperty (local.get $args)
      (call $fromArray (array.new_fixed $chars 6 (i32.const 108) (i32.const 101) (i32.const 110) (i32.const 103)
                                                 (i32.const 116) (i32.const 104))
                       (i32.const 0) (i32.const 5))))))
