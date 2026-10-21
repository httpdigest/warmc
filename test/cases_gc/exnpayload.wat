;; An exception's payload is a root set while the exception is being built.
;;
;; A `throw` puts its values in the throw area, which belongs to no value and no
;; safepoint map -- so a reference among them is invisible to the collector, and
;; the allocation of the exception object itself is the one thing that can
;; collect it out from under the throw.  `exn_alloc` suppresses collection for
;; exactly that window.
;;
;; The collection that allocation puts off happens right after it, once the
;; payload is in the object: the object is rooted, the throw site carries a
;; stack map like any call, and the runtime collects there if the allocation
;; ran up the debt.  Without that, a program whose *only* allocation on the
;; collector's heap is a throw -- C++, where every throw is one -- never reached
;; an unsuppressed allocation, and its heap grew with every exception it ever
;; threw (cppbench/exceptions: 30 MB at 1.2 million iterations, where
;; wasmtime stays under 10).  An earlier repair rooted the payload words and let the
;; allocation itself collect, and this case returned 4002 instead of 14000
;; under WARMC_GC_STRESS: that collection ran with no wasm frame to start the
;; walk from, so nothing the frames held was a root.
;;
;; Both payload values are freshly allocated and referenced by nothing else, and
;; they are read back *after* the catch, so losing either is an answer and not a
;; crash.  The throw goes through a funcref in a mutable global, a call the
;; optimiser cannot see into: thrown where it is caught, the throw would be a
;; jump, and there would be no exception object to allocate.

(module
  (type $box (struct (field i32)))
  (tag $t (param (ref $box)) (param (ref $box)))
  (type $raise_t (func (param (ref $box)) (param (ref $box))))
  (global $raise (mut (ref null $raise_t)) (ref.func $raise))
  (type $pass_t (func (param (ref $box)) (result (ref $box))))
  (global $pass (mut (ref null $pass_t)) (ref.func $pass))
  (func $pass (type $pass_t) (local.get 0))
  (func $raise (type $raise_t) (param $a (ref $box)) (param $b (ref $box))
    (throw $t (local.get $a) (local.get $b)))

  (func (export "payload_survives") (local $s i32) (local $i i32)
    (local $a (ref null $box)) (local $b (ref null $box))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 2000)))
        (block $caught (result (ref $box) (ref $box))
          (try_table (catch $t $caught)
            (call_ref $raise_t (struct.new $box (i32.const 3))
                               (struct.new $box (i32.const 4))
                               (global.get $raise)))
          unreachable)
        (local.set $b) (local.set $a)
        (local.set $s (i32.add (local.get $s)
          (i32.add (struct.get $box 0 (local.get $a))
                   (struct.get $box 0 (local.get $b)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (if (i32.ne (local.get $s) (i32.const 14000)) (then unreachable)))

  ;; A throw whose own frame has the pad.  catch_all_ref needs the exception
  ;; object, so the throw stays a throw although it is caught where it is
  ;; thrown, and $keep is live into the pad across it: while the runtime
  ;; collects at the throw, that site's stack map is the only root $keep has.
  ;; It comes back from an opaque call, or escape analysis would see a box
  ;; that never leaves the function and keep its field in a register.  A root
  ;; recorded in the wrong place is what WARMC_GC_VERIFY checks for, in the
  ;; run_gc_modes.sh modes that set it; a root left out is not visible here,
  ;; since the collector frees a page only once all of it is dead and the
  ;; exception object keeps $keep's page alive past the catch.
  (func (export "pad_keeps_ref") (local $s i32) (local $i i32)
    (local $keep (ref null $box))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 2000)))
        (local.set $keep (call_ref $pass_t (struct.new $box (local.get $i))
                                           (global.get $pass)))
        (block $caught (result exnref)
          (try_table (catch_all_ref $caught)
            (throw $t (struct.new $box (i32.const 3))
                      (struct.new $box (i32.const 4))))
          unreachable)
        drop
        (local.set $s (i32.add (local.get $s) (struct.get $box 0 (local.get $keep))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (if (i32.ne (local.get $s) (i32.const 1999000)) (then unreachable))))
