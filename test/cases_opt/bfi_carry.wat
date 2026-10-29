;; regalloc.inc's coalescer leaves apart a bfi made of a value a loop
;; carries and the parameter whose field it inserts: hw = hw << 8 & 0xff00
;; | c, as clang writes musl's uint16_t, the field hw's low byte put at bits
;; 8-15 of the byte c (isel.c's s->bfi).
;; Made the parameter's own register, the bfi would write the register its
;; field comes from: a64 then makes it in a scratch register from a copy of
;; c and copies it back, on the chain the loop carries.  twobyte is musl's
;; twobyte_strstr (cbench/strings); pairs inserts the field of the other
;; value it carries, which is made the parameter's as any value is.
;;
;; stats -O2 a64: bfi_apart=1
;;
;; run: twobyte 16 27759 => i32:20
;; run: twobyte 16 26725 => i32:17
;; run: twobyte 16 31354 => i32:0
;; run: twobyte 16 27748 => i32:26
;; run: twobyte 16 28448 => i32:21
;; run: pairs 16 => i32:1869378148
;; run: pairs 20 => i32:1869378148
;; run: pairs 26 => i32:100
;; run: pairs 27 => i32:0

(module
  (memory 1)
  (data (i32.const 16) "hello world\00")
  ;; the address of the second byte of the first pair p..that is nw, or 0
  (func $twobyte (export "twobyte") (param $p i32) (param $nw i32) (result i32)
    (local $hw i32) (local $c i32)
    (local.set $hw (i32.or (i32.shl (i32.load8_u (local.get $p)) (i32.const 8))
                           (i32.load8_u offset=1 (local.get $p))))
    (local.set $p (i32.add (local.get $p) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.eq (local.get $hw) (local.get $nw)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $done (i32.eqz (local.get $c)))
        (local.set $hw (i32.or (i32.and (i32.shl (local.get $hw) (i32.const 8)) (i32.const 0xff00))
                               (local.get $c)))
        (br $l)))
    (select (local.get $p) (i32.const 0) (i32.load8_u (local.get $p))))
  ;; a and b each take the other's low byte above the next byte
  (func $pairs (export "pairs") (param $p i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $t i32)
    (block $done
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $done (i32.eqz (local.get $c)))
        (local.set $t (i32.or (i32.and (i32.shl (local.get $b) (i32.const 8)) (i32.const 0xff00))
                              (local.get $c)))
        (local.set $b (local.get $a))
        (local.set $a (local.get $t))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.or (local.get $a) (i32.shl (local.get $b) (i32.const 16))))
)
