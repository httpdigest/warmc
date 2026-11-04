;; Byte arrays passed to --native-module imports (test/native/bytes.c): an
;; (array i8) or (array (mut i8)) parameter is two of C's arguments, the
;; address of the array's bytes and their count, which C reads and writes
;; in place.  Arrays of every size, C's writes read back here, five arrays
;; among numbers (the last ones on the stack), and many collections between
;; calls, the array C reads moved by them or not.  Each result checked, the
;; program exiting with the number of the first that is wrong, or printing
;; "bytes: ok".
(module
  (type $ro (array i8))
  (type $bytes (array (mut i8)))
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (import "c" "nb_sum" (func $sum (param (ref $ro)) (result i64)))
  ;; the same C function at another type: a mutable array's bytes
  (import "c" "nb_sum" (func $msum (param (ref $bytes)) (result i64)))
  (import "c" "nb_count" (func $count (param (ref null $ro) i32) (result i32)))
  (import "c" "nb_fill" (func $fill (param (ref $bytes) i32)))
  (import "c" "nb_cmp" (func $cmp (param (ref $ro) (ref $ro)) (result i32)))
  (import "c" "nb_many" (func $many (param i32 (ref $ro) f64 (ref $ro) (ref $bytes) (ref $ro) i64 (ref $bytes))
                                   (result i64)))
  (import "c" "nb_addr" (func $addr (param (ref $ro)) (result i64)))
  (memory 1)
  (data (i32.const 16) "bytes: ok\n")
  (data $hw "hello, world")
  (data $hello "hello")
  (data $abc "abc")
  (data $abd "abd")

  (func $check (param $ok i32) (param $n i32)
    (if (i32.eqz (local.get $ok)) (then (call $proc_exit (local.get $n)))))

  (func (export "_start")
    (local $hw (ref null $ro)) (local $m (ref null $bytes)) (local $c (ref null $bytes)) (local $e (ref null $bytes))
    (local $keep (ref null $ro)) (local $g (ref null $bytes)) (local $i i32) (local $moved i32) (local $at i64)
    (call $check (i64.eq (call $sum (array.new $ro (i32.const 100) (i32.const 3))) (i64.const 300)) (i32.const 1))
    (local.set $hw (array.new_data $ro $hw (i32.const 0) (i32.const 12)))
    (call $check (i64.eq (call $sum (ref.as_non_null (local.get $hw))) (i64.const 1160)) (i32.const 2))
    (call $check (i32.eq (call $count (local.get $hw) (i32.const 108)) (i32.const 3)) (i32.const 3))
    ;; C's writes, read back
    (local.set $m (array.new_default $bytes (i32.const 10)))
    (call $fill (ref.as_non_null (local.get $m)) (i32.const 5))
    (call $check (i32.eq (array.get_u $bytes (local.get $m) (i32.const 0)) (i32.const 5)) (i32.const 4))
    (call $check (i32.eq (array.get_u $bytes (local.get $m) (i32.const 9)) (i32.const 14)) (i32.const 5))
    (call $check (i64.eq (call $msum (ref.as_non_null (local.get $m))) (i64.const 95)) (i32.const 6))
    ;; two arrays
    (call $check (i32.eq (call $cmp (ref.as_non_null (local.get $hw)) (array.new_data $ro $hello (i32.const 0) (i32.const 5)))
                         (i32.const 1)) (i32.const 7))
    (call $check (i32.eqz (call $cmp (ref.as_non_null (local.get $hw)) (ref.as_non_null (local.get $hw)))) (i32.const 8))
    (call $check (i32.eq (call $cmp (array.new_data $ro $abc (i32.const 0) (i32.const 3))
                                    (array.new_data $ro $abd (i32.const 0) (i32.const 3)))
                         (i32.const -1)) (i32.const 9))
    ;; five among numbers, C writing two of them
    (local.set $c (array.new_default $bytes (i32.const 5)))
    (local.set $e (array.new_default $bytes (i32.const 7)))
    (call $check (i64.eq (call $many (i32.const 1) (array.new_data $ro $abc (i32.const 0) (i32.const 3)) (f64.const 2.75)
                                     (array.new $ro (i32.const 2) (i32.const 4)) (ref.as_non_null (local.get $c))
                                     (array.new $ro (i32.const 3) (i32.const 2)) (i64.const 100000000)
                                     (ref.as_non_null (local.get $e)))
                         (i64.const 100706877)) (i32.const 10))
    (call $check (i32.eq (array.get_u $bytes (local.get $c) (i32.const 0)) (i32.const 42)) (i32.const 11))
    (call $check (i32.eq (array.get_u $bytes (local.get $e) (i32.const 6)) (i32.const 43)) (i32.const 12))
    ;; none at all
    (call $check (i64.eqz (call $sum (array.new $ro (i32.const 0) (i32.const 0)))) (i32.const 13))
    (call $check (i32.eqz (call $count (array.new $ro (i32.const 0) (i32.const 0)) (i32.const 0))) (i32.const 14))
    ;; collections between the calls: 128 MB of arrays C sums, and one kept
    ;; throughout, read each time where it is then
    (local.set $keep (array.new $ro (i32.const 7) (i32.const 1000)))
    (local.set $at (call $addr (ref.as_non_null (local.get $keep))))
    (loop $l
      (local.set $g (array.new $bytes (i32.and (local.get $i) (i32.const 255)) (i32.const 65536)))
      (call $check (i64.eq (call $msum (ref.as_non_null (local.get $g)))
                           (i64.mul (i64.const 65536) (i64.extend_i32_u (i32.and (local.get $i) (i32.const 255)))))
                   (i32.const 15))
      (call $check (i64.eq (call $sum (ref.as_non_null (local.get $keep))) (i64.const 7000)) (i32.const 16))
      (if (i64.ne (call $addr (ref.as_non_null (local.get $keep))) (local.get $at)) (then
        (local.set $moved (i32.add (local.get $moved) (i32.const 1)))
        (local.set $at (call $addr (ref.as_non_null (local.get $keep))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 2000))))
    (call $check (i32.eq (array.get_u $bytes (local.get $g) (i32.const 65535)) (i32.const 207)) (i32.const 17))
    (i32.store (i32.const 0) (i32.const 16))
    (i32.store (i32.const 4) (i32.const 10))
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8)))))
