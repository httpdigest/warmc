;; An exception thrown from one module, through a second that has no tag of
;; its own and no type of the collector's, to a third that catches it
;; (test/link/exnthrudriver.c): the second is an object of its own (-c),
;; whose frames another module's exception may walk, so it keeps its unwind
;; records, as a program's only module with neither has none to keep.
(module
  (tag (export "t") (param i32))
  (func (export "thrower") (param i32) (throw 0 (i32.add (local.get 0) (i32.const 1)))))
