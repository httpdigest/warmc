#ifndef WARMC_TARGET_H
#define WARMC_TARGET_H

#include "ir.h"

/* The live-range split plan: written by the first allocation, consumed by
   the retry.  veto flags params whose coalescing across forward edges --
   a header's entry edges, a merge's incoming ones -- the retry skips,
   cutting a spilled web at its loop boundary so the in-loop piece
   competes for a register on its own; addv/addhdr name values to thread
   through a NEW header param first (the loop-invariant shape).  The plan
   only ever asks -- the retry's allocation stays the sole authority. */
typedef struct SplitCtl {
  uint8_t *veto;
  uint32_t veto_n;
  Ref *addv;
  uint32_t *addhdr;
  Ref *addp;
  uint32_t nadd, cap_add;
  uint32_t nflagged;

  /* The retry-aware half: cutp lists the vetoed params so the retry can
     see a cut fail (its web still in a slot) the same way it sees an inv
     param fail.  The retry marks failures instead of emitting; the driver
     rolls the IR back, drops the failed grants and re-runs, so a failed
     grant never competes -- the second-order shuffle that kept the whole
     feature opt-in.  accept stops the loop: failures are then absorbed by
     the singleton slot-alias fallback. */
  Ref *cutp;
  uint32_t ncut, cap_cut;
  uint8_t *inv_fail;
  uint8_t *cut_fail;
  uint32_t nfailed_new;
  bool accept;

  /* The other side of a cut: params outside the loop that what the loop
     defines flows into along a forward edge, vetoed with it and dropped
     with it -- xcut_hdr[k] is the header param whose cut xcutp[k] serves.
     Without them the in-loop piece stays joined to everything it flows
     into after the loop, and is spilled with that. */
  Ref *xcutp, *xcut_hdr;
  uint32_t nxcut, cap_xcut;

  /* Every rung in every loop (WARMC_SPLIT), rather than the default: only
     cuts, and only in loops that make no call. */
  bool full;
} SplitCtl;

// One backend, selected once at startup from --target before any worker
// thread runs.  The generic layers reach the target only through these
// hooks (via the ir_* wrappers in ir.c and the driver in codegen.c);
// everything else target-specific lives behind produce_body.
typedef struct Target {
  const char *name;
  bool is_x64;
  // Is a wrap whose readers take the low half only no instruction, its
  // readers reading the i64's register (a64's analyse_wrap_alias)?
  bool wrap_alias;

  // May the idx-th argument of `user` be folded as the immediate `cval`?
  bool (*imm_ok)(IrFunc *f, Ref user, uint32_t idx, uint64_t cval, ValType ct);
  // Instructions needed to materialize a scalar / v128 constant.
  int (*const_cost)(uint64_t bits, bool wide);
  int (*vconst_cost)(uint64_t lo, uint64_t hi);
  // May a load/store of 1<<size_log2 bytes fold `off` into its address?
  bool (*mem_disp_ok)(uint64_t off, uint32_t size_log2);

  // Register allocation + emission of one function body; the return-code
  // protocol is the split/far-branch retry contract ir_produce_function
  // drives (1 done, 0 needs far branches, 2/3 split rounds, 4 run again to
  // place the loops it measured).
  int (*produce_body)(IrFunc *f, ObjBuilder *obj, const OptOpts *opt,
                      uint32_t out_bytes, FuncOut *fout, bool far,
                      SplitCtl *sc, bool split_collect);
  // One alignment-padding nop into a text buffer.
  void (*pad_nop)(Buf *b);
  // The call-to-warmc_unreached body of the shared dead-function stub.
  void (*emit_dead_call)(ObjBuilder *obj, uint32_t sym);
  // A function that traps with `code` through warmc_trap (`sym`): what a
  // call_indirect code table holds for a slot the call's checks reject.
  // NULL where call_indirect has no code tables.
  void (*emit_trap_stub)(ObjBuilder *obj, uint32_t sym, int code);

  // Register capacity for outgoing call arguments (int slots include the
  // implicit vmctx); sizes the shared out-area computation.
  uint8_t narg_int, narg_fp;
  // Bytes one scalar argument takes once it spills to the stack.  a64 keeps
  // 16 so every slot is v128-sized; x64 uses 8 because that IS the Win64
  // layout, which is what makes a host C function bound as a wasm import
  // callable without an adapter (a v128 still takes an aligned 16 there).
  uint8_t arg_slot;

  uint32_t max_frame;

  // How many copies of its body an iteration of a vectorised loop holds
  // where nothing keeps it to fewer (vectorize_one in opt.c): what the
  // vector registers hold with every copy's loads in flight at once.
  uint8_t vec_copies;
  // The integer lane sizes (bit n for VLANE_SIZE n) where a vector multiply
  // is one instruction, no dearer than the shift and add it can stand for.
  uint8_t vec_mul_lanes;
} Target;

extern const Target warmc_target_a64;
extern const Target warmc_target_x64;
extern const Target *g_target;

#endif
