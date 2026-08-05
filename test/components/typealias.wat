;; A type named through the fresh index an export or an import introduces.
;;
;; Every export and every import aliases its definition into a *new* index of
;; the same sort, and a later definition may name that index rather than the
;; original.  Nothing else in the corpus does: wit-smith writes each type once
;; and refers to it by the index it was declared at, so until this fixture the
;; two slot kinds were decoded, counted, and never followed -- `list<...>` of
;; one came out with an unresolved element, which is the wrong element size in
;; anything emitted from it.
;;
;; The lists are what makes it observable.  A list is a pointer and a length
;; whatever it holds, so its own size and alignment say nothing; what the
;; element is decides how far apart two of them sit, which is a byte
;; comparison in test/run_adapter.py.
(component
  (type $rec (record (field "a" u32) (field "b" u64)))
  (export "rec" (type $rec))
  ;; Type 1 is the index the export introduced, not the one $rec was declared
  ;; at.  A walk that stops at the export slot makes this a list of nothing.
  (type $via-export (list 1))

  (import "given" (type (eq $rec)))
  ;; And type 3 is the import's, with an `(eq)` bound that says which.
  (type $via-import (list 3))

  ;; A record holding both, so the element sizes decide the field offsets and
  ;; a wrong one moves every field after it.
  (type $pair (record (field "x" $via-export) (field "y" $via-import)
                      (field "z" u8)))
)
