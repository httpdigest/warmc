;; A function of Go's in the shape Go's compiler gives main.fib, its
;; resume dispatch a br_table on the first parameter (opt.c pass_go_resume),
;; and what becomes of its calls.  The table keeps 0 and 1, a call's way in,
;; and the blocks the return addresses (2 << 16 | k) name: 0, where the
;; morestack call resumes it, 4 and 5; 0 and 1 are tested first, and the
;; table, which only a resume reaches, is laid out after the code
;; (Block.outline, ir.c ir_emit_order), so that a call falls through to the
;; entry and each call to the code after it.  morestack_noctxt, inlined,
;; returns morestack's result packed again as gosp.c packs it, which the fold
;; sees through (opt.c repacked), so the call of morestack, which never
;; returns normally, is taken as unwinding at -O2 too (go_noret_calls).  The
;; stack pointer after each call, the low half of its result, goes on to the
;; code after the call and to the unwinding return in the result's own
;; register (opt.c pass_wrap_params, isel.c analyse_wrap_alias): no move of
;; it after either call, the one mov from w0 the unwinding return's extend.
;; One unconditional branch, in the table's compare tree; b.hs twice, the
;; entry's test for the table and one of the tree's.
;;
;; stats -O1: go_resume_pruned=3 go_resume_split=1 go_noret_calls=1
;; stats -O2: go_resume_pruned=3 go_resume_split=1 go_noret_calls=1
;; stats -O1 a64: wrap_params=3
;; stats -O2 a64: wrap_params=3
;; shape: fib mov\tw\d+, w0$=1
;; shape: fib \tb\t=1
;; shape: fib b\.hs=2
;; shape: fib and\t=0
;;
;; run: fib 20 0 => i32:6765
;; run: fib 0 0 => i32:0
;; run: fib 1 0 => i32:1
;; run: fib 2 0 => i32:1
;; run: fib 30 3500 => i32:-1
;; run: resume4 10 1000 => i32:1021
;; run: resume4 2 7 => i32:7
;; run: resume5 1000 234 => i32:1234
(module
  (@custom "go:buildid" "warmc-test")
  (type $go (func (param i32) (result i32)))
  (type $ex (func (param i32 i32) (result i32)))
  (global $sp (mut i32) (i32.const 4096))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 64))
  (memory 1)
  (table 3 funcref)
  (elem (i32.const 0) $morestack $morestack_noctxt $fib)

  ;; out of stack: unwinds, as Go's switches to a bigger stack, after
  ;; bumping counters at 8..100, too many for the inliner to take
  (func $morestack (type $go) (local i32)
    (i32.store offset=8 (local.get 1) (i32.add (i32.load offset=8 (local.get 1)) (i32.const 1)))
    (i32.store offset=12 (local.get 1) (i32.add (i32.load offset=12 (local.get 1)) (i32.const 2)))
    (i32.store offset=16 (local.get 1) (i32.add (i32.load offset=16 (local.get 1)) (i32.const 3)))
    (i32.store offset=20 (local.get 1) (i32.add (i32.load offset=20 (local.get 1)) (i32.const 4)))
    (i32.store offset=24 (local.get 1) (i32.add (i32.load offset=24 (local.get 1)) (i32.const 5)))
    (i32.store offset=28 (local.get 1) (i32.add (i32.load offset=28 (local.get 1)) (i32.const 6)))
    (i32.store offset=32 (local.get 1) (i32.add (i32.load offset=32 (local.get 1)) (i32.const 7)))
    (i32.store offset=36 (local.get 1) (i32.add (i32.load offset=36 (local.get 1)) (i32.const 8)))
    (i32.store offset=40 (local.get 1) (i32.add (i32.load offset=40 (local.get 1)) (i32.const 9)))
    (i32.store offset=44 (local.get 1) (i32.add (i32.load offset=44 (local.get 1)) (i32.const 10)))
    (i32.store offset=48 (local.get 1) (i32.add (i32.load offset=48 (local.get 1)) (i32.const 11)))
    (i32.store offset=52 (local.get 1) (i32.add (i32.load offset=52 (local.get 1)) (i32.const 12)))
    (i32.store offset=56 (local.get 1) (i32.add (i32.load offset=56 (local.get 1)) (i32.const 13)))
    (i32.store offset=60 (local.get 1) (i32.add (i32.load offset=60 (local.get 1)) (i32.const 14)))
    (i32.store offset=64 (local.get 1) (i32.add (i32.load offset=64 (local.get 1)) (i32.const 15)))
    (i32.store offset=68 (local.get 1) (i32.add (i32.load offset=68 (local.get 1)) (i32.const 16)))
    (i32.store offset=72 (local.get 1) (i32.add (i32.load offset=72 (local.get 1)) (i32.const 17)))
    (i32.store offset=76 (local.get 1) (i32.add (i32.load offset=76 (local.get 1)) (i32.const 18)))
    (i32.store offset=80 (local.get 1) (i32.add (i32.load offset=80 (local.get 1)) (i32.const 19)))
    (i32.store offset=84 (local.get 1) (i32.add (i32.load offset=84 (local.get 1)) (i32.const 20)))
    (i32.store offset=88 (local.get 1) (i32.add (i32.load offset=88 (local.get 1)) (i32.const 21)))
    (i32.store offset=92 (local.get 1) (i32.add (i32.load offset=92 (local.get 1)) (i32.const 22)))
    (i32.store offset=96 (local.get 1) (i32.add (i32.load offset=96 (local.get 1)) (i32.const 23)))
    (i32.store offset=100 (local.get 1) (i32.add (i32.load offset=100 (local.get 1)) (i32.const 24)))
    (i32.const 1))
  (func $morestack_noctxt (type $go) (local i32)
    global.get 0
    local.set 1
    i64.const 0
    global.set 1
    i32.const 0
    call $morestack
    global.get 0
    local.set 1
    return)

  ;; Go's main.fib: the argument at sp+8, the result at sp+16
  (func $fib (type $go) (local i32 i64)
    global.get 0
    local.set 1
    block
      loop
        block
          block
            block
              block
                block
                  block
                    local.get 0
                    br_table 0 0 1 2 3 4 5
                  end
                  local.get 1
                  global.get 2
                  i32.wrap_i64
                  i32.load offset=16
                  i32.le_u
                  if
                    local.get 1
                    i32.const 8
                    i32.sub
                    local.tee 1
                    global.set 0
                    local.get 1
                    i64.const 0x20000
                    i64.store
                    i32.const 0
                    call $morestack_noctxt
                    global.get 0
                    local.set 1
                    br_if 7
                  end
                  local.get 1
                  i32.const 24
                  i32.sub
                  local.tee 1
                  global.set 0
                  local.get 1
                  i64.load offset=32
                  i64.const 2
                  i64.lt_s
                  i32.eqz
                  if
                    i32.const 3
                    local.set 0
                    br 6
                  end
                end
                local.get 1
                i64.extend_i32_u
                i64.const 40
                i64.add
                i32.wrap_i64
                local.get 1
                i64.load offset=32
                i64.store
                local.get 1
                i32.const 24
                i32.add
                local.tee 1
                global.set 0
                local.get 1
                i32.const 8
                i32.add
                local.tee 1
                global.set 0
                i32.const 0
                return
              end
              local.get 1
              local.get 1
              i64.load offset=32
              i64.const -1
              i64.add
              i64.store
              local.get 1
              i32.const 8
              i32.sub
              local.tee 1
              global.set 0
              local.get 1
              i64.const 0x20004
              i64.store
              i32.const 0
              call $fib
              global.get 0
              local.set 1
              br_if 4
            end
            local.get 1
            i64.extend_i32_u
            i64.const 8
            i64.add
            i32.wrap_i64
            i64.load
            local.set 2
            local.get 1
            local.get 2
            i64.store offset=16
            local.get 1
            local.get 1
            i64.load offset=32
            i64.const -2
            i64.add
            i64.store
            local.get 1
            i32.const 8
            i32.sub
            local.tee 1
            global.set 0
            local.get 1
            i64.const 0x20005
            i64.store
            i32.const 0
            call $fib
            global.get 0
            local.set 1
            br_if 3
          end
          local.get 1
          i64.extend_i32_u
          i64.const 40
          i64.add
          i32.wrap_i64
          local.get 1
          i64.load offset=16
          local.get 1
          i64.extend_i32_u
          i64.const 8
          i64.add
          i32.wrap_i64
          i64.load
          i64.add
          i64.store
          local.get 1
          i32.const 24
          i32.add
          local.tee 1
          global.set 0
          local.get 1
          i32.const 8
          i32.add
          local.tee 1
          global.set 0
          i32.const 0
          return
        end
      end
      unreachable
    end
    i32.const 1)

  ;; fib(n) with the stack guard at guard, -1 when the stack runs out
  (func (export "fib") (type $ex)
    (global.set $sp (i32.const 4096))
    (i32.store offset=16 (i32.const 64) (local.get 1))
    (i64.store (i32.const 4104) (i64.extend_i32_u (local.get 0)))
    (if (result i32) (call $fib (i32.const 0))
      (then (i32.const -1))
      (else (i32.wrap_i64 (i64.load (i32.const 4112))))))
  ;; fib entered at a block as a resume would: its frame at 4072, the
  ;; argument n at 4104 and the result at 4112: at 4, past the first
  ;; call, whose result a is at 4080; at 5, past the second, a saved at 4088
  ;; and b at 4080.
  (func $resume (param $at i32) (param $n i64) (param $a i64) (param $b i64) (result i32)
    (i64.store (i32.const 4104) (local.get $n))
    (i64.store (i32.const 4088) (local.get $a))
    (i64.store (i32.const 4080) (select (local.get $b) (local.get $a) (i32.eq (local.get $at) (i32.const 5))))
    (global.set $sp (i32.const 4072))
    (if (result i32) (call $fib (local.get $at))
      (then (i32.const -1))
      (else (i32.wrap_i64 (i64.load (i32.const 4112))))))
  (func (export "resume4") (type $ex)
    (call $resume (i32.const 4) (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1)) (i64.const 0)))
  (func (export "resume5") (type $ex)
    (call $resume (i32.const 5) (i64.const 0) (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1))))
)
