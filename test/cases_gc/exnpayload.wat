;; An exception's payload is a root set while the exception is being built.
;;
;; A `throw` puts its values in the throw area, which belongs to no value and no
;; safepoint map -- so a reference among them is invisible to the collector, and
;; the allocation of the exception object itself is the one thing that can
;; collect it out from under the throw.  `exn_alloc` suppresses collection for
;; exactly that window.
;;
;; That suppression has a cost this case does not measure: the allocation debt
;; still accrues, and a program whose *only* allocation is a throw never reaches
;; an unsuppressed allocation to pay it, so its heap grows without bound
;; (ktbench/exn: 750,000 throws, one collection, 176 MB against a 77 KB live
;; set).  The obvious repair -- root the payload words and let the allocation
;; pace like any other -- makes *this case* return 4002 instead of 14000 under
;; WARMC_GC_STRESS, so something else on the throw path holds a reference the
;; rootlist does not cover.  Until that is understood the suppression stays, and
;; this case is what will say whether a future attempt is correct.
;;
;; Both payload values are freshly allocated and referenced by nothing else, and
;; they are read back *after* the catch, so losing either is an answer and not a
;; crash.

(module
  (type $box (struct (field i32)))
  (tag $t (param (ref $box)) (param (ref $box)))

  (func (export "payload_survives") (local $s i32) (local $i i32)
    (local $a (ref null $box)) (local $b (ref null $box))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 2000)))
        (block $caught (result (ref $box) (ref $box))
          (try_table (catch $t $caught)
            (throw $t (struct.new $box (i32.const 3))
                      (struct.new $box (i32.const 4))))
          unreachable)
        (local.set $b) (local.set $a)
        (local.set $s (i32.add (local.get $s)
          (i32.add (struct.get $box 0 (local.get $a))
                   (struct.get $box 0 (local.get $b)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (if (i32.ne (local.get $s) (i32.const 14000)) (then unreachable))))
