;; Go's resume dispatch pruned (opt.c pass_go_resume) in functions of every
;; number of blocks from 4 to 27: f0 to f23, fk with a br_table of 2 + k
;; cases on its first parameter -- 0 and 1 the entry, which adds k + 1 to the
;; word at 136; each other case c a block that stores 1000 (k + 1) + c at
;; 128; the default unreachable.  None stores a return address, so nothing
;; resumes it and only 0 and 1 can arrive: every other case traps, and the
;; pass makes the block they go to.  Making it moves the function's blocks
;; when the function has as many as it has room for -- f4's 8 and f12's 16
;; -- and the dispatch is then the one in the blocks where they are now:
;; taken where they were, it kept its own edges with the pruned table's
;; after them, every case as it had been and the two past its default the
;; entry's.
;;
;; at(k, i) enters fk at i as a resume would and answers 100000 times the
;; word at 136 plus the word at 128.  At 0 and 1, the entry; at 2, a case
;; that is pruned where the pass runs and the wasm's own block at -O0; at
;; k + 3, past the table: the default, a trap at every level.
;;
;; stats -O2: go_resume_pruned=299
;;
;; run: at 0 0 => i32:100000
;; run: at 0 1 => i32:100000
;; run: at 0 3 => trap unreachable
;; run: at 1 0 => i32:200000
;; run: at 1 1 => i32:200000
;; run: at 1 2 => trap unreachable
;; run -O0: at 1 2 => i32:2002
;; run: at 1 4 => trap unreachable
;; run: at 2 0 => i32:300000
;; run: at 2 1 => i32:300000
;; run: at 2 2 => trap unreachable
;; run -O0: at 2 2 => i32:3002
;; run: at 2 5 => trap unreachable
;; run: at 3 0 => i32:400000
;; run: at 3 1 => i32:400000
;; run: at 3 2 => trap unreachable
;; run -O0: at 3 2 => i32:4002
;; run: at 3 6 => trap unreachable
;; run: at 4 0 => i32:500000
;; run: at 4 1 => i32:500000
;; run: at 4 2 => trap unreachable
;; run -O0: at 4 2 => i32:5002
;; run: at 4 7 => trap unreachable
;; run: at 5 0 => i32:600000
;; run: at 5 1 => i32:600000
;; run: at 5 2 => trap unreachable
;; run -O0: at 5 2 => i32:6002
;; run: at 5 8 => trap unreachable
;; run: at 6 0 => i32:700000
;; run: at 6 1 => i32:700000
;; run: at 6 2 => trap unreachable
;; run -O0: at 6 2 => i32:7002
;; run: at 6 9 => trap unreachable
;; run: at 7 0 => i32:800000
;; run: at 7 1 => i32:800000
;; run: at 7 2 => trap unreachable
;; run -O0: at 7 2 => i32:8002
;; run: at 7 10 => trap unreachable
;; run: at 8 0 => i32:900000
;; run: at 8 1 => i32:900000
;; run: at 8 2 => trap unreachable
;; run -O0: at 8 2 => i32:9002
;; run: at 8 11 => trap unreachable
;; run: at 9 0 => i32:1000000
;; run: at 9 1 => i32:1000000
;; run: at 9 2 => trap unreachable
;; run -O0: at 9 2 => i32:10002
;; run: at 9 12 => trap unreachable
;; run: at 10 0 => i32:1100000
;; run: at 10 1 => i32:1100000
;; run: at 10 2 => trap unreachable
;; run -O0: at 10 2 => i32:11002
;; run: at 10 13 => trap unreachable
;; run: at 11 0 => i32:1200000
;; run: at 11 1 => i32:1200000
;; run: at 11 2 => trap unreachable
;; run -O0: at 11 2 => i32:12002
;; run: at 11 14 => trap unreachable
;; run: at 12 0 => i32:1300000
;; run: at 12 1 => i32:1300000
;; run: at 12 2 => trap unreachable
;; run -O0: at 12 2 => i32:13002
;; run: at 12 15 => trap unreachable
;; run: at 13 0 => i32:1400000
;; run: at 13 1 => i32:1400000
;; run: at 13 2 => trap unreachable
;; run -O0: at 13 2 => i32:14002
;; run: at 13 16 => trap unreachable
;; run: at 14 0 => i32:1500000
;; run: at 14 1 => i32:1500000
;; run: at 14 2 => trap unreachable
;; run -O0: at 14 2 => i32:15002
;; run: at 14 17 => trap unreachable
;; run: at 15 0 => i32:1600000
;; run: at 15 1 => i32:1600000
;; run: at 15 2 => trap unreachable
;; run -O0: at 15 2 => i32:16002
;; run: at 15 18 => trap unreachable
;; run: at 16 0 => i32:1700000
;; run: at 16 1 => i32:1700000
;; run: at 16 2 => trap unreachable
;; run -O0: at 16 2 => i32:17002
;; run: at 16 19 => trap unreachable
;; run: at 17 0 => i32:1800000
;; run: at 17 1 => i32:1800000
;; run: at 17 2 => trap unreachable
;; run -O0: at 17 2 => i32:18002
;; run: at 17 20 => trap unreachable
;; run: at 18 0 => i32:1900000
;; run: at 18 1 => i32:1900000
;; run: at 18 2 => trap unreachable
;; run -O0: at 18 2 => i32:19002
;; run: at 18 21 => trap unreachable
;; run: at 19 0 => i32:2000000
;; run: at 19 1 => i32:2000000
;; run: at 19 2 => trap unreachable
;; run -O0: at 19 2 => i32:20002
;; run: at 19 22 => trap unreachable
;; run: at 20 0 => i32:2100000
;; run: at 20 1 => i32:2100000
;; run: at 20 2 => trap unreachable
;; run -O0: at 20 2 => i32:21002
;; run: at 20 23 => trap unreachable
;; run: at 21 0 => i32:2200000
;; run: at 21 1 => i32:2200000
;; run: at 21 2 => trap unreachable
;; run -O0: at 21 2 => i32:22002
;; run: at 21 24 => trap unreachable
;; run: at 22 0 => i32:2300000
;; run: at 22 1 => i32:2300000
;; run: at 22 2 => trap unreachable
;; run -O0: at 22 2 => i32:23002
;; run: at 22 25 => trap unreachable
;; run: at 23 0 => i32:2400000
;; run: at 23 1 => i32:2400000
;; run: at 23 2 => trap unreachable
;; run -O0: at 23 2 => i32:24002
;; run: at 23 26 => trap unreachable
(module
  (@custom "go:buildid" "warmc-test")
  (type $go (func (param i32) (result i32)))
  (type $ex (func (param i32 i32) (result i32)))
  (global $sp (mut i32) (i32.const 4096))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 64))
  (memory 1)
  (table 24 funcref)
  (elem (i32.const 0) $f0 $f1 $f2 $f3 $f4 $f5 $f6 $f7 $f8 $f9 $f10 $f11 $f12 $f13 $f14 $f15 $f16 $f17 $f18 $f19 $f20 $f21 $f22 $f23)
  (func $f0 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    local.get 0
    br_table 0 0 1
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 1)))
    i32.const 0
    return
    end
    unreachable)

  (func $f1 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    local.get 0
    br_table 0 0 1 2
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 2)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 2002))
    i32.const 0
    return
    end
    unreachable)

  (func $f2 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 3)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 3002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 3003))
    i32.const 0
    return
    end
    unreachable)

  (func $f3 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 4)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 4002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 4003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 4004))
    i32.const 0
    return
    end
    unreachable)

  (func $f4 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 5)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 5002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 5003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 5004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 5005))
    i32.const 0
    return
    end
    unreachable)

  (func $f5 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 6)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 6002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 6003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 6004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 6005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 6006))
    i32.const 0
    return
    end
    unreachable)

  (func $f6 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 7)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 7007))
    i32.const 0
    return
    end
    unreachable)

  (func $f7 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 8)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 8008))
    i32.const 0
    return
    end
    unreachable)

  (func $f8 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 9)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 9009))
    i32.const 0
    return
    end
    unreachable)

  (func $f9 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 10)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 10010))
    i32.const 0
    return
    end
    unreachable)

  (func $f10 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 11)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 11011))
    i32.const 0
    return
    end
    unreachable)

  (func $f11 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 12)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 12012))
    i32.const 0
    return
    end
    unreachable)

  (func $f12 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 13)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 13013))
    i32.const 0
    return
    end
    unreachable)

  (func $f13 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 14)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 14014))
    i32.const 0
    return
    end
    unreachable)

  (func $f14 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 15)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 15015))
    i32.const 0
    return
    end
    unreachable)

  (func $f15 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 16)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 16016))
    i32.const 0
    return
    end
    unreachable)

  (func $f16 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 17)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 17017))
    i32.const 0
    return
    end
    unreachable)

  (func $f17 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 18)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 18018))
    i32.const 0
    return
    end
    unreachable)

  (func $f18 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 19)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 19019))
    i32.const 0
    return
    end
    unreachable)

  (func $f19 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 20)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20019))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 20020))
    i32.const 0
    return
    end
    unreachable)

  (func $f20 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 21)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21019))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21020))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 21021))
    i32.const 0
    return
    end
    unreachable)

  (func $f21 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 22)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22019))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22020))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22021))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 22022))
    i32.const 0
    return
    end
    unreachable)

  (func $f22 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 23)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23019))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23020))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23021))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23022))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 23023))
    i32.const 0
    return
    end
    unreachable)

  (func $f23 (type $go) (local i32)
    global.get 0
    local.set 1
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    block
    local.get 0
    br_table 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
    end
    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const 24)))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24002))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24003))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24004))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24005))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24006))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24007))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24008))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24009))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24010))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24011))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24012))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24013))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24014))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24015))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24016))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24017))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24018))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24019))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24020))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24021))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24022))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24023))
    i32.const 0
    return
    end
    (i32.store (i32.const 128) (i32.const 24024))
    i32.const 0
    return
    end
    unreachable)

  (func (export "at") (type $ex)
    (i32.store (i32.const 128) (i32.const 0))
    (i32.store (i32.const 136) (i32.const 0))
    (drop (call_indirect (type $go) (local.get 1) (local.get 0)))
    (i32.add (i32.mul (i32.load (i32.const 136)) (i32.const 100000))
             (i32.load (i32.const 128))))
)
