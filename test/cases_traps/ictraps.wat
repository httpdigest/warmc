;; The slots a call_indirect code table must trap on (src/module_data.c,
;; codegen_plan_icall_tables).  Table 0 is the module's only table and
;; nothing writes it, so each call type gets a table of code pointers whose
;; rejected slots are trap stubs, and every one of them has to say what the
;; full lookup would have said.  test/run_traps.sh runs each export.
;;
;; $x's entries reach from slot 2 to near the end, so its window is the whole
;; table, 9000 slots rounded up to 12288 for the compare's immediate: past
;; 9000 a stub says out of bounds, past 12288 the compare does.  $y's start
;; at 4100, past 4096 empty slots, so its window starts at 4096 (an
;; immediate's `lsl #12`) and runs to 4096 + 8192: an index below it or past
;; it takes the full lookup out of line, which traps with the slot's own
;; reason, null or a type mismatch or out of bounds.  $z names no entry and
;; has no table at all.  The indices come from memory, so no pass can fold
;; them.
(module
  (type $x (func (param i32) (result i32)))
  (type $y (func (param i32 i32) (result i32)))
  (type $z (func (result i64)))
  (memory 1)
  (table 9000 funcref)
  (elem (i32.const 2) func $x1)
  (elem (i32.const 7) func $x2)
  (elem (i32.const 4100) func $y1)
  (elem (i32.const 5000) func $x3)
  (elem (i32.const 8300) func $y2)
  (elem (i32.const 8990) func $x4)

  ;; index k lives at 4 * k for k < 16
  (data (i32.const 0)
    "\02\00\00\00"  ;; 0: $x1
    "\07\00\00\00"  ;; 1: $x2
    "\88\13\00\00"  ;; 2: 5000, $x3
    "\1e\23\00\00"  ;; 3: 8990, $x4
    "\04\10\00\00"  ;; 4: 4100, $y1
    "\6c\20\00\00"  ;; 5: 8300, $y2
    "\03\00\00\00"  ;; 6: 3, empty, inside every window that starts at 0
    "\1c\25\00\00"  ;; 7: 9500, past the table, inside the rounded window
    "\20\4e\00\00"  ;; 8: 20000, past every window
    "\ff\ff\ff\ff"  ;; 9: 2^32 - 1
    "\64\00\00\00"  ;; 10: 100, empty, below $y's window
    "\70\17\00\00"  ;; 11: 6000, empty, inside $y's window
    "\00\30\00\00"  ;; 12: 12288, the first index past $y's window
    "\00\00\00\80"  ;; 13: 2^31
    "\00\f0\ff\ff"  ;; 14: 2^32 - 4096
    "\28\23\00\00") ;; 15: 9000, the first index past the table

  (func $x1 (type $x) (i32.add (local.get 0) (i32.const 1)))
  (func $x2 (type $x) (i32.mul (local.get 0) (i32.const 3)))
  (func $x3 (type $x) (i32.sub (local.get 0) (i32.const 5)))
  (func $x4 (type $x) (i32.xor (local.get 0) (i32.const 0x55)))
  (func $y1 (type $y) (i32.add (local.get 0) (local.get 1)))
  (func $y2 (type $y) (i32.sub (local.get 0) (local.get 1)))

  (func $idx (param $k i32) (result i32) (i32.load (i32.shl (local.get $k) (i32.const 2))))
  (func $cx (param $k i32) (result i32)
    (call_indirect (type $x) (i32.const 100) (call $idx (local.get $k))))
  (func $cy (param $k i32) (result i32)
    (call_indirect (type $y) (i32.const 100) (i32.const 7) (call $idx (local.get $k))))
  (func $cz (param $k i32) (result i64)
    (call_indirect (type $z) (call $idx (local.get $k))))
  (func $tx (param $k i32) (result i32)
    (return_call_indirect (type $x) (i32.const 100) (call $idx (local.get $k))))
  (func $ty (param $k i32) (result i32)
    (return_call_indirect (type $y) (i32.const 100) (i32.const 7) (call $idx (local.get $k))))

  ;; every entry through its own type's table, called and tail-called
  (func (export "ict_ok") (result i32)
    (if (i32.ne (call $cx (i32.const 0)) (i32.const 101)) (then (unreachable)))
    (if (i32.ne (call $cx (i32.const 1)) (i32.const 300)) (then (unreachable)))
    (if (i32.ne (call $cx (i32.const 2)) (i32.const 95)) (then (unreachable)))
    (if (i32.ne (call $cx (i32.const 3)) (i32.const 49)) (then (unreachable)))
    (if (i32.ne (call $cy (i32.const 4)) (i32.const 107)) (then (unreachable)))
    (if (i32.ne (call $cy (i32.const 5)) (i32.const 93)) (then (unreachable)))
    (if (i32.ne (call $tx (i32.const 3)) (i32.const 49)) (then (unreachable)))
    (if (i32.ne (call $ty (i32.const 4)) (i32.const 107)) (then (unreachable)))
    (i32.const 0))

  ;; $x: the whole table is its window
  (func (export "ict_x_null") (result i32) (call $cx (i32.const 6)))
  (func (export "ict_x_type") (result i32) (call $cx (i32.const 4)))
  (func (export "ict_x_oob_pad") (result i32) (call $cx (i32.const 7)))
  (func (export "ict_x_oob_end") (result i32) (call $cx (i32.const 15)))
  (func (export "ict_x_oob") (result i32) (call $cx (i32.const 8)))
  (func (export "ict_x_oob_top") (result i32) (call $cx (i32.const 9)))
  (func (export "ict_x_tail_null") (result i32) (call $tx (i32.const 6)))
  (func (export "ict_x_tail_oob") (result i32) (call $tx (i32.const 8)))

  ;; $y: a window from 4096, the full lookup outside it
  (func (export "ict_y_null_below") (result i32) (call $cy (i32.const 10)))
  (func (export "ict_y_type_below") (result i32) (call $cy (i32.const 0)))
  (func (export "ict_y_null_in") (result i32) (call $cy (i32.const 11)))
  (func (export "ict_y_type_in") (result i32) (call $cy (i32.const 2)))
  (func (export "ict_y_type_in_end") (result i32) (call $cy (i32.const 3)))
  (func (export "ict_y_oob_pad") (result i32) (call $cy (i32.const 7)))
  (func (export "ict_y_oob_past") (result i32) (call $cy (i32.const 12)))
  (func (export "ict_y_oob_far") (result i32) (call $cy (i32.const 8)))
  (func (export "ict_y_oob_2p31") (result i32) (call $cy (i32.const 13)))
  (func (export "ict_y_oob_wrap") (result i32) (call $cy (i32.const 14)))
  (func (export "ict_y_tail_type_below") (result i32) (call $ty (i32.const 1)))
  (func (export "ict_y_tail_null_in") (result i32) (call $ty (i32.const 11)))

  ;; $z: no table, the full lookup inline
  (func (export "ict_z_type") (result i64) (call $cz (i32.const 0)))
  (func (export "ict_z_null") (result i64) (call $cz (i32.const 6)))
  (func (export "ict_z_oob") (result i64) (call $cz (i32.const 15)))

  ;; a loop whose table address sits in the pinned register, until index
  ;; 4 names a $y
  (func (export "ict_loop_type") (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s
        (call_indirect (type $x) (local.get $s) (call $idx (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))
    (local.get $s))
)
