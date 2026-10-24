;; A memory whose declared maximum is 65536 pages, which the runtime lets a
;; 32-bit memory grow to 65535 of: what an import of it is checked against
;; is the 65536 the module said (test/link/memmaxdriver.c).
(module (memory (export "m") 1 65536))
