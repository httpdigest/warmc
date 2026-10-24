;; An import of test/link/memmax_a.wat's memory that allows at most 65535
;; pages, fewer than its declared 65536: refused.
(module (import "A" "m" (memory 1 65535)))
