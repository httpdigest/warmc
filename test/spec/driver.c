/* driver.c -- runs one .wast file's commands, from tables the harness
 * generates beside it.
 *
 * A .wast file is a little program: instantiate these modules, register some
 * of them under names the others import, then invoke exports and check what
 * comes back.  For an AOT compiler that is a linked executable with the
 * modules compiled in and this walking the command list.
 *
 * Every file in the corpus is linked into *one* such executable, which is
 * then invoked once per file with the file's name in argv[1] -- see the note
 * on SpecFile in spec_tables.h for why.  Named nothing, it lists what it
 * holds, which is both what the harness reads and what a person wants when
 * they run it by hand.
 *
 * The tables are data rather than generated code because a single file can
 * hold ten thousand assertions, and the logic that checks one belongs in a
 * place where it can be read.
 */
#include "spec_tables.h"

/* The runtime's CLI harness refers to this; we replace the harness. */
const ModuleDesc warmc_module = { 0 };

/* ------------------------------------------------------------ printing */

static void out(const char *s) {
  const char *p = s;
  while (*p) p++;
  warmc_rt_write(2, s, (unsigned long)(p - s));
}

static void put_u(u64 v) {
  char buf[24];
  int i = 24;
  if (!v) buf[--i] = '0';
  while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
  warmc_rt_write(2, buf + i, (unsigned long)(24 - i));
}

static void put_hex(u64 v) {
  static const char d[] = "0123456789abcdef";
  char buf[18];
  int i = 18;
  if (!v) buf[--i] = '0';
  while (v) { buf[--i] = d[v & 15]; v >>= 4; }
  buf[--i] = 'x'; buf[--i] = '0';
  warmc_rt_write(2, buf + i, (unsigned long)(18 - i));
}

/* --------------------------------------------------------- comparison */

static int is_nan32(u32 b) { return (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu); }
static int is_nan64(u64 b) {
  return (b & 0x7ff0000000000000ull) == 0x7ff0000000000000ull &&
         (b & 0x000fffffffffffffull);
}

static int matches(const SpecVal *want, u64 got) {
  switch (want->kind) {
    case SV_I32: return (u32)got == (u32)want->bits;
    case SV_I64: return got == want->bits;
    case SV_F32: return (u32)got == (u32)want->bits;
    case SV_F64: return got == want->bits;
    case SV_NULLREF: return got == 0;
    case SV_EXTERNREF: return got == want->bits;
    case SV_REF_NONNULL: return got != 0;
    case SV_F32_NAN_CANON: return ((u32)got & 0x7fffffffu) == 0x7fc00000u;
    case SV_F32_NAN_ARITH: return is_nan32((u32)got) && ((u32)got & 0x00400000u);
    case SV_F64_NAN_CANON: return (got & 0x7fffffffffffffffull) == 0x7ff8000000000000ull;
    case SV_F64_NAN_ARITH: return is_nan64(got) && (got & 0x0008000000000000ull);
  }
  return 0;
}

static int nan32_ok(u32 b, unsigned pat) {
  if (pat == LP_CANON) return (b & 0x7fffffffu) == 0x7fc00000u;
  return is_nan32(b) && (b & 0x00400000u);
}
static int nan64_ok(u64 b, unsigned pat) {
  if (pat == LP_CANON) return (b & 0x7fffffffffffffffull) == 0x7ff8000000000000ull;
  return is_nan64(b) && (b & 0x0008000000000000ull);
}

/* A v128 is compared as a whole -- unless some of its lanes are constrained
 * only to be a NaN of a given class, in which case each lane is its own
 * question and the vector cannot be one comparison.  That is most of what
 * `simd_f32x4_arith.wast` asserts, and it is the reason those files were
 * unmeasured until the instruction set landed. */
