;; A bytecode interpreter's dispatch loop, as clang emits test/cbench/interp
;; and wasm-tools shrink reduced it: a br_table whose default target is the
;; loop header it dispatches from.  The header's params -- the program counter
;; among them -- are live into the cases, and the register allocator's
;; liveness used to drop them from the header's live-out set when it reached
;; that self edge, which defines them: a value made in the header after their
;; last use there took their register, and every case that read the program
;; counter read an address.  -O0 trapped out of bounds and -O2 never stopped.
(module
  (memory (;0;) 2)
  (global (;0;) (mut i32) i32.const 6220)
  (func (export "dispatch_self_default") (result i32)
    (local i32 i32 i32 i32 i32 i64 i64 i64)
    block ;; label = @1
      i32.const 65676
      i32.load
      i32.eqz
      if ;; label = @2
        i32.const 0
        i32.const 1
        i32.store
        block (result i32) ;; label = @3
          global.get 0
          i32.const 544
          i32.sub
          local.tee 2
          global.set 0
          local.get 2
          i64.const 0
          i64.store offset=24
          local.get 2
          i64.const 0
          i64.store offset=16
          local.get 2
          i64.const 0
          i64.store offset=8
          local.get 2
          i64.const 0
          i64.store
          loop ;; label = @4
            i32.const 0
            local.set 0
            i32.const 0
            local.set 1
            loop ;; label = @5
              local.get 0
              local.tee 4
              i32.const 1
              i32.add
              local.tee 3
              local.set 0
              block ;; label = @6
                block ;; label = @7
                  block ;; label = @8
                    block ;; label = @9
                      block ;; label = @10
                        block ;; label = @11
                          block ;; label = @12
                            block ;; label = @13
                              block ;; label = @14
                                block ;; label = @15
                                  block ;; label = @16
                                    block ;; label = @17
                                      block ;; label = @18
                                        block ;; label = @19
                                          local.get 4
                                          i32.const 2
                                          i32.shl
                                          i32.const 65536
                                          i32.add
                                          i32.load
                                          br_table 0 (;@19;) 1 (;@18;) 2 (;@17;) 3 (;@16;) 4 (;@15;) 5 (;@14;) 6 (;@13;) 7 (;@12;) 8 (;@11;) 9 (;@10;) 10 (;@9;) 11 (;@8;) 12 (;@7;) 14 (;@5;)
                                        end
                                        local.get 2
                                        i32.const 32
                                        i32.add
                                        local.get 1
                                        i32.const 3
                                        i32.shl
                                        i32.add
                                        local.get 3
                                        i32.const 2
                                        i32.shl
                                        i32.const 65536
                                        i32.add
                                        i64.load32_s
                                        i64.store
                                        br 12 (;@6;)
                                      end
                                      local.get 1
                                      i32.const 3
                                      i32.shl
                                      local.get 2
                                      i32.add
                                      i32.const 16
                                      i32.add
                                      local.tee 0
                                      local.get 0
                                      i64.load
                                      local.get 2
                                      i32.const 32
                                      i32.add
                                      local.get 1
                                      i32.const 1
                                      i32.sub
                                      local.tee 1
                                      i32.const 3
                                      i32.shl
                                      i32.add
                                      i64.load
                                      i64.add
                                      i64.store
                                      local.get 3
                                      local.set 0
                                      br 12 (;@5;)
                                    end
                                    local.get 1
                                    i32.const 3
                                    i32.shl
                                    local.get 2
                                    i32.add
                                    i32.const 16
                                    i32.add
                                    local.tee 0
                                    local.get 0
                                    i64.load
                                    local.get 2
                                    i32.const 32
                                    i32.add
                                    local.get 1
                                    i32.const 1
                                    i32.sub
                                    local.tee 1
                                    i32.const 3
                                    i32.shl
                                    i32.add
                                    i64.load
                                    i64.sub
                                    i64.store
                                    local.get 3
                                    local.set 0
                                    br 11 (;@5;)
                                  end
                                  local.get 1
                                  i32.const 3
                                  i32.shl
                                  local.get 2
                                  i32.add
                                  i32.const 16
                                  i32.add
                                  local.tee 0
                                  local.get 0
                                  i64.load
                                  local.get 2
                                  i32.const 32
                                  i32.add
                                  local.get 1
                                  i32.const 1
                                  i32.sub
                                  local.tee 1
                                  i32.const 3
                                  i32.shl
                                  i32.add
                                  i64.load
                                  i64.mul
                                  i64.store
                                  local.get 3
                                  local.set 0
                                  br 10 (;@5;)
                                end
                                local.get 2
                                i32.const 32
                                i32.add
                                local.get 1
                                i32.const 3
                                i32.shl
                                i32.add
                                local.tee 0
                                local.get 0
                                i32.const 8
                                i32.sub
                                i64.load
                                i64.store
                                local.get 1
                                i32.const 1
                                i32.add
                                local.set 1
                                local.get 3
                                local.set 0
                                br 9 (;@5;)
                              end
                              local.get 2
                              i32.const 32
                              i32.add
                              local.get 1
                              i32.const 3
                              i32.shl
                              i32.add
                              local.tee 0
                              i32.const 8
                              i32.sub
                              local.tee 4
                              i64.load
                              local.set 7
                              local.get 4
                              local.get 0
                              i32.const 16
                              i32.sub
                              local.tee 0
                              i64.load
                              i64.store
                              local.get 0
                              local.get 7
                              i64.store
                              local.get 3
                              local.set 0
                              br 8 (;@5;)
                            end
                            local.get 4
                            i32.const 2
                            i32.add
                            local.set 0
                            local.get 2
                            i32.const 32
                            i32.add
                            local.get 1
                            i32.const 1
                            i32.sub
                            local.tee 1
                            i32.const 3
                            i32.shl
                            i32.add
                            i64.load
                            i64.eqz
                            br_if 7 (;@5;)
                            local.get 3
                            i32.const 2
                            i32.shl
                            i32.const 65536
                            i32.add
                            i32.load
                            local.set 0
                            br 7 (;@5;)
                          end
                          local.get 1
                          i32.const 3
                          i32.shl
                          local.get 2
                          i32.add
                          i32.const 24
                          i32.add
                          local.tee 0
                          local.get 0
                          i64.load
                          i64.const 1
                          i64.sub
                          i64.store
                          local.get 3
                          local.set 0
                          br 6 (;@5;)
                        end
                        local.get 2
                        i32.const 32
                        i32.add
                        local.get 1
                        i32.const 3
                        i32.shl
                        i32.add
                        local.get 2
                        local.get 3
                        i32.const 2
                        i32.shl
                        i32.const 65536
                        i32.add
                        i32.load
                        i32.const 3
                        i32.shl
                        i32.add
                        i64.load
                        i64.store
                        br 4 (;@6;)
                      end
                      local.get 2
                      local.get 3
                      i32.const 2
                      i32.shl
                      i32.const 65536
                      i32.add
                      i32.load
                      i32.const 3
                      i32.shl
                      i32.add
                      local.get 2
                      i32.const 32
                      i32.add
                      local.get 1
                      i32.const 1
                      i32.sub
                      local.tee 1
                      i32.const 3
                      i32.shl
                      i32.add
                      i64.load
                      i64.store
                      local.get 4
                      i32.const 2
                      i32.add
                      local.set 0
                      br 4 (;@5;)
                    end
                    local.get 1
                    i32.const 3
                    i32.shl
                    local.get 2
                    i32.add
                    i32.const 16
                    i32.add
                    local.tee 0
                    local.get 0
                    i64.load
                    local.get 2
                    i32.const 32
                    i32.add
                    local.get 1
                    i32.const 1
                    i32.sub
                    local.tee 1
                    i32.const 3
                    i32.shl
                    i32.add
                    i64.load
                    i64.xor
                    i64.store
                    local.get 3
                    local.set 0
                    br 3 (;@5;)
                  end
                  local.get 1
                  i32.const 3
                  i32.shl
                  local.get 2
                  i32.add
                  i32.const 16
                  i32.add
                  local.tee 0
                  local.get 0
                  i64.load
                  local.get 2
                  i32.const 32
                  i32.add
                  local.get 1
                  i32.const 1
                  i32.sub
                  local.tee 1
                  i32.const 3
                  i32.shl
                  i32.add
                  i64.load
                  i64.shr_u
                  i64.store
                  local.get 3
                  local.set 0
                  br 2 (;@5;)
                end
                local.get 1
                i32.const 3
                i32.shl
                local.get 2
                i32.add
                i32.const 24
                i32.add
                i64.load
                local.get 6
                i64.const 1000003
                i64.mul
                local.get 5
                i64.add
                i64.add
                local.set 6
                local.get 5
                i64.const 1
                i64.add
                local.tee 5
                i64.const 4
                i64.ne
                br_if 2 (;@4;)
                local.get 2
                i32.const 544
                i32.add
                global.set 0
                local.get 6
                i32.wrap_i64
                local.tee 0
                i32.const 20
                i32.shr_u
                i32.const 63
                i32.and
                local.get 0
                i32.const 127
                i32.and
                i32.add
                br 3 (;@3;)
              end
              local.get 1
              i32.const 1
              i32.add
              local.set 1
              local.get 4
              i32.const 2
              i32.add
              local.set 0
              br 0 (;@5;)
            end
            unreachable
          end
          unreachable
        end
        return
      end
      unreachable
    end
    i32.const -1
  )
  (data (;0;) (i32.const 65544) "\09")
  (data (;1;) (i32.const 65556) "\c0\c6-\00\09\00\00\00\01\00\00\00\08")
  (data (;2;) (i32.const 65580) "\1f\00\00\00\03\00\00\00\08\00\00\00\01\00\00\00\01\00\00\00\08")
  (data (;3;) (i32.const 65612) "\07\00\00\00\0b\00\00\00\0a\00\00\00\09\00\00\00\00\00\00\00\08\00\00\00\01\00\00\00\07\00\00\00\04\00\00\00\09\00\00\00\01\00\00\00\06\00\00\00\08\00\00\00\08\00\00\00\00\00\00\00\0c")
)
