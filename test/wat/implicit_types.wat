;; Which type an inline signature stands for, which run_wat.py holds byte for
;; byte to wasm-tools: the first function type that is final, has no
;; supertype and is in no (rec ...), or else a new one at the end of the
;; module, which later signatures then share.  A rec group of one would do as
;; well as no group -- the reference interpreter takes it -- and the two
;; modules are equivalent; this is wasm-tools' choice.  A type that is not
;; final, where wasm-tools is wrong, is in gen_reject_cases.py instead, by the
;; verdict it gets.
(module
  (type $plain (func (param i32)))
  (type $final (sub final (func (param i64))))
  (rec (type $one (func (param f32))))
  (rec (type $pair (func (param f64))) (type (struct)))
  (func (param i32))
  (func (param i64))
  (func (param f32))
  (func (param f64))
  (func (param f32))
  (func (param i32) (result i32)
    (block (param i32) (result i32))))
