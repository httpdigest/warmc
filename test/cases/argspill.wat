;; A function's prologue places its register arguments: some into the
;; registers the allocator gave them, some into spill slots.  Those moves read
;; the incoming argument registers, so one that writes an argument register
;; another argument has yet to be read from has to wait for a parallel copy.
;; A spill-slot store is such a reader too, and it was not counted as one: here
;; the fourth argument was moved into the fifth's register before the fifth was
;; stored to its slot, so the buffer pointer became the prefix length (0) and
;; the digits were read from address 0.
;;
;; $pad_integral is Rust's core::fmt::Formatter::pad_integral, delta-reduced
;; from rsbench/fmtfloat with the shape that put its arguments where they
;; clashed at -O0 kept as it was.  The write it ends in records the pointer and
;; length it was given, and the export reads the bytes back.

(module
  (type $write2 (func (param i32 i32) (result i32)))
  (type $write3 (func (param i32 i32 i32) (result i32)))
  (memory 1)
  (table 2 2 funcref)
  (elem (i32.const 0) func $write_str $write_char)
  (global $ptr (mut i32) (i32.const -1))
  (global $len (mut i32) (i32.const -1))
  (data (i32.const 512) "9950")
  (data (i32.const 1024) "\00\00\00\00\00\08\00\00\20\00\00\60\00\00\00\00")
  (data (i32.const 2060) "\00\00\00\00\01\00\00\00")

  (func $write_str (type $write3) (param i32 i32 i32) (result i32)
    (global.set $ptr (local.get 1))
    (global.set $len (local.get 2))
    (i32.const 0))
  (func $write_char (type $write2) (param i32 i32) (result i32)
    (i32.const 0))
  (func $write_prefix (param i32 i32 i32 i32 i32) (result i32)
    (i32.const 0))

  (func (export "digits_written") (result i32)
    (drop (call $pad_integral (i32.const 1024) (i32.const 1) (i32.const 1)
                              (i32.const 0) (i32.const 512) (i32.const 4)))
    (i32.add (i32.load (global.get $ptr)) (global.get $len)))

  (func $pad_integral (param i32 i32 i32 i32 i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i64)
    block
      block
        local.get 1
        br_if 0
      end
      i32.const 43
      local.set 6
    end
    block
      block
        i32.const 8388608
        br_if 0
        i32.const 0
        local.set 2
        br 1
      end
      block
        local.get 2
        local.set 1
        local.get 3
        local.set 1
      end
      local.get 1
      local.get 6
      i32.add
      local.set 6
    end
    block
      block
        local.get 0
        br_if 0
        block
          block
            block
              local.get 7
              br_if 0
              local.get 9
              local.set 10
              block
                block
                  block
                    i32.const 29
                    br_table 2 0 1 0 2
                  end
                end
                local.get 9
                i32.const 65534
                i32.shr_u
                local.set 10
              end
              local.get 7
              local.set 0
              loop
              end
            end
            local.get 0
            local.get 0
            i32.const -1612709888
            local.get 2
            local.get 3
            call $write_prefix
            br_if 3
            local.get 9
            local.get 6
            i32.and
            local.set 6
            loop
              local.get 1
              local.get 11
              i32.const 48
              call_indirect (type $write2)
              br 4
            end
          end
          local.get 0
          local.get 4
          i32.and
          local.set 10
          loop
            block
              i32.const 65535
              return
            end
          end
        end
        i32.const 0
        return
      end
      i32.const 1
      local.get 0
      local.get 8
      local.get 2
      local.get 3
      call $write_prefix
      br_if 0
      local.get 1
      local.get 4
      local.get 5
      local.get 0
      i32.load offset=12
      call_indirect (type $write3)
      local.set 7
    end
    local.get 7))
