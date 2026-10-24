;; -fdead-frames's shapes (test/run_all.sh runs the cases with it too): a
;; module with C's stack -- $sp, which each function moves down by its frame
;; and back -- where each store into a frame that something reads is read:
;; by the function itself after a loop's back edge, in a landing pad and
;; through a later read of the stack pointer, by a callee through a pointer
;; at a known offset, through one it walks, through one it passes on, by
;; one that keeps the pointer, through an indirect call, by memory.copy, and
;; by functions that scan the stack up from the stack pointer or from a
;; local's address, as collectors do.  Each export's answer depends on those
;; stores; the frames' other stores, which nothing reads, are the ones the
;; pass takes.
;; Every frame a scan reads is written whole first: under the flag, bytes a
;; frame never wrote are whatever the stores it dropped left.

(module
  (memory 2)
  (global $sp (mut i32) (i32.const 65536))
  ;; never set: each callee tail-calls itself under it, which keeps it out of
  ;; line, so that its reads stay its own and not its caller's
  (global $deep (mut i32) (i32.const 0))
  (global $kept (mut i32) (i32.const 0))
  (type $t1 (func (param i32) (result i32)))
  (table 1 funcref)
  (elem (i32.const 0) $sum2)
  (tag $e (param i32))

  ;; reads [p, p+8)
  (func $sum2 (param $p i32) (result i32)
    (if (global.get $deep) (then (return_call $sum2 (local.get $p))))
    (i32.add (i32.load (local.get $p)) (i32.load offset=4 (local.get $p))))

  ;; reads [p+4, p+12), through $sum2
  (func $fwd (param $p i32) (result i32)
    (if (global.get $deep) (then (return_call $fwd (local.get $p))))
    (call $sum2 (i32.add (local.get $p) (i32.const 4))))

  ;; reads [p, p + 4n), walking p
  (func $sumn (param $p i32) (param $n i32) (result i32)
    (local $s i32)
    (if (global.get $deep) (then (return_call $sumn (local.get $p) (local.get $n))))
    (block $d
      (loop $l
        (br_if $d (i32.eqz (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; keeps the pointer, which $use_kept reads through later
  (func $keep (param $p i32)
    (if (global.get $deep) (then (return_call $keep (local.get $p))))
    (global.set $kept (local.get $p)))
  (func $use_kept (result i32)
    (if (global.get $deep) (then (return_call $use_kept)))
    (i32.load (global.get $kept)))

  ;; the words from the stack pointer up to the top of the stack
  (func $scan (result i32)
    (local $p i32) (local $s i32)
    (local.set $p (global.get $sp))
    (if (global.get $deep) (then (return_call $scan)))
    (block $d
      (loop $l
        (br_if $d (i32.ge_u (local.get $p) (i32.const 65536)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (br $l)))
    (local.get $s))

  ;; scans from a frame of its own, which it fills first
  (func $scan_framed (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 8)))
    (if (global.get $deep) (then (return_call $scan_framed)))
    (global.set $sp (local.get $fp))
    (i64.store (local.get $fp) (i64.const 0))
    (local.set $r (call $scan))
    (global.set $sp (i32.add (local.get $fp) (i32.const 8)))
    (local.get $r))

  ;; the words from a local of its own up to the top of the stack, as a
  ;; conservative collector finds its roots
  (func $scan_from_local (result i32)
    (local $fp i32) (local $p i32) (local $s i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (if (global.get $deep) (then (return_call $scan_from_local)))
    (global.set $sp (local.get $fp))
    (i64.store (local.get $fp) (i64.const 0))
    (i64.store offset=8 (local.get $fp) (i64.const 0))
    (local.set $p (local.get $fp))
    (block $d
      (loop $l
        (br_if $d (i32.ge_u (local.get $p) (i32.const 65536)))
        (local.set $s (i32.add (local.get $s) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (br $l)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $s))

  ;; writes memory elsewhere, so nothing in a frame is forwarded across it
  (func $touch
    (if (global.get $deep) (then (return_call $touch)))
    (i32.store (i32.const 70008) (i32.add (i32.load (i32.const 70008)) (i32.const 1))))

  (func $thrower (param $x i32)
    (if (global.get $deep) (then (return_call $thrower (local.get $x))))
    (throw $e (local.get $x)))

  (func (export "callee_reads") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 5))
    (i32.store offset=4 (local.get $fp) (i32.const 7))
    (i32.store offset=8 (local.get $fp) (i32.const 100))
    (local.set $r (call $sum2 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "callee_walks") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 1))
    (i32.store offset=4 (local.get $fp) (i32.const 2))
    (i32.store offset=8 (local.get $fp) (i32.const 3))
    (i32.store offset=12 (local.get $fp) (i32.const 4))
    (local.set $r (call $sumn (local.get $fp) (i32.const 4)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "passed_on") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 1000))
    (i32.store offset=4 (local.get $fp) (i32.const 20))
    (i32.store offset=8 (local.get $fp) (i32.const 22))
    (i32.store offset=12 (local.get $fp) (i32.const 1000))
    (local.set $r (call $fwd (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "partly_read") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store offset=4 (local.get $fp) (i32.const 11))
    (i32.store offset=8 (local.get $fp) (i32.const 22))
    (i32.store offset=12 (local.get $fp) (i32.const 33))
    (local.set $r (call $sum2 (i32.add (local.get $fp) (i32.const 4))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "kept_pointer") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (call $keep (local.get $fp))
    (i32.store (local.get $fp) (i32.const 42))
    (local.set $r (call $use_kept))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "scanned") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 1))
    (i32.store offset=4 (local.get $fp) (i32.const 2))
    (i32.store offset=8 (local.get $fp) (i32.const 3))
    (i32.store offset=12 (local.get $fp) (i32.const 4))
    (local.set $r (call $scan_framed))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "scanned_from_local") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 5))
    (i32.store offset=4 (local.get $fp) (i32.const 6))
    (i32.store offset=8 (local.get $fp) (i32.const 7))
    (i32.store offset=12 (local.get $fp) (i32.const 8))
    (local.set $r (call $scan_from_local))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "loop_carried") (result i32)
    (local $fp i32) (local $i i32) (local $s i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 0))
    (loop $l
      (local.set $s (i32.add (local.get $s) (i32.load (local.get $fp))))
      (i32.store (local.get $fp) (i32.mul (local.get $i) (i32.const 3)))
      (call $touch)
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 10))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $s))

  (func (export "landing_pad") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 77))
    (local.set $r
      (i32.add
        (block $h (result i32)
          (try_table (catch $e $h) (call $thrower (i32.const 1)))
          (i32.const 1000))
        (i32.load (local.get $fp))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "later_sp") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store offset=4 (local.get $fp) (i32.const 5))
    ;; a callee that moves the stack pointer and puts it back, so the read
    ;; after it is not known to be the frame's
    (drop (call $inner (i32.const 1)))
    (local.set $r (i32.load offset=4 (global.get $sp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  (func (export "copied_out") (result i32)
    (local $fp i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 9))
    (i32.store offset=4 (local.get $fp) (i32.const 8))
    (memory.copy (i32.const 70000) (local.get $fp) (i32.const 8))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (i32.add (i32.load (i32.const 70000)) (i32.load (i32.const 70004))))

  (func (export "indirect") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 3))
    (i32.store offset=4 (local.get $fp) (i32.const 4))
    (local.set $r (call_indirect (type $t1) (local.get $fp) (i32.const 0)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))

  ;; stores nothing reads but the function's own loads, which forwarding
  ;; answers; its answer is the same with them or without
  (func (export "own_reads") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 32)))
    (global.set $sp (local.get $fp))
    (i64.store (local.get $fp) (i64.const 0x0000000200000001))
    (i64.store offset=8 (local.get $fp) (i64.const 0x0000000400000003))
    (local.set $r (i32.add (i32.load (local.get $fp)) (i32.load offset=12 (local.get $fp))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; a frame two calls deep, each with its own
  (func $inner (param $x i32) (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (if (global.get $deep) (then (return_call $inner (local.get $x))))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (local.get $x))
    (i32.store offset=4 (local.get $fp) (i32.mul (local.get $x) (i32.const 2)))
    (local.set $r (call $sum2 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))
  (func (export "nested") (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 40))
    (local.set $r (call $inner (i32.const 7)))
    (local.set $r (i32.add (local.get $r) (i32.load (local.get $fp))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))
)