static int matches2(const SpecVal *want, u64 got, u64 got_hi) {
  if (want->kind == SV_V128) return got == want->bits && got_hi == want->hi;
  if (want->kind == SV_V128_NAN) {
    for (unsigned i = 0; i < want->lanes; i++) {
      unsigned pat = (want->pat >> (2 * i)) & 3u;
      if (want->lanes == 4) {
        u32 g = (u32)((i < 2 ? got >> (32 * i) : got_hi >> (32 * (i - 2))) & 0xffffffffu);
        u32 e = (u32)((i < 2 ? want->bits >> (32 * i)
                             : want->hi >> (32 * (i - 2))) & 0xffffffffu);
        if (pat == LP_EXACT ? g != e : !nan32_ok(g, pat)) return 0;
      } else {
        u64 g = i == 0 ? got : got_hi;
        u64 e = i == 0 ? want->bits : want->hi;
        if (pat == LP_EXACT ? g != e : !nan64_ok(g, pat)) return 0;
      }
    }
    return 1;
  }
  return matches(want, got);
}

/* Travels in a v register, and so is read out of q0 or written into fargs. */
static int is_vreg_kind(unsigned char k) {
  return k == SV_F32 || k == SV_F64 || k == SV_F32_NAN_CANON ||
         k == SV_F32_NAN_ARITH || k == SV_F64_NAN_CANON ||
         k == SV_F64_NAN_ARITH || k == SV_V128 || k == SV_V128_NAN;
}

/* An expected result is either one value or a disjunction of them, and a
 * disjunction's alternatives are the entries that follow its header -- so a
 * result list has no fixed stride and is walked rather than indexed.  The
 * three helpers below are the only places that know that. */

/* The entry that carries the result's *type*.  A disjunction's own header
 * carries none, and every alternative under it is a value of the one type the
 * signature says, so the first of them answers for all. */
static const SpecVal *typed_entry(const SpecVal *p) {
  return p->kind == SV_EITHER ? &p[1] : p;
}

static int result_in_vreg(const SpecVal *p) {
  return is_vreg_kind(typed_entry(p)->kind);
}

/* Does `got` satisfy the entry at *pp?  Advances *pp past the whole entry,
 * alternatives included. */
static int match_result(const SpecVal **pp, u64 got, u64 got_hi) {
  const SpecVal *p = *pp;
  if (p->kind == SV_EITHER) {
    unsigned n = (unsigned)p->bits;
    int ok = 0;
    for (unsigned k = 1; k <= n && !ok; k++) ok = matches2(&p[k], got, got_hi);
    /* The stride is a precondition rather than a tested rule: it only decides
     * anything for an action returning *several* results one of which is a
     * disjunction, and no file in the corpus writes one -- every relaxed
     * function returns a single v128.  Ablating it is uncaught, and would
     * stay uncaught however many relaxed cases were added. */
    *pp = p + n + 1;
    return ok;
  }
  *pp = p + 1;
  return matches2(p, got, got_hi);
}

/* ------------------------------------------------------------- running */

#define MAX_MODULES 4096
static WarmInstance *insts[MAX_MODULES];

static unsigned npass, nfail;
static const SpecFile *cur_file;

static void fail_at(unsigned line, const char *what) {
  nfail++;
  out("FAIL ");
  out(cur_file->name);
  out(":");
  put_u(line);
  out(": ");
  out(what);
  out("\n");
}

static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

