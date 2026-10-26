;; Accesses at constant addresses past a displacement's reach (opt.c
;; mem_ptr_consts): a pointer to the page below the first of a block's run,
;; one add to the memory base (lsl 12), and each access by displacement from
;; it -- for an access alone in its block too, as dlmalloc's tests of
;; mparams and gm are.  An access the page does not reach by a displacement
;; (0x11113, an unaligned word too far into its page for an ldur) keeps its
;; own address, and so do addresses from 16 MiB up, past what an add of a
;; page makes ($high, in a second memory of 258 pages); every one reads and
;; writes the bytes it did -- a data segment's ($datum), and a store's
;; through an address in a register -- and traps where it did, at the end of
;; either memory, a page reached by one add included ($page_trap).
;;
;; lone makes no access through an address in a register; its pointers are
;; adds of pages, one an access (its load of 0x10f50 is the store's value):
;; shape: lone \[x\d+, x\d+\]=0
;; shape: lone add\s+x\d+, x\d+, #0x1[01], lsl #12=5
;;
;; run: datum 0 => i32:305419896
;; run: datum 1 => i32:-1430532899
;; run: datum 69928 => i32:7
;; run: lone 0 => i32:1145324612
;; run: lone 1 => i32:2004318071
;; run: sizes 7 => i64:-6148914691236560882
;; run: sizes 0 => i64:0
;; run: unaligned 3 => i32:322262905
;; run: unaligned 0 => i32:16843009
;; run: pair_unaligned => i32:201984006
;; run: high 5 => i32:1075
;; run: high 0 => i32:0
;; run: loop 100 => i32:5100
;; run: loop 1 => i32:0
;; run: f64s 2 => f64:0x4029000000000000
;; run: f64s 0 => f64:0x0000000000000000
;; run: edge_ok => i32:-559038737
;; run: edge_trap => trap: out of bounds memory access
;; run: edge_trap8 => trap: out of bounds memory access
;; run: page_trap => trap: out of bounds memory access
;; run: high_edge_ok => i32:-559038737
;; run: high_edge_trap => trap: out of bounds memory access
(module
  (memory $m 2 2)
  (memory $big 258 258)

  (data (i32.const 0x11120) "\78\56\34\12\dd\cc\bb\aa")
  (data (i32.const 0x12113) "\01\02\03\04\05\06\07\08")

  ;; bytes the data segment put at 0x11120, read at a constant address
  ;; alone in its block, and a word stored through an address in a register
  ;; (0x11128 = 69928) read back at a constant one
  (func $datum (export "datum") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.load (i32.const 0x11120)))
      (else
        (if (result i32) (i32.eq (local.get 0) (i32.const 1))
          (then (i32.load (i32.const 0x11124)))
          (else
            (i32.store (local.get 0) (i32.const 7))
            (i32.load (i32.const 0x11128)))))))

  ;; one access per block, at addresses whose page differs
  (func $lone (export "lone") (param i32) (result i32)
    (i32.store (i32.const 0x10f50) (i32.const 0x11111111))
    (if (local.get 0)
      (then (i32.store (i32.const 0x11110) (i32.const 0x22222222)))
      (else (i32.store (i32.const 0x11114) (i32.const 0x33333333))))
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.add (i32.load (i32.const 0x11114)) (i32.load (i32.const 0x10f50))))
      (else
        (i32.add (i32.load (i32.const 0x11110))
                 (i32.mul (i32.load (i32.const 0x10f50)) (i32.const 5))))))

  ;; a byte, a halfword and a word, each by itself, then read back as one
  ;; doubleword
  (func $sizes (export "sizes") (param i32) (result i64)
    (i64.store (i32.const 0x12008) (i64.const 0))
    (i32.store8 (i32.const 0x12008) (local.get 0))
    (if (local.get 0) (then (i32.store16 (i32.const 0x1200a) (i32.const 0xaaaa))))
    (if (local.get 0) (then (i32.store (i32.const 0x1200c) (i32.const 0xaaaaaaaa))))
    (i64.add (i64.load (i32.const 0x12008))
             (i64.extend_i32_u (i32.load8_u (i32.const 0x12008)))))

  ;; an unaligned word an ldur reaches from its page (0x110fd), and words it
  ;; does not (0x11113, 0x11117), two of which share a pointer to 0x11110
  (func $unaligned (export "unaligned") (param i32) (result i32)
    (i32.store (i32.const 0x110fd) (i32.const 0x12345678))
    (i32.store (i32.const 0x11117) (i32.const 0x01010101))
    (if (local.get 0) (then (i32.store (i32.const 0x11113) (i32.load (i32.const 0x110fd)))))
    (i32.add (i32.load (i32.const 0x11113)) (i32.load (i32.const 0x11117))))

  ;; two unaligned words of a data segment, which share a pointer to
  ;; 0x12110, not a page
  (func $pair_unaligned (export "pair_unaligned") (result i32)
    (i32.add (i32.load (i32.const 0x12113)) (i32.load (i32.const 0x12117))))

  ;; from 16 MiB up the address is built as before
  (func $high (export "high") (param i32) (result i32)
    (i32.store $big (i32.const 0x1000010) (local.get 0))
    (if (local.get 0) (then (i32.store $big (i32.const 0x1001234) (i32.const 1000))))
    (i32.add (i32.load $big (i32.const 0x1001234))
             (i32.mul (i32.load $big (i32.const 0x1000010)) (i32.const 15))))

  ;; accesses at constant addresses in a loop
  (func $loop (export "loop") (param i32) (result i32)
    (local $i i32)
    (i32.store (i32.const 0x13000) (i32.const 0))
    (i32.store (i32.const 0x13ffc) (i32.const 0))
    (loop $l
      (i32.store (i32.const 0x13000) (i32.add (i32.load (i32.const 0x13000)) (local.get $i)))
      (if (i32.and (local.get $i) (i32.const 1))
        (then (i32.store (i32.const 0x13ffc) (i32.add (i32.load (i32.const 0x13ffc)) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get 0))))
    (i32.add (i32.load (i32.const 0x13000)) (i32.load (i32.const 0x13ffc))))

  (func $f64s (export "f64s") (param i32) (result f64)
    (f64.store (i32.const 0x14010) (f64.convert_i32_u (local.get 0)))
    (if (local.get 0) (then (f64.store (i32.const 0x14ff8) (f64.const 10.5))))
    (f64.add (f64.load (i32.const 0x14010)) (f64.load (i32.const 0x14ff8))))

  ;; the last word of each memory, and the words and doubleword that run
  ;; past one
  (func $edge_ok (export "edge_ok") (result i32)
    (i32.store (i32.const 0x1fffc) (i32.const 0xdeadbeef))
    (i32.load (i32.const 0x1fffc)))
  (func $edge_trap (export "edge_trap") (result i32)
    (i32.load (i32.const 0x1fffd)))
  (func $edge_trap8 (export "edge_trap8") (result i64)
    (i64.load (i32.const 0x1fff9)))
  ;; a page one add makes, past the end of the first memory
  (func $page_trap (export "page_trap") (result i32)
    (i32.load (i32.const 0x21f50)))
  (func $high_edge_ok (export "high_edge_ok") (result i32)
    (i32.store $big (i32.const 0x101fffc) (i32.const 0xdeadbeef))
    (i32.load $big (i32.const 0x101fffc)))
  (func $high_edge_trap (export "high_edge_trap") (result i32)
    (i32.load $big (i32.const 0x101fffd)))
)
