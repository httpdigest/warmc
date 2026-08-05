;; Canonical definitions, which the rest of the corpus does not have.
;;
;; test/components is otherwise WIT documents -- type space only -- and
;; wit-smith generates the same.  Nothing there has a `canon lift` or a `canon
;; lower`, so the options, the function index space and the needs-memory rule
;; were reachable only through a hand-run TinyGo build.  This is the smallest
;; component that exercises them, and it is built so both answers appear:
;; `f-flat` fits in registers either way, `f-spill` does not.
(component
  (type $iface (instance
    ;; Fits: two flat params, one flat result, no pointer anywhere.
    (export "f-flat" (func (param "a" u32) (param "b" u32) (result u64)))
    ;; Does not: a list is a pointer, and the result is three flat values
    ;; against a limit of one, so lowering it has to return through memory.
    (export "f-spill" (func (param "s" string) (result (tuple u32 u32 u32))))
    ;; Neither limit is anywhere near reached -- two flat params, no result.
    ;; It has to reach memory only because a string is a pointer, so this is
    ;; the case that tells the two halves of the rule apart.
    (export "f-ptr" (func (param "s" string)))
    ;; The mirror image: no pointer anywhere, but three flat results against a
    ;; limit of one.  Without it the spill half of the rule is shadowed --
    ;; every other case here reaches memory for the pointer first.
    (export "f-wide" (func (param "a" u32) (result (tuple u32 u32 u32))))
  ))
  (import "test:abi/iface" (instance $i (type $iface)))

  ;; The memory has to come from a module of its own.  A `canon lower` names
  ;; the memory it writes through, and the module that imports the lowered
  ;; function is the one that would otherwise own it -- so a single module is
  ;; circular, and every real toolchain splits it exactly here.
  (core module $libc
    (memory (export "mem") 1)
    (func (export "realloc") (param i32 i32 i32 i32) (result i32) i32.const 0)
  )
  (core instance $li (instantiate $libc))
  (alias core export $li "mem" (core memory $mem))
  (alias core export $li "realloc" (core func $ra))

  (core module $m
    (func $flat  (import "test:abi/iface" "f-flat") (param i32 i32) (result i64))
    (func $spill (import "test:abi/iface" "f-spill") (param i32 i32 i32))
    (func $ptr   (import "test:abi/iface" "f-ptr") (param i32 i32))
    (func $wide  (import "test:abi/iface" "f-wide") (param i32 i32))
    ;; Lifted below: the core side of a string parameter is pointer+length.
    (func (export "run") (param i32 i32) (result i32) i32.const 0)
  )

  (alias export $i "f-flat"  (func $af))
  (alias export $i "f-spill" (func $as))
  (core func $cf (canon lower (func $af)))
  (core func $cs (canon lower (func $as) (memory $mem) (realloc $ra)))
  (alias export $i "f-ptr" (func $ap))
  (core func $cp (canon lower (func $ap) (memory $mem) (realloc $ra)))
  (alias export $i "f-wide" (func $aw))
  (core func $cw (canon lower (func $aw) (memory $mem) (realloc $ra)))

  (core instance $ii (export "f-flat" (func $cf)) (export "f-spill" (func $cs))
                     (export "f-ptr" (func $cp)) (export "f-wide" (func $cw)))
  (core instance $mi (instantiate $m (with "test:abi/iface" (instance $ii))))
  (alias core export $mi "run" (core func $run))

  (type $rt (func (param "s" string) (result (tuple u32 u32 u32))))
  (func $lifted (type $rt) (canon lift (core func $run) (memory $mem) (realloc $ra)))
  (export "run" (func $lifted))
)
