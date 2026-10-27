;; array.new_data and array.init_data from a data segment that is active, in
;; a module whose only segment is: instantiation drops it, so nothing may be
;; taken from it -- an empty array from its offset 0 is made, and one byte
;; traps -- and the runtime has no table of segments to look one up in at all
;; (src/datapack.c writes one only for a passive segment something reads).
(module
  (type $a8 (array (mut i8)))
  (memory 1)
  (data $act (i32.const 16) "\01\02\03\04")
  (func (export "in_memory")
    (if (i32.ne (i32.load (i32.const 16)) (i32.const 0x04030201)) (then (unreachable))))
  (func (export "empty_new")
    (if (array.len (array.new_data $a8 $act (i32.const 0) (i32.const 0))) (then (unreachable))))
  (func (export "empty_init")
    (array.init_data $a8 $act (array.new_default $a8 (i32.const 4))
                     (i32.const 1) (i32.const 0) (i32.const 0)))
  (func (export "empty_memory_init")
    (memory.init $act (i32.const 64) (i32.const 0) (i32.const 0))
    (data.drop $act))
  (func (export "one_byte")
    (drop (array.new_data $a8 $act (i32.const 0) (i32.const 1)))))