static int run_one(const SpecFile *f) {
  const ModuleDesc *const *spec_modules = f->modules;
  const char *const *spec_module_names = f->module_names;
  WarmInstance *cur = 0;

  cur_file = f;

  for (unsigned c = 0; c < f->ncmds; c++) {
    const SpecCmd *cmd = &f->cmds[c];
    switch (cmd->op) {
      case CMD_MODULE: {
        u32 err = 0;
        WarmInstance *v = warmc_instantiate_catching(spec_modules[cmd->module], &err);
        if (cmd->expect == EXPECT_FAIL_INSTANTIATE || cmd->expect == EXPECT_TRAP) {
          if (err) npass++;
          else fail_at(cmd->line, "module instantiated but should not have");
          /* Keep going either way; a failed instantiation defines nothing. */
          if (!err) { insts[cmd->module] = v; cur = v; }
          break;
        }
        if (err) {
          fail_at(cmd->line, "instantiation failed");
          break;
        }
        insts[cmd->module] = v;
        cur = v;
        if (spec_module_names[cmd->module][0])
          warmc_register(spec_module_names[cmd->module], v);
        npass++;
        break;
      }

      case CMD_REGISTER:
        if (insts[cmd->module])
          warmc_register_n(cmd->name, cmd->name_len, insts[cmd->module]);
        break;

      case CMD_INVOKE: {
        WarmInstance *target = cmd->module == 0xffffu ? cur : insts[cmd->module];
        if (!target) { fail_at(cmd->line, "no instance for the action"); break; }
        const ExportDesc *e = warmc_find_export_n(target, cmd->name,
                                                 cmd->name_len);
        if (!e || e->kind != 0) { fail_at(cmd->line, "unknown export"); break; }

        u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
        u64 ret[WARMC_RETBUF_WORDS] = { 0 };
        unsigned ni = 0, nf = 0;
        for (unsigned i = 0; i < cmd->nargs; i++) {
          const SpecVal *a = &cmd->args[i];
          if (is_vreg_kind(a->kind)) {
            if (nf < 8) { fargs[2 * nf] = a->bits; fargs[2 * nf + 1] = a->hi; nf++; }
          } else { if (ni < 7) iargs[ni++] = a->bits; }
        }

        /* An export may re-export a function another module defines, and
         * that one has to be entered with its own instance. */
        FuncRef fr = warmc_export_func(target, e);
#if defined(__x86_64__)
        /* Parameters past x64's register ceilings (3 ints, 4 floats) go
         * through the stack-slot image warmc_invoke_s copies for us, laid
         * out the way the callee reads it: eight bytes per scalar in
         * parameter order, an aligned sixteen for a v128. */
#define SPEC_STK_WORDS 24
        u64 stk[SPEC_STK_WORDS];
        unsigned ni2 = 0, nf2 = 0, w = 0;
        for (unsigned i = 0; i < cmd->nargs; i++) {
          const SpecVal *a = &cmd->args[i];
          if (a->kind == SV_V128 || a->kind == SV_V128_NAN) {
            if (nf2 >= 4 && w + 2 < SPEC_STK_WORDS) {
              if (w & 1) w++;
              stk[w++] = a->bits; stk[w++] = a->hi;
            }
            nf2++;
          } else if (is_vreg_kind(a->kind)) {
            if (nf2 >= 4 && w < SPEC_STK_WORDS) stk[w++] = a->bits;
            nf2++;
          } else {
            if (ni2 >= 3 && w < SPEC_STK_WORDS) stk[w++] = a->bits;
            ni2++;
          }
        }
        u32 trap = warmc_invoke_catching_s(fr.code,
                                           (WarmInstance *)fr.instance,
                                           iargs, fargs, ret, stk, 8ull * w);
#else
        u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                                         iargs, fargs, ret);
#endif

        if (cmd->expect == EXPECT_EXCEPTION) {
          /* Exactly an uncaught exception.  Any other trap means the module
           * left compiled code for a reason the assertion did not ask for --
           * a `try_table` that took the exception and then went wrong is the
           * one this has to be able to tell apart. */
          if (trap == TRAP_UNCAUGHT_EXN) npass++;
          else if (trap) {
            nfail++;
            out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
            out(": expected an exception, got a trap: ");
            out(warmc_trap_name(trap)); out("\n");
          } else fail_at(cmd->line, "expected an exception");
          break;
        }
        if (cmd->expect == EXPECT_TRAP || cmd->expect == EXPECT_EXHAUSTION) {
          if (trap) npass++;
          else fail_at(cmd->line, "expected a trap");
          break;
        }
        if (trap) {
          nfail++;
          out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
          out(": unexpected trap: "); out(warmc_trap_name(trap)); out("\n");
          break;
        }

        int ok = 1;
        const SpecVal *want = cmd->res;
        for (unsigned i = 0; i < cmd->nres && ok; i++) {
          u64 got, got_hi;
          if (e->nresults == 1) {
            got = result_in_vreg(want) ? ret[WARMC_RET_V0] : ret[WARMC_RET_X0];
            got_hi = ret[WARMC_RET_V0 + 1];
          } else {
            got = ret[WARMC_RET_MULTI(i)];
            got_hi = ret[WARMC_RET_MULTI(i) + 1];
          }
          const SpecVal *entry = want;
          const SpecVal *typed = typed_entry(entry);
          int wide = typed->kind == SV_V128 || typed->kind == SV_V128_NAN;
          if (!match_result(&want, got, got_hi)) {
            ok = 0;
            nfail++;
            out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
            out(": "); out(cmd->name); out(" returned "); put_hex(got);
            /* A v128 whose halves are printed one at a time reads as the wrong
             * number entirely; every relaxed-simd result is one. */
            if (wide) { out(":"); put_hex(got_hi); }
            if (entry->kind == SV_EITHER) {
              out(", none of "); put_u(entry->bits); out(" allowed results");
            } else {
              out(", expected "); put_hex(entry->bits);
              if (wide) { out(":"); put_hex(entry->hi); }
            }
            out("\n");
          }
        }
        if (ok) npass++;
        break;
      }

      case CMD_GET: {
        WarmInstance *target = cmd->module == 0xffffu ? cur : insts[cmd->module];
        if (!target) { fail_at(cmd->line, "no instance for the action"); break; }
        const ExportDesc *e = warmc_find_export_n(target, cmd->name,
                                                 cmd->name_len);
        if (!e || e->kind != 3) { fail_at(cmd->line, "unknown global export"); break; }
        const u64 *cell = warmc_global_cell(target, e->index);
        u64 got = cell[0];
        const SpecVal *want = cmd->res;
        if (cmd->nres && !match_result(&want, got, cell[1])) {
          nfail++;
          out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
          out(": global "); out(cmd->name); out(" is "); put_hex(got);
          if (cmd->res[0].kind == SV_EITHER) {
            out(", none of "); put_u(cmd->res[0].bits); out(" allowed results");
          } else {
            out(", expected "); put_hex(cmd->res[0].bits);
          }
          out("\n");
        } else {
          npass++;
        }
        break;
      }
    }
  }

  out("spec-run: ");
  out(f->name);
  out(" ");
  put_u(npass);
  out(" passed, ");
  put_u(nfail);
  out(" failed\n");
  /* Silent unless WARMC_GC_STATS is set, and then it goes to stderr, which
   * nothing here parses.  This is the only harness in the tree that catches a
   * trap and keeps running, so it is the only one whose counters can say
   * anything about what a trap did to the collector -- and, once the mark loop
   * is on a thread, the only one that exercises unwinding out from under a
   * round at all. */
  warmc_report_run(cur);
  return nfail ? 1 : 0;
}

static int drive(int argc, char **argv) {
  /* Named nothing, say what is in here.  Every file is linked in, and which
   * of them to run is the caller's choice; running them all in one process
   * is deliberately not offered, because the registry a `register` command
   * writes to is global and finite, and one file's fault would end the rest. */
  if (argc < 2) {
    for (unsigned i = 0; i < spec_nfiles; i++) {
      out(spec_files[i].name);
      out("\n");
    }
    return 0;
  }

  for (unsigned i = 0; i < spec_nfiles; i++)
    if (str_eq(spec_files[i].name, argv[1])) return run_one(&spec_files[i]);

  out("spec-run: no such file: ");
  out(argv[1]);
  out("\n");
  return 2;
}

int (*warmc_driver_hook)(int, char **) = drive;
