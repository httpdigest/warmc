;; The wasi:* calls no TinyGo guest ever makes, made by hand.
;;
;; A trace of every host call across the twenty-two Go programs in the two
;; suites comes back with eleven of the bound forty-three never entered.  They
;; are not dead surface -- they are what a guest with its own event loop, or
;; one that closes what it opens, would reach -- but TinyGo takes stdout once
;; and never gives it back, sleeps only by duration, and cannot make a symlink
;; at all.  So this is a guest written to call them, rather than a program that
;; happens to.
;;
;; It is a core module: warmc's text format assembles core wasm, not component
;; text, so run_wasip2.sh puts it through `wasm-tools component embed` against
;; the same WIT tree TinyGo builds against and then `component new`.  Every
;; import below is spelled in the lowered canonical ABI, which is what
;; --dump-link prints for a component that imports it.
;;
;; What is printed is one digit per check, in order, and every one must be 1.
;; The digits are: a fresh timer is not ready, it is ready after block, poll
;; returns exactly the index of the one pollable it was given, a stream
;; subscribe answers a handle, flush succeeds, and symlink-at then readlink-at
;; round-trip a target that a TinyGo guest could not have written.

(module
  (import "wasi:cli/stdout@0.2.0" "get-stdout" (func $stdout (result i32)))
  (import "wasi:cli/stdin@0.2.0" "get-stdin" (func $stdin (result i32)))
  (import "wasi:io/streams@0.2.0" "[method]output-stream.blocking-flush"
    (func $flush (param i32 i32)))
  (import "wasi:io/streams@0.2.0" "[method]output-stream.write"
    (func $swrite (param i32 i32 i32 i32)))
  (import "wasi:io/streams@0.2.0" "[method]output-stream.check-write"
    (func $checkw (param i32 i32)))
  (import "wasi:cli/terminal-stdin@0.2.0" "get-terminal-stdin"
    (func $tstdin (param i32)))
  (import "wasi:cli/terminal-stdout@0.2.0" "get-terminal-stdout"
    (func $tstdout (param i32)))
  (import "wasi:cli/terminal-stderr@0.2.0" "get-terminal-stderr"
    (func $tstderr (param i32)))
  (import "wasi:cli/terminal-input@0.2.0" "[resource-drop]terminal-input"
    (func $tidrop (param i32)))
  (import "wasi:cli/terminal-output@0.2.0" "[resource-drop]terminal-output"
    (func $todrop (param i32)))
  (import "wasi:io/streams@0.2.0" "[method]output-stream.subscribe"
    (func $osub (param i32) (result i32)))
  (import "wasi:io/streams@0.2.0" "[method]input-stream.subscribe"
    (func $isub (param i32) (result i32)))
  (import "wasi:io/streams@0.2.0" "[resource-drop]output-stream"
    (func $odrop (param i32)))
  (import "wasi:io/streams@0.2.0" "[resource-drop]input-stream"
    (func $idrop (param i32)))
  (import "wasi:io/error@0.2.0" "[resource-drop]error" (func $edrop (param i32)))
  (import "wasi:io/poll@0.2.0" "[method]pollable.ready" (func $ready (param i32) (result i32)))
  (import "wasi:io/poll@0.2.0" "[method]pollable.block" (func $block (param i32)))
  (import "wasi:io/poll@0.2.0" "poll" (func $poll (param i32 i32 i32)))
  (import "wasi:io/poll@0.2.0" "[resource-drop]pollable" (func $pdrop (param i32)))
  (import "wasi:clocks/monotonic-clock@0.2.0" "subscribe-duration"
    (func $subdur (param i64) (result i32)))
  (import "wasi:clocks/monotonic-clock@0.2.0" "subscribe-instant"
    (func $subinst (param i64) (result i32)))
  (import "wasi:clocks/monotonic-clock@0.2.0" "now" (func $now (result i64)))
  (import "wasi:filesystem/preopens@0.2.0" "get-directories" (func $preopens (param i32)))
  (import "wasi:filesystem/types@0.2.0" "[method]descriptor.symlink-at"
    (func $symlink (param i32 i32 i32 i32 i32 i32)))
  (import "wasi:filesystem/types@0.2.0" "[method]descriptor.readlink-at"
    (func $readlink (param i32 i32 i32 i32)))
  (import "wasi:filesystem/types@0.2.0" "[resource-drop]descriptor"
    (func $ddrop (param i32)))

  (memory (export "memory") 4)

  ;; The canonical ABI needs somewhere to put a list it returns.  A bump
  ;; pointer is enough: nothing here frees, and the alignment it is asked for
  ;; is never more than 8.
  (global $bump (mut i32) (i32.const 0x10000))
  (func (export "cabi_realloc")
        (param $old i32) (param $oldn i32) (param $align i32) (param $n i32)
        (result i32)
    (local $p i32)
    (global.set $bump
      (i32.and (i32.add (global.get $bump) (i32.sub (local.get $align) (i32.const 1)))
               (i32.sub (i32.const 0) (local.get $align))))
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))

  (global $out (mut i32) (i32.const 0x200))
  (func $digit (param $v i32)
    (i32.store8 (global.get $out) (i32.add (i32.const 48) (local.get $v)))
    (global.set $out (i32.add (global.get $out) (i32.const 1))))

  (data (i32.const 0x300) "probe-target")
  (data (i32.const 0x310) "probe-link")

  (func (export "wasi:cli/run@0.2.0#run") (result i32)
    (local $so i32) (local $si i32) (local $p i32) (local $q i32)
    (local $d i32) (local $n i32)

    (local.set $so (call $stdout))
    (local.set $si (call $stdin))

    ;; A timer far enough out that it cannot already have expired, then the
    ;; same one after blocking on it.
    (local.set $p (call $subdur (i64.const 30000000)))
    (call $digit (i32.eqz (call $ready (local.get $p))))
    (call $block (local.get $p))
    (call $digit (call $ready (local.get $p)))

    ;; poll over a list of one, which must answer that one.
    (i32.store (i32.const 0x400) (local.get $p))
    (call $poll (i32.const 0x400) (i32.const 1) (i32.const 0x100))
    (call $digit (i32.eq (i32.load (i32.const 0x104)) (i32.const 1)))
    (call $digit (i32.eqz (i32.load (i32.load (i32.const 0x100)))))
    (call $pdrop (local.get $p))

    ;; An instant that has already passed.  Whether it is *ready* before
    ;; anything has waited on it is not a question the two engines answer the
    ;; same way -- wasmtime's timer reports itself unready until its reactor
    ;; has advanced it once, which is an artifact of how it is built rather
    ;; than something wasi:clocks says.  Blocking first asks the same code the
    ;; same question in the one form both agree on: a passed instant must not
    ;; wait.
    (local.set $q (call $subinst (call $now)))
    (call $block (local.get $q))
    (call $digit (call $ready (local.get $q)))
    (call $pdrop (local.get $q))

    ;; Subscribing to a stream answers a pollable, and stdout is writable.
    (local.set $p (call $osub (local.get $so)))
    (call $digit (call $ready (local.get $p)))
    (call $pdrop (local.get $p))
    (local.set $p (call $isub (local.get $si)))
    (call $pdrop (local.get $p))

    ;; Flush reports ok in the discriminant of its result.
    (call $flush (local.get $so) (i32.const 0x100))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x100))))

    ;; check-write is the other half of the writing path: rustc's std goes
    ;; through check-write/write/flush and never calls
    ;; blocking-write-and-flush, so a host that only implements the blocking
    ;; one answers hello-world from tinygo and nothing at all from rustc.  The
    ;; permit is a u64 at offset 8 of the result, and has to be more than zero
    ;; or a guest that respects it can never write.
    (call $checkw (local.get $so) (i32.const 0x100))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x100))))
    (call $digit (i64.gt_u (i64.load (i32.const 0x108)) (i64.const 0)))

    ;; Whether the three standard descriptors are terminals.  Under the
    ;; harness they are files, so all three answer none and the handles are
    ;; never made -- which is why the two [resource-drop]terminal calls below
    ;; are reached only when this is run on a tty, and are imported here so
    ;; that the coverage check counts them as asked for.
    (call $tstdin (i32.const 0x100))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x100))))
    (if (i32.load8_u (i32.const 0x100))
      (then (call $tidrop (i32.load (i32.const 0x104)))))
    (call $tstderr (i32.const 0x100))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x100))))
    (if (i32.load8_u (i32.const 0x100))
      (then (call $todrop (i32.load (i32.const 0x104)))))
    (call $tstdout (i32.const 0x100))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x100))))
    (if (i32.load8_u (i32.const 0x100))
      (then (call $todrop (i32.load (i32.const 0x104)))))

    ;; symlink-at then readlink-at, against the first preopen.
    (call $preopens (i32.const 0x100))
    (local.set $n (i32.load (i32.const 0x104)))
    (call $digit (i32.gt_u (local.get $n) (i32.const 0)))
    (local.set $d (i32.load (i32.load (i32.const 0x100))))
    (call $symlink (local.get $d) (i32.const 0x300) (i32.const 12)
                   (i32.const 0x310) (i32.const 10) (i32.const 0x120))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x120))))
    (call $readlink (local.get $d) (i32.const 0x310) (i32.const 10)
                    (i32.const 0x140))
    (call $digit (i32.eqz (i32.load8_u (i32.const 0x140))))
    (call $digit (i32.eq (i32.load (i32.const 0x148)) (i32.const 12)))
    (call $digit (i32.eq (i32.load8_u (i32.load (i32.const 0x144)))
                         (i32.const 112)))
    (call $ddrop (local.get $d))

    ;; Dropping the streams last, so a drop that closed the wrong descriptor
    ;; still has the write below to give it away.
    (i32.store8 (global.get $out) (i32.const 10))
    (global.set $out (i32.add (global.get $out) (i32.const 1)))
    (i32.store (i32.const 0x180) (i32.const 0x200))
    (i32.store (i32.const 0x184)
               (i32.sub (global.get $out) (i32.const 0x200)))
    ;; Out through write and flush rather than blocking-write-and-flush, which
    ;; the Go cases cover and this one is here to complement: these are the
    ;; two calls a Rust guest's every line of output goes through.
    (call $swrite (local.get $so) (i32.const 0x200)
                  (i32.sub (global.get $out) (i32.const 0x200))
                  (i32.const 0x100))
    (call $flush (local.get $so) (i32.const 0x120))
    (call $idrop (local.get $si))
    (call $odrop (local.get $so))
    (i32.const 0))
)
