;; The graph, arranged so that resolving it wrongly gives a wrong answer.
;;
;; The rest of test/components is about *decoding* -- structure that a dump can
;; be compared against.  This one is compiled and run, so every part of it is
;; chosen to be observable in a returned number:
;;
;;   - one core module instantiated twice, whose two states must stay apart;
;;   - a consumer bound to both of them, under two module names, so a linker
;;     that resolved by name rather than by argument would bind both to one;
;;   - inline core instances that *rename* -- nothing here is imported under
;;     the name its exporter uses -- so the renaming has to be resolved away
;;     rather than carried into the program;
;;   - a memory and a global crossing the boundary, not only functions;
;;   - a nested component instantiated twice, reaching an *outer* core module
;;     by alias, which is the only way a component can be a closed definition
;;     and still share code;
;;   - and an exported instance, which is how every real toolchain states an
;;     interface and is where the `interface#function` export names come from.
;;
;; Every export is self-contained: it does its whole sequence and returns, so
;; the answer does not depend on what was called before it and warmc's CLI and
;; `wasmtime run --invoke` can be compared one call at a time.
(component
  ;; A cell with state of its own: a mutable global and a memory.
  (core module $cell
    (memory (export "mem") 1)
    (global $n (mut i32) (i32.const 0))
    (func (export "put") (param i32)
      global.get $n local.get 0 i32.add global.set $n)
    (func (export "get") (result i32) global.get $n)
    (global (export "base") i32 (i32.const 700)))

  (core instance $c1 (instantiate $cell))
  (core instance $c2 (instantiate $cell))

  (alias core export $c1 "put" (core func $put1))
  (alias core export $c1 "get" (core func $get1))
  (alias core export $c1 "mem" (core memory $mem1))
  (alias core export $c1 "base" (core global $base1))
  (alias core export $c2 "put" (core func $put2))
  (alias core export $c2 "get" (core func $get2))

  ;; Renaming: the consumer asks for "store" and "load", which is not what
  ;; either cell calls them.
  (core instance $one
    (export "store" (func $put1))
    (export "load" (func $get1))
    (export "buf" (memory $mem1))
    (export "origin" (global $base1)))
  (core instance $two
    (export "store" (func $put2))
    (export "load" (func $get2)))

  (core module $work
    (import "a" "store" (func $sa (param i32)))
    (import "a" "load" (func $la (result i32)))
    (import "a" "buf" (memory 1))
    (import "a" "origin" (global $origin i32))
    (import "b" "store" (func $sb (param i32)))
    (import "b" "load" (func $lb (result i32)))

    ;; Two cells, two states.  Bound to one cell twice this reads 13013.
    (func (export "split") (result i32)
      (call $sa (i32.const 10))
      (call $sb (i32.const 3))
      (i32.add (i32.mul (call $la) (i32.const 1000)) (call $lb)))

    ;; The imported memory is the first cell's, and the imported global its
    ;; immutable one.
    (func (export "through-mem") (param i32) (result i32)
      (i32.store (i32.const 16) (i32.add (local.get 0) (global.get $origin)))
      (i32.load (i32.const 16))))

  (core instance $w
    (instantiate $work (with "a" (instance $one)) (with "b" (instance $two))))

  ;; A nested component, instantiated twice, reaching the cell module by an
  ;; outer alias.  Each instantiation gets a cell of its own, so the two
  ;; answers differ by what each was told to add.
  (component $step
    (alias outer 1 0 (core module $m))
    (core instance $i (instantiate $m))
    (core module $drive
      (import "c" "put" (func $put (param i32)))
      (import "c" "get" (func $get (result i32)))
      (func (export "run") (param i32) (result i32)
        (call $put (local.get 0))
        (call $put (local.get 0))
        (call $get)))
    (core instance $d (instantiate $drive (with "c" (instance $i))))
    (func (export "run") (param "n" u32) (result u32)
      (canon lift (core func $d "run"))))

  (instance $s1 (instantiate $step))
  (instance $s2 (instantiate $step))
  (alias export $s1 "run" (func $run1))
  (alias export $s2 "run" (func $run2))

  (func $split (result u32) (canon lift (core func $w "split")))
  (func $through (param "n" u32) (result u32)
    (canon lift (core func $w "through-mem")))

  ;; An exported instance, which is what a WIT interface encodes to.
  (instance $ops
    (export "split" (func $split))
    (export "through-mem" (func $through))
    (export "step-a" (func $run1))
    (export "step-b" (func $run2)))
  (export "test:link/ops" (instance $ops))

  ;; And one exported directly, so both export shapes are covered.
  (export "split-flat" (func $split))
)
