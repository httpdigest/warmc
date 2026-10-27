;; Data segments into an imported memory, which the runtime does not make
;; and which need not be zero: their zeros have to be written, and the bytes
;; between them left as they were -- where a memory the module defines takes
;; its segments as one compressed image (src/datapack.c), whose runs of zeros
;; are skipped, and whose gaps are zeros it writes.  A run of six zeros, of
;; twenty, gaps of 31 bytes and of 2, and a copy of earlier bytes
;; (test/link/dpmemdriver.c).
(module
  (import "A" "m" (memory 1))
  (data (i32.const 100) "\01\02\00\00\00\00\00\00\03")
  (data (i32.const 140) "\04")
  (data (i32.const 300) "\07\08")
  (data (i32.const 304) "\09")
  (data (i32.const 400) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
  (data (i32.const 500) "\0a\0b\0c\0d\0e\0f\10\11\12\13\14\15\16\17\18\19\1a\1b\1c\1d")
  (data (i32.const 520) "\0a\0b\0c\0d\0e\0f\10\11\12\13\14\15\16\17\18\19\1a\1b\1c\1d"))
