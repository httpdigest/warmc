(module
  ;; Counterexamples for the relaxed call barrier.
  ;;
  ;; warmc keeps what it knows about the caller's frame across a call whose
  ;; callee provably writes only its own -- every store at `global.get $sp - k`
  ;; with the whole access at or below the pointer.  Each `must reload` export
  ;; below breaks one clause of that and would return the stale value if the
  ;; clause were dropped; each is compared against wabt's interpreter, which
  ;; has no such rule.  The direct calls are the -O1 lens, where inlining is
  ;; off; the indirect ones and the two padded callees are the -O2 lens.

  (memory 1)
  (table 4 funcref)
  (elem (i32.const 0) $leaf_frame $peek $leaf_big)

  (type $void   (func))
  (type $peek_t (func (param i32) (result i32)))

  (global $sp    (mut i32) (i32.const 65536))
  (global $other (mut i32) (i32.const 0))
  ;; Nothing in this module ever assigns it, so it is zero at run time and
  ;; opaque at compile time -- a branch that is never taken and cannot be
  ;; folded away, which is what the speculation counterexamples need.
  (global $never (mut i32) (i32.const 0))

  ;; ------------------------------------------------------------- callees

  ;; What a leaf looks like out of an unoptimised producer: a frame below the
  ;; stack pointer that is never published, because the store back was dead.
  (func $leaf_frame
    (i32.store (i32.sub (global.get $sp) (i32.const 4)) (i32.const 0x5EED))
    (i32.store (i32.sub (global.get $sp) (i32.const 8)) (i32.const 0xF00D)))

  ;; The same, past the inliner's size limit, so -O2 sees a call.
  (func $leaf_big
    (local $b i32)
    (local.set $b (i32.sub (global.get $sp) (i32.const 256)))
    (i32.store offset=0 (local.get $b) (i32.const 4096))
    (i32.store offset=4 (local.get $b) (i32.const 4100))
    (i32.store offset=8 (local.get $b) (i32.const 4104))
    (i32.store offset=12 (local.get $b) (i32.const 4108))
    (i32.store offset=16 (local.get $b) (i32.const 4112))
    (i32.store offset=20 (local.get $b) (i32.const 4116))
    (i32.store offset=24 (local.get $b) (i32.const 4120))
    (i32.store offset=28 (local.get $b) (i32.const 4124))
    (i32.store offset=32 (local.get $b) (i32.const 4128))
    (i32.store offset=36 (local.get $b) (i32.const 4132))
    (i32.store offset=40 (local.get $b) (i32.const 4136))
    (i32.store offset=44 (local.get $b) (i32.const 4140))
    (i32.store offset=48 (local.get $b) (i32.const 4144))
    (i32.store offset=52 (local.get $b) (i32.const 4148))
    (i32.store offset=56 (local.get $b) (i32.const 4152))
    (i32.store offset=60 (local.get $b) (i32.const 4156))
    (i32.store offset=64 (local.get $b) (i32.const 4160))
    (i32.store offset=68 (local.get $b) (i32.const 4164))
    (i32.store offset=72 (local.get $b) (i32.const 4168))
    (i32.store offset=76 (local.get $b) (i32.const 4172))
    (i32.store offset=80 (local.get $b) (i32.const 4176))
    (i32.store offset=84 (local.get $b) (i32.const 4180))
    (i32.store offset=88 (local.get $b) (i32.const 4184))
    (i32.store offset=92 (local.get $b) (i32.const 4188))
    (i32.store offset=96 (local.get $b) (i32.const 4192))
    (i32.store offset=100 (local.get $b) (i32.const 4196))
    (i32.store offset=104 (local.get $b) (i32.const 4200))
    (i32.store offset=108 (local.get $b) (i32.const 4204))
    (i32.store offset=112 (local.get $b) (i32.const 4208))
    (i32.store offset=116 (local.get $b) (i32.const 4212))
    (i32.store offset=120 (local.get $b) (i32.const 4216))
    (i32.store offset=124 (local.get $b) (i32.const 4220))
    (i32.store offset=128 (local.get $b) (i32.const 4224))
    (i32.store offset=132 (local.get $b) (i32.const 4228))
    (i32.store offset=136 (local.get $b) (i32.const 4232))
    (i32.store offset=140 (local.get $b) (i32.const 4236))
    (i32.store offset=144 (local.get $b) (i32.const 4240))
    (i32.store offset=148 (local.get $b) (i32.const 4244))
    (i32.store offset=152 (local.get $b) (i32.const 4248))
    (i32.store offset=156 (local.get $b) (i32.const 4252))
    (i32.store offset=160 (local.get $b) (i32.const 4256))
    (i32.store offset=164 (local.get $b) (i32.const 4260))
    (i32.store offset=168 (local.get $b) (i32.const 4264))
    (i32.store offset=172 (local.get $b) (i32.const 4268))
    (i32.store offset=176 (local.get $b) (i32.const 4272))
    (i32.store offset=180 (local.get $b) (i32.const 4276))
    (i32.store offset=184 (local.get $b) (i32.const 4280))
    (i32.store offset=188 (local.get $b) (i32.const 4284))
    (i32.store offset=192 (local.get $b) (i32.const 4288))
    (i32.store offset=196 (local.get $b) (i32.const 4292))
    (i32.store offset=200 (local.get $b) (i32.const 4296))
    (i32.store offset=204 (local.get $b) (i32.const 4300))
    (i32.store offset=208 (local.get $b) (i32.const 4304))
    (i32.store offset=212 (local.get $b) (i32.const 4308))
    (i32.store offset=216 (local.get $b) (i32.const 4312))
    (i32.store offset=220 (local.get $b) (i32.const 4316))
    (i32.store offset=224 (local.get $b) (i32.const 4320))
    (i32.store offset=228 (local.get $b) (i32.const 4324))
    (i32.store offset=232 (local.get $b) (i32.const 4328))
    (i32.store offset=236 (local.get $b) (i32.const 4332))
    (i32.store offset=240 (local.get $b) (i32.const 4336))
    (i32.store offset=244 (local.get $b) (i32.const 4340))
    (i32.store offset=248 (local.get $b) (i32.const 4344))
    (i32.store offset=252 (local.get $b) (i32.const 4348))
  )

  ;; Reads, never writes.  Keeping the caller's loads across this is right;
  ;; keeping a store that is still waiting to be proved dead is not.
  (func $peek (param i32) (result i32) (i32.load (local.get 0)))

  ;; One byte too far: the access starts below the pointer and ends above it.
  (func $poke_minus2
    (i32.store align=1 (i32.sub (global.get $sp) (i32.const 2)) (i32.const 0x0BAD)))

  ;; Squarely in the caller's frame.
  (func $poke_at_sp
    (i32.store (global.get $sp) (i32.const 0x0BAD)))

  ;; And the same, too big to inline.
  (func $poke_at_sp_big
    (local $b i32)
    (i32.store (global.get $sp) (i32.const 0x0BAD))
    (local.set $b (i32.sub (global.get $sp) (i32.const 256)))
    (i32.store offset=0 (local.get $b) (i32.const 8192))
    (i32.store offset=4 (local.get $b) (i32.const 8196))
    (i32.store offset=8 (local.get $b) (i32.const 8200))
    (i32.store offset=12 (local.get $b) (i32.const 8204))
    (i32.store offset=16 (local.get $b) (i32.const 8208))
    (i32.store offset=20 (local.get $b) (i32.const 8212))
    (i32.store offset=24 (local.get $b) (i32.const 8216))
    (i32.store offset=28 (local.get $b) (i32.const 8220))
    (i32.store offset=32 (local.get $b) (i32.const 8224))
    (i32.store offset=36 (local.get $b) (i32.const 8228))
    (i32.store offset=40 (local.get $b) (i32.const 8232))
    (i32.store offset=44 (local.get $b) (i32.const 8236))
    (i32.store offset=48 (local.get $b) (i32.const 8240))
    (i32.store offset=52 (local.get $b) (i32.const 8244))
    (i32.store offset=56 (local.get $b) (i32.const 8248))
    (i32.store offset=60 (local.get $b) (i32.const 8252))
    (i32.store offset=64 (local.get $b) (i32.const 8256))
    (i32.store offset=68 (local.get $b) (i32.const 8260))
    (i32.store offset=72 (local.get $b) (i32.const 8264))
    (i32.store offset=76 (local.get $b) (i32.const 8268))
    (i32.store offset=80 (local.get $b) (i32.const 8272))
    (i32.store offset=84 (local.get $b) (i32.const 8276))
    (i32.store offset=88 (local.get $b) (i32.const 8280))
    (i32.store offset=92 (local.get $b) (i32.const 8284))
    (i32.store offset=96 (local.get $b) (i32.const 8288))
    (i32.store offset=100 (local.get $b) (i32.const 8292))
    (i32.store offset=104 (local.get $b) (i32.const 8296))
    (i32.store offset=108 (local.get $b) (i32.const 8300))
    (i32.store offset=112 (local.get $b) (i32.const 8304))
    (i32.store offset=116 (local.get $b) (i32.const 8308))
    (i32.store offset=120 (local.get $b) (i32.const 8312))
    (i32.store offset=124 (local.get $b) (i32.const 8316))
    (i32.store offset=128 (local.get $b) (i32.const 8320))
    (i32.store offset=132 (local.get $b) (i32.const 8324))
    (i32.store offset=136 (local.get $b) (i32.const 8328))
    (i32.store offset=140 (local.get $b) (i32.const 8332))
    (i32.store offset=144 (local.get $b) (i32.const 8336))
    (i32.store offset=148 (local.get $b) (i32.const 8340))
    (i32.store offset=152 (local.get $b) (i32.const 8344))
    (i32.store offset=156 (local.get $b) (i32.const 8348))
    (i32.store offset=160 (local.get $b) (i32.const 8352))
    (i32.store offset=164 (local.get $b) (i32.const 8356))
    (i32.store offset=168 (local.get $b) (i32.const 8360))
    (i32.store offset=172 (local.get $b) (i32.const 8364))
    (i32.store offset=176 (local.get $b) (i32.const 8368))
    (i32.store offset=180 (local.get $b) (i32.const 8372))
    (i32.store offset=184 (local.get $b) (i32.const 8376))
    (i32.store offset=188 (local.get $b) (i32.const 8380))
    (i32.store offset=192 (local.get $b) (i32.const 8384))
    (i32.store offset=196 (local.get $b) (i32.const 8388))
    (i32.store offset=200 (local.get $b) (i32.const 8392))
    (i32.store offset=204 (local.get $b) (i32.const 8396))
    (i32.store offset=208 (local.get $b) (i32.const 8400))
    (i32.store offset=212 (local.get $b) (i32.const 8404))
    (i32.store offset=216 (local.get $b) (i32.const 8408))
    (i32.store offset=220 (local.get $b) (i32.const 8412))
    (i32.store offset=224 (local.get $b) (i32.const 8416))
    (i32.store offset=228 (local.get $b) (i32.const 8420))
    (i32.store offset=232 (local.get $b) (i32.const 8424))
    (i32.store offset=236 (local.get $b) (i32.const 8428))
    (i32.store offset=240 (local.get $b) (i32.const 8432))
    (i32.store offset=244 (local.get $b) (i32.const 8436))
    (i32.store offset=248 (local.get $b) (i32.const 8440))
    (i32.store offset=252 (local.get $b) (i32.const 8444))
  )

  ;; Writes only its own frame, but calls something that does not.
  (func $outer_calls_poke (call $poke_at_sp))

  ;; Moves the stack pointer first, so `global.get $sp` inside it is no longer
  ;; the value the caller published.
  (func $move_sp
    (global.set $sp (i32.sub (global.get $sp) (i32.const 64)))
    (i32.store offset=68 (global.get $sp) (i32.const 0xDEAD))
    (global.set $sp (i32.add (global.get $sp) (i32.const 64))))

  ;; A frame hung off a different global.
  (func $leaf_other
    (i32.store (i32.sub (global.get $other) (i32.const 4)) (i32.const 0xC0DE)))

  (func $bump_other (global.set $other (i32.const 42)))

  ;; ------------------------------------------------- must still reload

  ;; The callee writes the caller's frame base.
  (func (export "above") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call $poke_at_sp)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  (func (export "above_big") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call $poke_at_sp_big)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; It ends two bytes above the pointer: `d <= 0` would accept this,
  ;; `d + width <= 0` is what makes it right.
  (func (export "overlap2") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call $poke_minus2)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; The caller's slot is below its own frame pointer, where the callee's is.
  (func (export "below") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (i32.sub (local.get $fp) (i32.const 8)) (i32.const 111))
    (call $leaf_frame)
    (local.set $r (i32.load (i32.sub (local.get $fp) (i32.const 8))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; The caller's slot is an absolute address, which nothing relates to the
  ;; stack pointer -- and here it is exactly where the callee's frame lands.
  (func (export "abs_slot") (result i32)
    (local $save i32) (local $r i32)
    (local.set $save (global.get $sp))
    (global.set $sp (i32.add (global.get $sp) (i32.const -65328)))
    (i32.store (i32.const 200) (i32.const 111))
    (call $leaf_frame)
    (local.set $r (i32.load (i32.const 200)))
    (global.set $sp (local.get $save))
    (local.get $r))

  ;; A frame measured from another global that happens to point into ours.
  (func (export "other_root") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (global.set $other (i32.add (local.get $fp) (i32.const 4)))
    (i32.store (local.get $fp) (i32.const 111))
    (call $leaf_other)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; Transitively not frame-local.
  (func (export "transitive") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call $outer_calls_poke)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; The callee republishes the stack pointer, so its frame is not where the
  ;; caller's arithmetic says it is.
  (func (export "moved_sp") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store offset=4 (local.get $fp) (i32.const 111))
    (call $move_sp)
    (local.set $r (i32.load offset=4 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; A global the callee assigns may not be carried across it either.
  (func (export "global_write") (result i32)
    (global.set $other (i32.const 7))
    (call $bump_other)
    (global.get $other))

  ;; The callee only reads -- but it reads a slot whose store is still a
  ;; candidate for dead-store elimination, and the next store would remove it.
  (func (export "peek_pending") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (local.set $r (call $peek (local.get $fp)))
    (i32.store (local.get $fp) (i32.const 222))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (i32.add (local.get $r) (i32.load (local.get $fp))))

  (func (export "ind_peek_pending") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (local.set $r (call_indirect (type $peek_t) (local.get $fp) (i32.const 1)))
    (i32.store (local.get $fp) (i32.const 222))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (i32.add (local.get $r) (i32.load (local.get $fp))))

  ;; ------------------------------------------------- must still be right
  ;; when the rule does fire, which is the shape it exists for

  (func (export "keeps") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (i32.store offset=4 (local.get $fp) (i32.const 222))
    (call $leaf_frame)
    (local.set $r (i32.add (i32.load (local.get $fp))
                           (i32.load offset=4 (local.get $fp))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  (func (export "keeps_big") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 320)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call $leaf_big)
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 320)))
    (local.get $r))

  (func (export "ind_keeps") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call_indirect (type $void) (i32.const 0))
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; A loop whose frame pointer is a different value every iteration: the one
  ;; assignment that reaches the call is re-executed, and naming its SSA value
  ;; still says which bytes the callee owns.
  (func (export "loop_sp") (result i32)
    (local $i i32) (local $acc i32) (local $fp i32) (local $save i32)
    (local.set $save (global.get $sp))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (global.set $sp (i32.sub (local.get $save)
                               (i32.mul (i32.add (local.get $i) (i32.const 1))
                                        (i32.const 32))))
      (local.set $fp (global.get $sp))
      (i32.store (local.get $fp) (local.get $i))
      (i32.store offset=8 (local.get $fp) (i32.const 5))
      (call $leaf_frame)
      (local.set $acc (i32.add (local.get $acc)
                               (i32.add (i32.load (local.get $fp))
                                        (i32.load offset=8 (local.get $fp)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (global.set $sp (local.get $save))
    (local.get $acc))

  ;; The dispatch shape: an indirect call in a loop, with the bound and the
  ;; accumulator living in the frame.
  (func (export "loop_dispatch") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store        (local.get $fp) (i32.const 0))    ;; i
    (i32.store offset=4 (local.get $fp) (i32.const 6))  ;; n
    (i32.store offset=8 (local.get $fp) (i32.const 1))  ;; acc
    (block $done (loop $again
      (br_if $done (i32.ge_u (i32.load (local.get $fp))
                             (i32.load offset=4 (local.get $fp))))
      (call_indirect (type $void) (i32.const 0))
      (i32.store offset=8 (local.get $fp)
                 (i32.add (i32.load offset=8 (local.get $fp))
                          (i32.load (local.get $fp))))
      (i32.store (local.get $fp)
                 (i32.add (i32.load (local.get $fp)) (i32.const 1)))
      (br $again)))
    (local.set $r (i32.load offset=8 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; Reads a byte out of a fixed address, so neither a call barrier nor the
  ;; forwarded value can answer it: it has to come from memory.
  (func $peek300 (result i32) (i32.load8_u (i32.const 300)))

  ;; ------------------------------------------------- iterated memory state
  ;;
  ;; What one loop leaves behind reaches the next only because the memory and
  ;; global analysis is swept until it stops moving rather than exactly twice:
  ;; two sweeps carry an answer through one loop header and no further, and a
  ;; producer that has not optimised anything emits its function body as a run
  ;; of loops one after another.  Sweeping optimistically is what makes that
  ;; possible -- a header assumes its back edge agrees until a later sweep can
  ;; check -- so every export here is a shape where the assumption is FALSE and
  ;; the check is the only thing standing between it and a wrong answer.

  ;; A loop that does move the stack pointer.  Reading it afterwards must see
  ;; where the loop left it, not where it started; an optimistic sweep that
  ;; was never validated answers the latter.
  (func (export "loop_moves_sp") (result i32)
    (local $i i32) (local $save i32) (local $r i32)
    (local.set $save (global.get $sp))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 3)))
      (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.set $r (i32.sub (local.get $save) (global.get $sp)))   ;; 48
    (global.set $sp (local.get $save))
    (local.get $r))

  ;; The same, but the write happens on one arm of a branch inside the loop,
  ;; so the back edge agrees with the entry edge on some iterations.  Agreeing
  ;; sometimes is not agreeing.
  (func (export "loop_moves_sp_cond") (result i32)
    (local $i i32) (local $save i32) (local $r i32)
    (local.set $save (global.get $sp))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (if (i32.eq (i32.and (local.get $i) (i32.const 1)) (i32.const 1))
        (then (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.set $r (i32.sub (local.get $save) (global.get $sp)))   ;; 32
    (global.set $sp (local.get $save))
    (local.get $r))

  ;; Three loops in a row -- the shape of a hash round function out of an
  ;; unoptimised producer.  The first two leave the stack pointer alone, which
  ;; is what the iteration is for; the third moves it, which is what the
  ;; validation is for.  Both answers have to be right at once.
  (func (export "sp_three_loops") (result i32)
    (local $i i32) (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 64)))
    (local.set $fp (global.get $sp))
    (block $d1 (loop $l1
      (br_if $d1 (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store offset=8 (local.get $fp) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l1)))
    (local.set $i (i32.const 0))
    (block $d2 (loop $l2
      (br_if $d2 (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store offset=12 (local.get $fp) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l2)))
    (local.set $i (i32.const 0))
    (block $d3 (loop $l3
      (br_if $d3 (i32.ge_u (local.get $i) (i32.const 2)))
      (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l3)))
    (local.set $r (i32.sub (local.get $fp) (global.get $sp)))     ;; 32
    (global.set $sp (i32.add (local.get $fp) (i32.const 64)))
    (local.get $r))

  ;; A frame slot the second of two loops overwrites.  Carrying the first
  ;; loop's answer past the second is the failure this one catches.
  (func (export "two_loops_frame") (result i32)
    (local $i i32) (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 64)))
    (local.set $fp (global.get $sp))
    (i32.store offset=0 (local.get $fp) (i32.const 5))
    (i32.store offset=8 (local.get $fp) (i32.const 0))
    (block $d1 (loop $l1
      (br_if $d1 (i32.ge_u (local.get $i) (i32.const 3)))
      (i32.store offset=8 (local.get $fp)
        (i32.add (i32.load offset=8 (local.get $fp))
                 (i32.load offset=0 (local.get $fp))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l1)))
    (local.set $i (i32.const 0))
    (block $d2 (loop $l2
      (br_if $d2 (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store offset=0 (local.get $fp)
        (i32.mul (i32.load offset=0 (local.get $fp)) (i32.const 3)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l2)))
    (local.set $r (i32.add (i32.load offset=8 (local.get $fp))
                           (i32.load offset=0 (local.get $fp))))   ;; 15 + 45
    (global.set $sp (i32.add (local.get $fp) (i32.const 64)))
    (local.get $r))

  ;; --------------------------------------------- dead stores across a join
  ;;
  ;; A store waiting to be proven dead may wait across a block boundary, but
  ;; only where the boundary is the block's only way out.  Here it is not: one
  ;; arm overwrites the slot and the other reads it, so a candidate carried
  ;; into the overwriting arm deletes a store the reading arm still needs.
  ;;
  ;; The reading arm has to read in a way the forwarding cannot answer from
  ;; the value alone, or deleting the store is invisible and the shape proves
  ;; nothing: a byte out of the middle of the word is the same location but
  ;; not the same access, so it has to come from memory.  The loop runs the
  ;; two arms on different iterations so neither can be folded away.
  (func (export "dse_two_succ") (result i32)
    (local $i i32) (local $fp i32) (local $acc i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store (local.get $fp) (i32.add (local.get $i) (i32.const 100)))
      (if (i32.eqz (local.get $i))
        (then (i32.store (local.get $fp) (i32.const 9)))
        (else (local.set $acc (i32.add (local.get $acc)
                                       (i32.load8_u (local.get $fp))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $acc))                                              ;; 101

  ;; The same join with the arms the other way round, so an implementation
  ;; that got the order right by luck still fails.
  (func (export "dse_two_succ_rev") (result i32)
    (local $i i32) (local $fp i32) (local $acc i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store offset=4 (local.get $fp)
                 (i32.add (local.get $i) (i32.const 200)))
      (if (i32.eqz (local.get $i))
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load8_u offset=4 (local.get $fp)))))
        (else (i32.store offset=4 (local.get $fp) (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $acc))                                              ;; 200

  ;; And the same again with the surviving read behind a call, which is the
  ;; other way a load cannot be answered from the stored value.
  (func (export "dse_two_succ_call") (result i32)
    (local $i i32) (local $acc i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 2)))
      (i32.store (i32.const 300) (i32.add (local.get $i) (i32.const 100)))
      (if (i32.eqz (local.get $i))
        (then (i32.store (i32.const 300) (i32.const 9)))
        (else (local.set $acc (i32.add (local.get $acc) (call $peek300)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                              ;; 101

  ;; A store a later block really does overwrite, with the join the only way
  ;; out of the first -- this one must be removed, so a fix that simply stops
  ;; carrying anything cannot pass either.  The unconditional inner block is
  ;; what forces the boundary; without the carry the store survives.
  (func (export "dse_one_succ") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store offset=16 (local.get $fp) (i32.const 41))
    (block (br 0))
    (i32.store offset=16 (local.get $fp) (i32.const 42))
    (local.set $r (i32.load8_u offset=16 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))                                                ;; 42

  ;; A narrower store does not overwrite a wider one, across a join or
  ;; anywhere else.
  (func (export "dse_narrow_join") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store offset=20 (local.get $fp) (i32.const 0x11223344))
    (block (br 0))
    (i32.store8 offset=20 (local.get $fp) (i32.const 0x99))
    (local.set $r (i32.load offset=20 (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))                                                ;; 0x11223399

  ;; ------------------------------------------- speculating a load out of a
  ;;                                              conditional loop body
  ;;
  ;; A loop-invariant load may leave a body that is not guaranteed to run when
  ;; some access to exactly those bytes already dominates the loop: wasm memory
  ;; only grows, so an address that was reachable once stays reachable.  Every
  ;; export here is a shape where that witness is absent or does not cover the
  ;; bytes, and the load would trap if it were hoisted anyway.  The reference
  ;; interpreter never speculates, so any hoist that is wrong shows up as a
  ;; trap against a value.

  ;; No witness at all: the address is never touched before the loop and is
  ;; far out of bounds, and the arm that reads it never runs.
  (func (export "licm_no_witness") (result i32)
    (local $i i32) (local $acc i32)
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (if (global.get $never)
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load (i32.const 0xfffff000))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 0

  ;; A witness one byte wide does not make the word that starts at it
  ;; reachable: 65535 is the last byte of the one page this module declares,
  ;; so the byte load is fine and the word load runs off the end.
  (func (export "licm_witness_narrower") (result i32)
    (local $i i32) (local $p i32) (local $acc i32)
    (local.set $p (i32.const 65535))
    (local.set $acc (i32.load8_u (local.get $p)))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (if (global.get $never)
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load (local.get $p))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 0

  ;; The witness is at a different displacement from the same base, which is a
  ;; different location however close it is.
  (func (export "licm_witness_offset") (result i32)
    (local $i i32) (local $p i32) (local $acc i32)
    (local.set $p (i32.const 65528))
    (local.set $acc (i32.load (local.get $p)))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (if (global.get $never)
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load offset=12 (local.get $p))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 0

  ;; The witness does not dominate the loop -- it is on the other arm of a
  ;; branch, so a run that takes this arm has never touched those bytes.
  (func (export "licm_witness_not_dom") (result i32)
    (local $i i32) (local $p i32) (local $acc i32)
    (local.set $p (i32.const 0xfffff000))
    (if (global.get $never)
      (then (local.set $acc (i32.load (local.get $p)))))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 4)))
      (if (global.get $never)
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load (local.get $p))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 0

  ;; The witness is real and does dominate, so this load may be speculated --
  ;; and the loop stores over it, so it still must not leave.  A rule that
  ;; only checked reachability and forgot the store would answer 30 here.
  (func (export "licm_witness_but_stored") (result i32)
    (local $i i32) (local $p i32) (local $acc i32)
    (local.set $p (i32.const 400))
    (i32.store (local.get $p) (i32.const 10))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 3)))
      (if (i32.eqz (global.get $never))
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load (local.get $p))))))
      (i32.store (local.get $p) (i32.add (i32.load (local.get $p))
                                         (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 10+11+12

  ;; The one that must fire: a witness that dominates, no store in the loop,
  ;; and the read sitting on an arm that is not guaranteed to run.  Only
  ;; --stats can see the hoist; the value is here so a hoist that is wrong
  ;; about *which* bytes still shows up.
  (func (export "licm_witness_hoists") (result i32)
    (local $i i32) (local $p i32) (local $acc i32)
    (local.set $p (i32.const 408))
    (i32.store (local.get $p) (i32.const 6))
    (block $done (loop $again
      (br_if $done (i32.ge_u (local.get $i) (i32.const 3)))
      (if (i32.eqz (global.get $never))
        (then (local.set $acc (i32.add (local.get $acc)
                                       (i32.load (local.get $p))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $again)))
    (local.get $acc))                                                ;; 18
)
