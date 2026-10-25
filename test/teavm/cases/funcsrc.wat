;; A function made from JavaScript source is the identity when its body is
;; `return ARG;` for its own argument, and anything else when called stops
;; the program: here `x => 1`, after `y => y` and `x => x` worked.
(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "wasm:js-string" "charCodeAt" (func $charCodeAt (param externref i32) (result i32)))
  (import "teavmJso" "createFunction1" (func $createFunction1 (param externref externref) (result (ref extern))))
  (import "teavmJso" "callFunction1" (func $callFunction1 (param externref externref) (result externref)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (data (i32.const 0) "xreturn x;return 1;yreturn y;return yy;")
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":40, \"shared\": false}")
  (func $mk (param $p i32) (param $n i32) (result (ref extern))
    (local $a (ref $chars)) (local $i i32)
    (local.set $a (array.new_default $chars (local.get $n)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (array.set $chars (local.get $a) (local.get $i)
          (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $fromArray (local.get $a) (i32.const 0) (local.get $n)))
  (func $Main::main_$caller$exported$0 (param $args externref)
    ;; y => y on "x": prints x
    (call $putchar (call $charCodeAt (call $callFunction1
      (call $createFunction1 (call $mk (i32.const 19) (i32.const 1)) (call $mk (i32.const 20) (i32.const 9)))
      (call $mk (i32.const 0) (i32.const 1))) (i32.const 0)))
    ;; x => x on "y": prints y
    (call $putchar (call $charCodeAt (call $callFunction1
      (call $createFunction1 (call $mk (i32.const 0) (i32.const 1)) (call $mk (i32.const 1) (i32.const 9)))
      (call $mk (i32.const 19) (i32.const 1))) (i32.const 0)))
    ;; y => return yy: not the identity, though it starts as one
    (drop (call $createFunction1 (call $mk (i32.const 19) (i32.const 1)) (call $mk (i32.const 29) (i32.const 10))))
    ;; x => 1
    (drop (call $callFunction1
      (call $createFunction1 (call $mk (i32.const 0) (i32.const 1)) (call $mk (i32.const 10) (i32.const 9)))
      (call $mk (i32.const 0) (i32.const 1))))
    (call $putchar (i32.const 33))))
