;; A data segment named from a *constant expression* rather than from code.
;;
;; The data count section exists for the code section alone: it is what lets a
;; `memory.init` name a segment the decoder has not read yet.  `array.new_data`
;; in a global's initialiser names one without the code section naming
;; anything, and the module printed from array.wast has no data count section
;; -- so emitting one here is three bytes nothing asked for.
;;
;; Its own file, and not a fourth module field in corners.wat, because that
;; file has a `data.drop` in it: the flag would be set by the code anyway and
;; the rule this pins would be masked.  It was, on the first attempt, and the
;; ablation that should have failed reported nothing.
(module
  (type $bvec (array i8))
  (global (ref $bvec) i32.const 1 i32.const 3 array.new_data $bvec $d)
  (data $d "\00\01\02\03\04")
)
