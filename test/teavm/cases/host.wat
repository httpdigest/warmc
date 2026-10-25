;; Every host call test/teavm/wasi.py defines, called and printed, in a module
;; shaped as TeaVM's wasm-gc target writes one: a memory and the heap's start
;; imported from the loader, the teavm.memoryRequirements section, the
;; exception tag exported as teavm.javaException, and main as
;; Main::main_$caller$exported$0.  Most of these no Java program reaches on
;; its own -- the string builtins serve JSO's conversions, the numbers its
;; wrappers -- so the corpora cannot show them right.  host.out is what it
;; prints; test/run_teavm.py runs it under wasmtime and warmc.
(module
  (type $chars (array (mut i16)))
  (type $Object (sub (struct (field (mut i32)))))
  (import "env" "memory" (memory 1 256))
  (import "teavmMemory" "heapOffset" (global $heapOffset i32))
  (import "teavmMemory" "maxSize" (global $maxSize i32))
  (import "teavmMemory" "notifyHeapResized" (func $resized))
  (import "teavmConsole" "putcharStdout" (func $putchar (param i32)))
  (import "teavmConsole" "putcharStderr" (func $putcharErr (param i32)))
  (import "wasm:js-string" "fromCharCode" (func $fromCharCode (param i32) (result (ref extern))))
  (import "wasm:js-string" "fromCodePoint" (func $fromCodePoint (param i32) (result (ref extern))))
  (import "wasm:js-string" "fromCharCodeArray" (func $fromArray (param (ref null $chars) i32 i32) (result (ref extern))))
  (import "wasm:js-string" "intoCharCodeArray" (func $intoArray (param externref (ref null $chars) i32) (result i32)))
  (import "wasm:js-string" "charCodeAt" (func $charCodeAt (param externref i32) (result i32)))
  (import "wasm:js-string" "codePointAt" (func $codePointAt (param externref i32) (result i32)))
  (import "wasm:js-string" "length" (func $length (param externref) (result i32)))
  (import "wasm:js-string" "concat" (func $concat (param externref externref) (result (ref extern))))
  (import "wasm:js-string" "substring" (func $substring (param externref i32 i32) (result (ref extern))))
  (import "wasm:js-string" "equals" (func $equals (param externref externref) (result i32)))
  (import "wasm:js-string" "compare" (func $compare (param externref externref) (result i32)))
  (import "wasm:js-string" "test" (func $test (param externref) (result i32)))
  (import "wasm:js-string" "cast" (func $cast (param externref) (result (ref extern))))
  (import "teavmJso" "stringBuiltinsSupported" (func $builtins (result i32)))
  (import "teavmJso" "isUndefined" (func $isUndefined (param externref) (result i32)))
  (import "teavmJso" "createClass" (func $createClass (param externref externref funcref) (result externref)))
  (import "teavmJso" "defineFunction" (func $defineFunction (param funcref i32) (result externref)))
  (import "teavmJso" "defineStaticMethod" (func $defineStaticMethod (param externref externref funcref i32)))
  (import "teavmJso" "createFunction1" (func $createFunction1 (param externref externref) (result (ref extern))))
  (import "teavmJso" "callFunction1" (func $callFunction1 (param externref externref) (result externref)))
  (import "teavmJso" "global" (func $global (param externref) (result externref)))
  (import "teavmJso" "callMethod0" (func $callMethod0 (param externref externref) (result externref)))
  (import "teavmJso" "getProperty" (func $getProperty (param externref externref) (result externref)))
  (import "teavmJso" "wrapInt" (func $wrapInt (param i32) (result externref)))
  (import "teavmJso" "unwrapInt" (func $unwrapInt (param externref) (result i32)))
  (import "teavmJso" "wrapDouble" (func $wrapDouble (param f64) (result externref)))
  (import "teavmJso" "unwrapDouble" (func $unwrapDouble (param externref) (result f64)))
  (import "teavmJso" "unwrapBoolean" (func $unwrapBoolean (param externref) (result i32)))
  (import "teavmDate" "currentTimeMillis" (func $millis (result f64)))
  (import "teavmMath" "sqrt" (func $sqrt (param f64) (result f64)))
  (import "teavmMath" "floor" (func $floor (param f64) (result f64)))
  (import "teavmMath" "ceil" (func $ceil (param f64) (result f64)))
  (import "teavmMath" "fabs" (func $fabs (param f64) (result f64)))
  (import "teavmMath" "random" (func $random (result f64)))
  (import "teavmMath" "sin" (func $sin (param f64) (result f64)))
  (import "teavm" "isnan" (func $isnan (param f64) (result i32)))
  (import "teavm" "isfinite" (func $isfinite (param f64) (result i32)))
  (import "teavm" "takeStackTrace" (func $takeStackTrace (param externref) (result externref)))
  (import "teavm" "decorateException" (func $decorate (param (ref null $Object))))
  (import "teavm" "createWeakRef" (func $weak (param (ref null $Object) externref externref) (result externref)))
  (import "teavm" "deref" (func $deref (param externref) (result (ref null $Object))))

  (tag $exn (export "teavm.javaException") (param (ref null $Object)))

  ;; TeaVM's data; the adapter's own goes past dataSize.
  (data (i32.const 0) "abcdef xyz length performance now return str; return 1; ")
  (@custom "teavm.memoryRequirements" "{\"min\":1, \"dataSize\":56, \"shared\": false}")
  (@custom "teavm.imports" "[{\"module\":\"teavmMemory\",\"name\":\"heapOffset\"}]")

  ;; Math.sin, as TeaVM compiles test/teavm/FdLibm.java: the adapter calls this
  ;; for teavmMath.sin and removes the export.
  (func $fdlibm_sin (export "warmc.fdlibm.sin") (param f64) (result f64)
    (f64.add (local.get 0) (f64.const 42)))

  (func $lit (param $p i32) (param $n i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (call $putchar (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next))))

  (func $js (param $s externref)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (call $length (local.get $s))))
        (call $putchar (call $charCodeAt (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next))))

  (func $int (param $v i32)
    (local $d i32)
    (if (i32.lt_s (local.get $v) (i32.const 0))
      (then (call $putchar (i32.const 45)) (local.set $v (i32.sub (i32.const 0) (local.get $v)))))
    (if (i32.ge_u (local.get $v) (i32.const 10))
      (then (call $int (i32.div_u (local.get $v) (i32.const 10)))))
    (call $putchar (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10)))))

  (func $sp (call $putchar (i32.const 32)))
  (func $nl (call $putchar (i32.const 10)))
  (func $label (param $c0 i32) (param $c1 i32)
    (call $putchar (local.get $c0)) (call $putchar (local.get $c1)) (call $putchar (i32.const 58)) (call $sp))

  ;; A JS string from n bytes of data, through fromCharCodeArray.
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

  (func $bracket (param $s externref)
    (call $putchar (i32.const 91)) (call $js (local.get $s)) (call $putchar (i32.const 93)))

  ;; What the loader alone calls, which the adapter does not export: a
  ;; function of TeaVM's own, the `main` JSO makes a JavaScript function of,
  ;; and a function main takes a reference to, which the export declared.
  ;; The program's own export stays.
  (func $release (export "teavm.free") (param i32))
  (global $mainfn (export "main") (mut externref) (ref.null extern))
  (func $wrap (export "teavm.js.wrapException") (param (ref null $Object)) (result externref)
    (ref.null extern))
  (func (export "app_answer") (result i32) (i32.const 42))

  (func $Main::main_$caller$exported$0 (param $args externref)
    (local $abc externref) (local $def externref) (local $all externref)
    (local $a (ref null $chars)) (local $o (ref null $Object)) (local $i i32)
    (local $t f64) (local $u f64) (local $ok i32)

    ;; js-string
    (drop (ref.func $wrap))
    (local.set $abc (call $mk (i32.const 0) (i32.const 3)))
    (local.set $def (call $mk (i32.const 3) (i32.const 3)))
    (local.set $all (call $concat (local.get $abc) (local.get $def)))
    (call $label (i32.const 115) (i32.const 49))           ;; s1: builtins, concat, length
    (call $int (call $builtins)) (call $sp)
    (call $js (local.get $all)) (call $sp)
    (call $int (call $length (local.get $all))) (call $sp)
    (call $int (call $length (call $substring (local.get $all) (i32.const 2) (i32.const 2))))
    (call $nl)
    (call $label (i32.const 115) (i32.const 50))           ;; s2: substring's clamping
    (call $bracket (call $substring (local.get $all) (i32.const 1) (i32.const 4)))
    (call $bracket (call $substring (local.get $all) (i32.const 4) (i32.const 1)))
    (call $bracket (call $substring (local.get $all) (i32.const 4) (i32.const 100)))
    (call $bracket (call $substring (local.get $all) (i32.const 7) (i32.const 9)))
    (call $bracket (call $substring (local.get $all) (i32.const -1) (i32.const 3)))
    (call $bracket (call $substring (local.get $all) (i32.const 0) (i32.const -1)))
    (call $bracket (call $substring (local.get $all) (i32.const 6) (i32.const 6)))
    (call $nl)
    (call $label (i32.const 115) (i32.const 51))           ;; s3: char codes and code points
    (call $int (call $charCodeAt (local.get $all) (i32.const 0))) (call $sp)
    (call $int (call $charCodeAt (call $fromCharCode (i32.const 0x1e9)) (i32.const 0))) (call $sp)
    (call $int (call $charCodeAt (call $fromCharCode (i32.const 0x10041)) (i32.const 0))) (call $sp)
    (call $int (call $length (call $fromCodePoint (i32.const 0x1f600)))) (call $sp)
    (call $int (call $codePointAt (call $fromCodePoint (i32.const 0x1f600)) (i32.const 0))) (call $sp)
    (call $int (call $codePointAt (call $fromCodePoint (i32.const 0x1f600)) (i32.const 1))) (call $sp)
    (call $int (call $codePointAt (call $concat (call $fromCharCode (i32.const 0xd83d)) (local.get $abc)) (i32.const 0))) (call $sp)
    (call $int (call $length (call $fromCodePoint (i32.const 0xdc00)))) (call $sp)
    (call $js (call $fromCodePoint (i32.const 0x1f600))) (call $js (call $fromCodePoint (i32.const 0xe9)))
    (call $nl)
    (call $label (i32.const 115) (i32.const 52))           ;; s4: compare, equals, test, cast
    (call $int (call $compare (local.get $abc) (local.get $def))) (call $sp)
    (call $int (call $compare (local.get $abc) (call $mk (i32.const 0) (i32.const 3)))) (call $sp)
    (call $int (call $compare (local.get $all) (local.get $abc))) (call $sp)
    (call $int (call $compare (call $fromCharCode (i32.const 0xffff)) (call $fromCodePoint (i32.const 0x10000)))) (call $sp)
    (call $int (call $compare (local.get $abc) (local.get $all))) (call $sp)
    (call $int (call $equals (local.get $abc) (call $mk (i32.const 0) (i32.const 3)))) (call $sp)
    (call $int (call $equals (local.get $abc) (local.get $def))) (call $sp)
    (call $int (call $equals (ref.null extern) (ref.null extern))) (call $sp)
    (call $int (call $equals (local.get $abc) (ref.null extern))) (call $sp)
    (call $int (call $equals (ref.null extern) (local.get $abc))) (call $sp)
    (call $int (call $test (local.get $abc))) (call $sp)
    (call $int (call $test (ref.null extern))) (call $sp)
    (call $int (call $test (call $wrapInt (i32.const 3)))) (call $sp)
    (call $js (call $cast (local.get $def)))
    (call $nl)
    (call $label (i32.const 115) (i32.const 53))           ;; s5: char arrays
    (local.set $a (array.new_default $chars (i32.const 8)))
    (call $int (call $intoArray (call $mk (i32.const 7) (i32.const 3)) (local.get $a) (i32.const 5))) (call $sp)
    (call $bracket (call $fromArray (local.get $a) (i32.const 5) (i32.const 8)))
    (call $bracket (call $fromArray (local.get $a) (i32.const 6) (i32.const 6)))
    (call $int (array.get_u $chars (local.get $a) (i32.const 0)))
    (call $nl)

    ;; JSO
    (call $label (i32.const 106) (i32.const 49))           ;; j1: main's arguments, undefined
    (call $int (call $unwrapInt (call $getProperty (local.get $args) (call $mk (i32.const 11) (i32.const 6))))) (call $sp)
    (call $int (call $isUndefined (local.get $args))) (call $sp)
    (call $int (call $isUndefined (ref.null extern)))
    (call $nl)
    (call $label (i32.const 106) (i32.const 50))           ;; j2: numbers
    (call $int (call $unwrapInt (call $wrapInt (i32.const -7)))) (call $sp)
    (call $int (i32.trunc_f64_s (f64.mul (call $unwrapDouble (call $wrapDouble (f64.const 2.75))) (f64.const 100)))) (call $sp)
    (call $int (call $unwrapInt (call $wrapDouble (f64.const 1e30)))) (call $sp)
    (call $int (call $unwrapInt (ref.null extern))) (call $sp)
    (call $int (call $isnan (call $unwrapDouble (ref.null extern)))) (call $sp)
    (call $int (call $unwrapBoolean (call $wrapInt (i32.const 0)))) (call $sp)
    (call $int (call $unwrapBoolean (call $wrapInt (i32.const 5))))
    (call $nl)
    (call $label (i32.const 106) (i32.const 51))           ;; j3: functions from source
    (call $js (call $callFunction1
      (call $createFunction1 (call $mk (i32.const 41) (i32.const 3)) (call $mk (i32.const 34) (i32.const 11)))
      (local.get $def)))
    (drop (call $createClass (local.get $abc) (ref.null extern) (ref.null func)))
    (drop (call $defineFunction (ref.func $fdlibm_sin) (i32.const 0)))
    (call $defineStaticMethod (local.get $abc) (local.get $def) (ref.null func) (i32.const 0))
    (call $nl)
    (call $label (i32.const 106) (i32.const 52))           ;; j4: performance.now
    (local.set $t (call $unwrapDouble (call $callMethod0
      (call $global (call $mk (i32.const 18) (i32.const 11))) (call $mk (i32.const 30) (i32.const 3)))))
    (local.set $u (call $unwrapDouble (call $callMethod0
      (call $global (call $mk (i32.const 18) (i32.const 11))) (call $mk (i32.const 30) (i32.const 3)))))
    (call $int (f64.ge (local.get $u) (local.get $t))) (call $sp)
    (call $int (f64.ge (local.get $t) (f64.const 0))) (call $sp)
    (call $int (f64.lt (local.get $u) (f64.const 60000)))
    (call $nl)

    ;; the rest
    (call $label (i32.const 109) (i32.const 49))           ;; m1: math
    (call $int (i32.trunc_f64_s (f64.mul (call $sqrt (f64.const 2)) (f64.const 1e6)))) (call $sp)
    (call $int (i32.trunc_f64_s (call $floor (f64.const -2.5)))) (call $sp)
    (call $int (i32.trunc_f64_s (call $ceil (f64.const -2.5)))) (call $sp)
    (call $int (i32.trunc_f64_s (call $fabs (f64.const -3.5)))) (call $sp)
    (call $int (i32.trunc_f64_s (call $sin (f64.const 1)))) (call $sp)
    (call $int (call $isnan (f64.const nan))) (call $int (call $isnan (f64.const 1)))
    (call $int (call $isfinite (f64.const inf))) (call $int (call $isfinite (f64.const -1e308)))
    (call $sp)
    (local.set $ok (i32.const 1))
    (local.set $u (f64.const -1))
    (local.set $i (i32.const 0))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 1000)))
        (local.set $t (call $random))
        (if (i32.or (f64.lt (local.get $t) (f64.const 0)) (f64.ge (local.get $t) (f64.const 1)))
          (then (local.set $ok (i32.const 0))))
        (if (f64.eq (local.get $t) (local.get $u)) (then (local.set $ok (i32.const 0))))
        (local.set $u (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $int (local.get $ok)) (call $sp)
    ;; about half of them past one half
    (local.set $ok (i32.const 0))
    (local.set $i (i32.const 0))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 4000)))
        (if (f64.ge (call $random) (f64.const 0.5)) (then (local.set $ok (i32.add (local.get $ok) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $int (i32.and (i32.gt_u (local.get $ok) (i32.const 1700)) (i32.lt_u (local.get $ok) (i32.const 2300))))
    (call $nl)
    (call $label (i32.const 109) (i32.const 50))           ;; m2: clock, heap, references
    (call $int (f64.gt (call $millis) (f64.const 1.7e12))) (call $sp)
    (local.set $t (call $millis))
    (call $int (f64.eq (local.get $t) (f64.floor (local.get $t)))) (call $sp)
    (call $int (i32.ge_u (global.get $heapOffset) (i32.const 56))) (call $sp)
    (call $int (i32.eqz (i32.and (global.get $heapOffset) (i32.const 7)))) (call $sp)
    (call $int (i32.le_u (global.get $heapOffset) (i32.mul (memory.size) (i32.const 65536)))) (call $sp)
    (call $int (global.get $maxSize)) (call $sp)
    (call $resized)
    (local.set $o (struct.new $Object (i32.const 5)))
    (call $int (ref.eq (local.get $o) (call $deref (call $weak (local.get $o) (ref.null extern) (ref.null extern))))) (call $sp)
    (call $int (ref.is_null (call $takeStackTrace (ref.null extern))))
    (call $decorate (local.get $o))
    (call $nl)

    ;; the console: UTF-8, surrogates, a line past the buffer, stderr
    (call $label (i32.const 99) (i32.const 49))            ;; c1
    (call $putchar (i32.const 0xe9)) (call $putchar (i32.const 0x20ac))
    (call $putchar (i32.const 0xd83d)) (call $putchar (i32.const 0xde00))
    (call $putchar (i32.const 0xd83d)) (call $putchar (i32.const 120))
    (call $putchar (i32.const 0xde00)) (call $putchar (i32.const 121))
    (call $putchar (i32.const 0x1fffe)) (call $putchar (i32.const 0xd800))
    (call $nl)
    (call $label (i32.const 99) (i32.const 51))            ;; c3: each width's ends
    (call $putchar (i32.const 0x7f)) (call $putchar (i32.const 0x80))
    (call $putchar (i32.const 0x7ff)) (call $putchar (i32.const 0x800))
    (call $putchar (i32.const 0xd7ff)) (call $putchar (i32.const 0xe000))
    (call $putchar (i32.const 0xdbff)) (call $putchar (i32.const 0xdfff))
    (call $putchar (i32.const 0xd800)) (call $putchar (i32.const 0xdc00))
    (call $putchar (i32.const 0xffff))
    (call $nl)
    (call $putcharErr (i32.const 69)) (call $putcharErr (i32.const 10))
    (call $label (i32.const 99) (i32.const 50))            ;; c2: 9000 x then '.'
    (local.set $i (i32.const 0))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 9000)))
        (call $putchar (i32.const 120))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    ;; TeaVM's heap starts at heapOffset: writing it all over must leave the
    ;; adapter's buffered line alone
    (memory.fill (global.get $heapOffset) (i32.const 90)
      (i32.sub (i32.mul (memory.size) (i32.const 65536)) (global.get $heapOffset)))
    (call $putchar (i32.const 46))
    (call $nl)
    ;; a last line with no newline, ending in a high surrogate
    (call $lit (i32.const 0) (i32.const 3))
    (call $putchar (i32.const 0xd83d))))
