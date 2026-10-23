;; call_indirect through the per-type code tables of a frozen table 0
;; (src/module_data.c, codegen_plan_icall_tables): the table here is the
;; module's only one and nothing writes it, so each call type gets a table of
;; code pointers, its slots the entries of that type and trap stubs between
;; them, and a call is a compare, a load and the branch with the caller's own
;; vmctx for the callee's.  Three types share the table, interleaved, the
;; way a Rust or TinyGo program's trait methods and closures do; $un2 is
;; $un written out again, which must find $un's table; $wide's entries sit
;; past a hundred empty slots, so its window starts there and the index is
;; taken less 100.  The answers are wasm-interp's.
;;
;; The shapes: calls in a loop, where the table's address is held in the
;; pinned register the function's hot type takes, next to a call of another
;; type in the same loop, which builds its address; a call out of any loop;
;; a tail call through the table; and an index from memory, so that nothing
;; but the table says what it calls.  test/cases_traps/ictraps.wat has the
;; slots that trap and the windows that do not cover the whole table.
(module
  (type $un (func (param i32) (result i32)))
  (type $bin (func (param i32 i32) (result i32)))
  (type $un2 (func (param i32) (result i32)))
  (type $wide (func (param i64 i32) (result i64)))
  (memory 1)
  (table 200 funcref)
  ;; slot 0 empty, as LLVM leaves it
  (elem (i32.const 1) func $inc $add $dbl $sub $neg $mul $sq $xor $shl)
  (elem (i32.const 11) func $chain)
  (elem (i32.const 100) func $wmul $inc $add $wneg)
  ;; the indices the loops walk, from memory: $un ones, then $bin ones, then
  ;; $chain's
  (data (i32.const 0) "\01\03\05\07\01\07\03\05")
  (data (i32.const 16) "\02\04\06\08\09\02\06\04")
  (data (i32.const 32) "\0b")

  (func $inc (type $un) (i32.add (local.get 0) (i32.const 1)))
  (func $add (type $bin) (i32.add (local.get 0) (local.get 1)))
  (func $dbl (type $un) (i32.shl (local.get 0) (i32.const 1)))
  (func $sub (type $bin) (i32.sub (local.get 0) (local.get 1)))
  (func $neg (type $un) (i32.sub (i32.const 0) (local.get 0)))
  (func $mul (type $bin) (i32.mul (local.get 0) (local.get 1)))
  (func $sq (type $un) (i32.mul (local.get 0) (local.get 0)))
  (func $xor (type $bin) (i32.xor (local.get 0) (local.get 1)))
  (func $shl (type $bin) (i32.shl (local.get 0) (i32.and (local.get 1) (i32.const 7))))
  (func $wmul (type $wide) (i64.mul (local.get 0) (i64.extend_i32_s (local.get 1))))
  (func $wneg (type $wide) (i64.sub (i64.extend_i32_u (local.get 1)) (local.get 0)))

  ;; the hot type in a loop: its table's address stays in a register
  (func $walk_un (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $s
        (call_indirect (type $un)
          (i32.add (local.get $s) (local.get $i))
          (i32.load8_u (i32.and (local.get $i) (i32.const 7)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))
  (func (export "walk_un") (result i32) (call $walk_un (i32.const 1000)))

  ;; two types in one loop: one of them has the register, the other builds
  ;; its table's address at the call
  (func $walk_mixed (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $done (loop $l
      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $s
        (call_indirect (type $bin)
          (local.get $s)
          (call_indirect (type $un2) (local.get $i)
            (i32.load8_u (i32.and (local.get $i) (i32.const 7))))
          (i32.load8_u offset=16 (i32.and (local.get $i) (i32.const 7)))))
      (local.set $s
        (call_indirect (type $bin) (local.get $s) (local.get $i)
          (i32.load8_u offset=16 (i32.and (i32.add (local.get $i) (i32.const 3))
                                          (i32.const 7)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))
  (func (export "walk_mixed") (result i32) (call $walk_mixed (i32.const 777)))

  ;; out of any loop, every slot of each type once
  (func $pick_un (param $k i32) (param $x i32) (result i32)
    (call_indirect (type $un) (local.get $x) (local.get $k)))
  (func $pick_bin (param $k i32) (param $x i32) (param $y i32) (result i32)
    (call_indirect (type $bin) (local.get $x) (local.get $y) (local.get $k)))
  (func (export "pick_un1") (result i32) (call $pick_un (i32.const 1) (i32.const 41)))
  (func (export "pick_un3") (result i32) (call $pick_un (i32.const 3) (i32.const 41)))
  (func (export "pick_un5") (result i32) (call $pick_un (i32.const 5) (i32.const 41)))
  (func (export "pick_un7") (result i32) (call $pick_un (i32.const 7) (i32.const 41)))
  (func (export "pick_bin2") (result i32) (call $pick_bin (i32.const 2) (i32.const 9) (i32.const 4)))
  (func (export "pick_bin4") (result i32) (call $pick_bin (i32.const 4) (i32.const 9) (i32.const 4)))
  (func (export "pick_bin6") (result i32) (call $pick_bin (i32.const 6) (i32.const 9) (i32.const 4)))
  (func (export "pick_bin8") (result i32) (call $pick_bin (i32.const 8) (i32.const 9) (i32.const 4)))
  (func (export "pick_bin9") (result i32) (call $pick_bin (i32.const 9) (i32.const 9) (i32.const 4)))

  ;; the type whose window starts at 100, an i64 result
  (func $pick_wide (param $k i32) (param $x i64) (result i64)
    (call_indirect (type $wide) (local.get $x) (i32.const -3) (local.get $k)))
  (func (export "pick_wide100") (result i64)
    (call $pick_wide (i32.const 100) (i64.const 0x123456789)))
  (func (export "pick_wide103") (result i64)
    (call $pick_wide (i32.const 103) (i64.const 0x123456789)))
  (func (export "pick_un101") (result i32) (call $pick_un (i32.const 101) (i32.const 41)))
  (func (export "pick_bin102") (result i32)
    (call $pick_bin (i32.const 102) (i32.const 9) (i32.const 4)))

  ;; a tail call through the table, a million deep: it must stay a jump
  (func $chain (param $n i32) (param $acc i32) (result i32)
    (if (result i32) (i32.eqz (local.get $n))
      (then (local.get $acc))
      (else
        (return_call_indirect (type $bin)
          (i32.sub (local.get $n) (i32.const 1))
          (call_indirect (type $un) (local.get $acc)
            (i32.load8_u (i32.and (local.get $n) (i32.const 7))))
          (i32.load8_u (i32.const 32))))))
  (func (export "chain") (result i32) (call $chain (i32.const 1000000) (i32.const 5)))
)
