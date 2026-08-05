/* spec_tables.h -- the shape of the tables test/spec/gen.py generates and
 * test/spec/driver.c walks.
 *
 * One header rather than a copy in each place because there are now three
 * kinds of file that have to agree on it: the driver, the per-.wast command
 * tables, and the index that lists them.  When the layout lived in two copies
 * the only thing keeping them in step was that both were edited together.
 */
#ifndef WARMC_SPEC_TABLES_H
#define WARMC_SPEC_TABLES_H

#include "../../rt/warm_rt.h"

/* Value kinds in the generated tables. */
enum {
  SV_I32, SV_I64, SV_F32, SV_F64,
  SV_NULLREF,          /* ref.null of any kind */
  SV_EXTERNREF,        /* ref.extern N, carried as the pointer N + 1 */
  SV_REF_NONNULL,      /* a reference the assertion constrains only by kind:
                        * (ref.func), (ref.array), (ref.struct) and the rest,
                        * all of which say non-null and nothing more */
  SV_F32_NAN_CANON, SV_F32_NAN_ARITH,
  SV_F64_NAN_CANON, SV_F64_NAN_ARITH,
  SV_V128,             /* `bits` is the low half, `hi` the top */
  /* A disjunction: the result must equal one of several alternatives, which
   * is how a .wast file states the allowed outcomes of a *relaxed* vector
   * instruction.  This entry is a header -- `bits` is how many alternatives
   * follow it, and they are the next `bits` entries of the same array, so a
   * result list is walked with a variable stride rather than indexed.
   *
   * This used to mean "any value: the assertion does not constrain it".  That
   * was never noticed because the only corpus files producing an `either` are
   * the seven relaxed-simd ones, and none of them compiled, so the code path
   * never ran.  Every interesting assertion in those files is an `either`, so
   * implementing the instructions over it would have made them all pass
   * regardless of the answer. */
  SV_EITHER,
  SV_V128_NAN          /* some lanes exact, some any NaN of a class */
};

/* Two bits per lane in `pat`, for SV_V128_NAN. */
enum { LP_EXACT, LP_CANON, LP_ARITH };

typedef struct {
  unsigned char kind, lanes;
  unsigned pat;
  u64 bits, hi;
} SpecVal;

enum { CMD_MODULE, CMD_REGISTER, CMD_INVOKE, CMD_GET };
/* EXPECT_EXCEPTION is its own verdict rather than a trap: a wast file asserts
 * the two separately, and a module whose `try_table` failed to catch would
 * otherwise pass an `assert_exception` for the wrong reason. */
enum { EXPECT_OK, EXPECT_TRAP, EXPECT_EXHAUSTION, EXPECT_FAIL_INSTANTIATE,
       EXPECT_EXCEPTION };

typedef struct {
  unsigned char op;
  unsigned char expect;
  unsigned short module;      /* index into the file's modules, or the target */
  const char *name;
  /* Its length in bytes, because a wasm name is a byte string and may hold a
   * NUL: `names.wast` exports one that begins "\00\01\02" beside the empty
   * name, and a C literal stops at the first of them.  Every lookup here goes
   * through the `_n` form of the runtime call for that reason. */
  unsigned name_len;
  unsigned nargs, nres;
  const SpecVal *args;
  const SpecVal *res;
  unsigned line;
} SpecCmd;

/* One .wast file: the modules it compiles to and the commands over them.
 *
 * The suite used to link one executable per file, and on macOS the first run
 * of a freshly linked binary costs a fifth of a second of signature
 * validation however small it is -- so two hundred and fifty-seven files cost
 * a minute before running a single assertion.  Every file goes into one
 * executable instead, and the harness invokes that same binary once per file:
 * the validation is paid once, and each file still gets its own process, so a
 * module that faults takes down its own run and nothing else. */
typedef struct {
  const char *name;                       /* "i32.wast" */
  const ModuleDesc *const *modules;
  const char *const *module_names;        /* "" unless registered up front */
  const SpecCmd *cmds;
  unsigned ncmds;
} SpecFile;

extern const SpecFile spec_files[];
extern const unsigned spec_nfiles;

#endif
