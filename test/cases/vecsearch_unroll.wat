;; LLVM's unrolled form of a byte search, the shape TinyGo emits for a
;; memchr loop at -opt=2: the counter steps by the unroll count, each of the
;; unrolled tests reads one byte at (base + i) + C + k and leaves the loop with
;; its own index.  warmc vectorises it sixteen bytes at a time and enters the
;; scalar loop again at the block that holds the match (vs_match_unrolled in
;; src/opt.c).  The loops are written out in LLVM's shape, one per unroll
;; count and latch, and the base comes from a mutable global so that nothing
;; folds it into the addresses.
;;
;; What there is to get wrong, and the export that would show it:
;;   * which byte of the block matched: every driver plants the match at
;;     every position in turn, and once nowhere, into a polynomial checksum;
;;   * the unroll count: by 2 (s2zero), 4 and 8, and by 3 (s3zero), which
;;     does not divide sixteen and has to stay scalar;
;;   * the latch: up to zero from -N (the -opt=2 shape), and up to a bound
;;     with != (s4ne, s8ne) or < (s4lt);
;;   * the byte: zero (eqz) and 0x41 (i32.eq);
;;   * the end of memory (s4atend): the buffer's last bytes are memory's
;;     last, so a vector block past them must fall back to the scalar loop.
(module
  (memory 2)
  (global $base (mut i32) (i32.const 4099))
  (func $f_s4zero (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const -256
    local.set $i
    block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.const 256
      i32.add
      i32.load8_u
      i32.eqz
      if
        local.get $i
        i32.const 256
        i32.add
        return
      end
      local.get $p
      i32.const 257
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $p
      i32.const 258
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e2
      local.get $p
      i32.const 259
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e3
      local.get $i
      i32.const 4
      i32.add
      local.tee $i
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 257
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 258
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 259
    i32.add
    return
  )
  (func $f_s2zero (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const -96
    local.set $i
    block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.const 96
      i32.add
      i32.load8_u
      i32.eqz
      if
        local.get $i
        i32.const 96
        i32.add
        return
      end
      local.get $p
      i32.const 97
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $i
      i32.const 2
      i32.add
      local.tee $i
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 97
    i32.add
    return
  )
  (func $f_s8zero (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const -128
    local.set $i
    block $e7 block $e6 block $e5 block $e4 block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.const 128
      i32.add
      i32.load8_u
      i32.eqz
      if
        local.get $i
        i32.const 128
        i32.add
        return
      end
      local.get $p
      i32.const 129
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $p
      i32.const 130
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e2
      local.get $p
      i32.const 131
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e3
      local.get $p
      i32.const 132
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e4
      local.get $p
      i32.const 133
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e5
      local.get $p
      i32.const 134
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e6
      local.get $p
      i32.const 135
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e7
      local.get $i
      i32.const 8
      i32.add
      local.tee $i
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 129
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 130
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 131
    i32.add
    return
    end ;; $e4
    local.get $i
    i32.const 132
    i32.add
    return
    end ;; $e5
    local.get $i
    i32.const 133
    i32.add
    return
    end ;; $e6
    local.get $i
    i32.const 134
    i32.add
    return
    end ;; $e7
    local.get $i
    i32.const 135
    i32.add
    return
  )
  (func $f_s3zero (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const -96
    local.set $i
    block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.const 96
      i32.add
      i32.load8_u
      i32.eqz
      if
        local.get $i
        i32.const 96
        i32.add
        return
      end
      local.get $p
      i32.const 97
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $p
      i32.const 98
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e2
      local.get $i
      i32.const 3
      i32.add
      local.tee $i
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 97
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 98
    i32.add
    return
  )
  (func $f_s4ne (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const 0
    local.set $i
    block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.load8_u
      i32.const 65
      i32.eq
      if
        local.get $i
        return
      end
      local.get $p
      i32.const 1
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e1
      local.get $p
      i32.const 2
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e2
      local.get $p
      i32.const 3
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e3
      local.get $i
      i32.const 4
      i32.add
      local.tee $i
      i32.const 160
      i32.ne
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 1
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 2
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 3
    i32.add
    return
  )
  (func $f_s4lt (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const 0
    local.set $i
    block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.load8_u
      i32.const 65
      i32.eq
      if
        local.get $i
        return
      end
      local.get $p
      i32.const 1
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e1
      local.get $p
      i32.const 2
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e2
      local.get $p
      i32.const 3
      i32.add
      i32.load8_u
      i32.const 65
      i32.eq
      br_if $e3
      local.get $i
      i32.const 4
      i32.add
      local.tee $i
      i32.const 200
      i32.lt_u
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 1
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 2
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 3
    i32.add
    return
  )
  (func $f_s8ne (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const 0
    local.set $i
    block $e7 block $e6 block $e5 block $e4 block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.load8_u
      i32.eqz
      if
        local.get $i
        return
      end
      local.get $p
      i32.const 1
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $p
      i32.const 2
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e2
      local.get $p
      i32.const 3
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e3
      local.get $p
      i32.const 4
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e4
      local.get $p
      i32.const 5
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e5
      local.get $p
      i32.const 6
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e6
      local.get $p
      i32.const 7
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e7
      local.get $i
      i32.const 8
      i32.add
      local.tee $i
      i32.const 64
      i32.ne
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 1
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 2
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 3
    i32.add
    return
    end ;; $e4
    local.get $i
    i32.const 4
    i32.add
    return
    end ;; $e5
    local.get $i
    i32.const 5
    i32.add
    return
    end ;; $e6
    local.get $i
    i32.const 6
    i32.add
    return
    end ;; $e7
    local.get $i
    i32.const 7
    i32.add
    return
  )
  (func (export "s4zero") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 256)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 256)))
      (if (i32.lt_u (local.get $m) (i32.const 256))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s4zero (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 256))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s2zero") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 96)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 96)))
      (if (i32.lt_u (local.get $m) (i32.const 96))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s2zero (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 96))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s8zero") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 128)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 128)))
      (if (i32.lt_u (local.get $m) (i32.const 128))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s8zero (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 128))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s3zero") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 96)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 96)))
      (if (i32.lt_u (local.get $m) (i32.const 96))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s3zero (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 96))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s4ne") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 160)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 160)))
      (if (i32.lt_u (local.get $m) (i32.const 160))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 65))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s4ne (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 160))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s4lt") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 200)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 200)))
      (if (i32.lt_u (local.get $m) (i32.const 200))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 65))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s4lt (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 200))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func (export "s8ne") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    global.get $base
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 64)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 64)))
      (if (i32.lt_u (local.get $m) (i32.const 64))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_s8ne (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 64))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
  (func $f_atend (param $base i32) (result i32)
    (local $i i32) (local $p i32)
    i32.const -40
    local.set $i
    block $e3 block $e2 block $e1
    loop $l
      local.get $base
      local.get $i
      i32.add
      local.tee $p
      i32.const 40
      i32.add
      i32.load8_u
      i32.eqz
      if
        local.get $i
        i32.const 40
        i32.add
        return
      end
      local.get $p
      i32.const 41
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e1
      local.get $p
      i32.const 42
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e2
      local.get $p
      i32.const 43
      i32.add
      i32.load8_u
      i32.eqz
      br_if $e3
      local.get $i
      i32.const 4
      i32.add
      local.tee $i
      br_if $l
    end
    i32.const -1
    return
    end ;; $e1
    local.get $i
    i32.const 41
    i32.add
    return
    end ;; $e2
    local.get $i
    i32.const 42
    i32.add
    return
    end ;; $e3
    local.get $i
    i32.const 43
    i32.add
    return
  )
  (func (export "s4atend") (result i32)
    (local $j i32) (local $m i32) (local $h i32) (local $b i32)
    (i32.sub (i32.mul (memory.size) (i32.const 65536)) (i32.const 40))
    local.set $b
    ;; nothing in the buffer equals the byte searched for
    (block $done (loop $f
      (br_if $done (i32.ge_u (local.get $j) (i32.const 40)))
      (i32.store8 (i32.add (local.get $b) (local.get $j))
                  (i32.add (i32.const 1) (i32.and (local.get $j) (i32.const 0x3f))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $f)))
    ;; m = N is the search that finds nothing
    (block $out (loop $each
      (br_if $out (i32.gt_u (local.get $m) (i32.const 40)))
      (if (i32.lt_u (local.get $m) (i32.const 40))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m)) (i32.const 0))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (call $f_atend (local.get $b))))
      (if (i32.lt_u (local.get $m) (i32.const 40))
        (then (i32.store8 (i32.add (local.get $b) (local.get $m))
                          (i32.add (i32.const 1) (i32.and (local.get $m) (i32.const 0x3f))))))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br $each)))
    local.get $h)
)
