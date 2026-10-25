;; TeaVM's data ends at the end of the memory's one page, so the adapter's
;; buffers and messages need a page more than TeaVM asked for.
(module
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1))
  (import "teavmMemory" "heapOffset" (global $heapOffset i32))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (tag $exn (export "teavm.javaException") (param (ref null $Object)))
  (data (i32.const 65532) "data")
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":65536, \"shared\": false}")
  (func $Main::main_$caller$exported$0 (param $args externref)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
        (call $putchar (i32.load8_u (i32.add (i32.const 65532) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $putchar (i32.add (i32.const 48) (i32.ge_u (global.get $heapOffset) (i32.const 65536))))
    (call $putchar (i32.add (i32.const 48)
      (i32.le_u (global.get $heapOffset) (i32.mul (memory.size) (i32.const 65536)))))
    (call $putchar (i32.const 10))))
