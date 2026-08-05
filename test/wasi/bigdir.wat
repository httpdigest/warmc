;; fd_readdir over a directory far bigger than the buffer.
;;
;; [[readdir]] pins the resume logic; this pins its cost.  Every call used to
;; dup the descriptor, fdopendir it, rewinddir it and then skip `cookie`
;; entries from the top before writing anything, so walking a directory of N
;; entries in bites of B cost O(N^2/B) readdir steps and N openings of the
;; stream.  The handle now lives on the fd and a call that asks for the cookie
;; the stream already sits at just carries on; a truncated entry is put back
;; with seekdir so the next call resumes on it rather than rescanning.
;;
;; 200 names in 64-byte bites is ~200 calls, which is where the old shape got
;; expensive.  What is printed is the entry count and the total name length,
;; as in readdir.wat -- the order two engines hand back the names in is not
;; something either has to agree on.

(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_readdir"
    (func $readdir (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close"
    (func $close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 2)

  (data (i32.const 0x100) "b000")

  (func $puts (param $p i32) (param $n i32)
    (i32.store (i32.const 0x200) (local.get $p))
    (i32.store (i32.const 0x204) (local.get $n))
    (drop (call $write (i32.const 1) (i32.const 0x200) (i32.const 1)
                       (i32.const 0x20c))))

  (func $putn (param $v i32)
    (local $p i32)
    (local.set $p (i32.const 0x260))
    (i32.store8 (local.get $p) (i32.const 10))
    (loop $d
      (local.set $p (i32.sub (local.get $p) (i32.const 1)))
      (i32.store8 (local.get $p)
        (i32.add (i32.const 48) (i32.rem_u (local.get $v) (i32.const 10))))
      (local.set $v (i32.div_u (local.get $v) (i32.const 10)))
      (br_if $d (local.get $v)))
    (call $puts (local.get $p) (i32.sub (i32.const 0x261) (local.get $p))))

  (func $die (param $code i32) (call $exit (local.get $code)) (unreachable))

  (func (export "_start")
    (local $i i32) (local $e i32)
    (local $cookie i64) (local $got i32) (local $at i32)
    (local $namlen i32) (local $entries i32) (local $bytes i32)

    ;; b000 .. b199, each created and closed at once.
    (local.set $i (i32.const 0))
    (loop $mk
      (i32.store8 (i32.const 0x101)
        (i32.add (i32.const 48) (i32.div_u (local.get $i) (i32.const 100))))
      (i32.store8 (i32.const 0x102)
        (i32.add (i32.const 48)
                 (i32.rem_u (i32.div_u (local.get $i) (i32.const 10))
                            (i32.const 10))))
      (i32.store8 (i32.const 0x103)
        (i32.add (i32.const 48) (i32.rem_u (local.get $i) (i32.const 10))))
      (local.set $e (call $open (i32.const 3) (i32.const 0) (i32.const 0x100)
                          (i32.const 4) (i32.const 1)
                          (i64.const 78) (i64.const 78)
                          (i32.const 0) (i32.const 0x304)))
      (if (local.get $e) (then (call $die (i32.const 20))))
      (drop (call $close (i32.load (i32.const 0x304))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $i) (i32.const 200))))

    ;; Walk it all back in 64-byte bites, counting only whole entries.
    (block $done
      (loop $call
        (if (call $readdir (i32.const 3) (i32.const 0x800) (i32.const 64)
                  (local.get $cookie) (i32.const 0x300))
          (then (call $die (i32.const 21))))
        (local.set $got (i32.load (i32.const 0x300)))
        (br_if $done (i32.lt_u (local.get $got) (i32.const 24)))
        (local.set $at (i32.const 0))
        (block $chunk
          (loop $walk
            (br_if $chunk
              (i32.gt_u (i32.add (local.get $at) (i32.const 24))
                        (local.get $got)))
            (local.set $namlen
              (i32.load (i32.add (i32.const 0x800)
                                 (i32.add (local.get $at) (i32.const 16)))))
            (br_if $chunk
              (i32.gt_u (i32.add (local.get $at)
                                 (i32.add (i32.const 24) (local.get $namlen)))
                        (local.get $got)))
            (local.set $cookie
              (i64.load (i32.add (i32.const 0x800) (local.get $at))))
            (local.set $entries (i32.add (local.get $entries) (i32.const 1)))
            (local.set $bytes (i32.add (local.get $bytes) (local.get $namlen)))
            (local.set $at
              (i32.add (local.get $at)
                       (i32.add (i32.const 24) (local.get $namlen))))
            (br $walk)))
        (br_if $done (i32.eqz (local.get $at)))
        (br $call)))

    (call $putn (local.get $entries))
    (call $putn (local.get $bytes))))
