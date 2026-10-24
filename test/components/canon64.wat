;; A canonical definition whose memory option names a 64-bit memory: the
;; canonical ABI here is 32-bit, so the component is refused when it is
;; compiled, saying so -- where it once compiled into a program that failed
;; to start, its adapter importing a 32-bit memory.
(component
  (component $provider
    (core module $m
      (memory (export "mem") i64 1)
      (global $bump (mut i64) (i64.const 1024))
      (func (export "realloc") (param i64 i64 i64 i64) (result i64)
        global.get $bump
        global.get $bump local.get 3 i64.add global.set $bump)
      (func (export "len") (param $p i64) (param $n i64) (result i32)
        (i32.add (i32.wrap_i64 (local.get $n)) (i32.load8_u (local.get $p)))))
    (core instance $i (instantiate $m))
    (func $len (param "s" string) (result u32)
      (canon lift (core func $i "len") (memory (core memory $i "mem")) (realloc (core func $i "realloc"))))
    (instance $iface (export "len" (func $len)))
    (export "t:t/ops" (instance $iface)))
  (component $consumer
    (import "t:t/ops" (instance $g (export "len" (func (param "s" string) (result u32)))))
    (alias export $g "len" (func $len))
    (core module $mem
      (memory (export "mem") 1)
      (data (i32.const 16) "warmc")
      (global $bump (mut i32) (i32.const 1000))
      (func (export "realloc") (param i32 i32 i32 i32) (result i32)
        global.get $bump
        global.get $bump local.get 3 i32.add global.set $bump))
    (core instance $mi (instantiate $mem))
    (core func $llen (canon lower (func $len) (memory (core memory $mi "mem")) (realloc (core func $mi "realloc"))))
    (core module $c
      (import "" "len" (func $len (param i32 i32) (result i32)))
      (func (export "run") (result i32) (call $len (i32.const 16) (i32.const 5))))
    (core instance $ci (instantiate $c (with "" (instance (export "len" (func $llen))))))
    (func (export "run") (result u32) (canon lift (core func $ci "run"))))
  (instance $p (instantiate $provider))
  (instance $c (instantiate $consumer (with "t:t/ops" (instance $p "t:t/ops"))))
  (export "run" (func $c "run")))
