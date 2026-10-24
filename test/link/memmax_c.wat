;; An import of test/link/memmax_a.wat's memory that allows its declared
;; 65536 pages: linked.
(module (import "A" "m" (memory 1 65536)))
