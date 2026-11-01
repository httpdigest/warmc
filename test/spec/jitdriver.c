/* jitdriver.c -- runs one .wast file's commands, as driver.c does, with
 * every module compiled by the JIT (libwarmc) through the C API.
 *
 * The tables are driver.c's, which test/spec/gen.py generates; in place of
 * the compiled modules' descriptors they name each module's binary
 * (SpecFile.paths).  What the AOT driver leaves to the runtime -- resolving
 * an import from the instances registered under its module name -- this
 * does as any embedder of wasm.h does: by looking each import up in what it
 * registered, and handing wasm_instance_new the externs in import order.
 * The verdicts, and the line the harness reads, are driver.c's.
 */
#include "wasm.h"
#include "warm.h"
#include "spec_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ printing */

static void out(const char *s) { fputs(s, stderr); }
static void put_u(u64 v) { fprintf(stderr, "%llu", (unsigned long long)v); }
static void put_hex(u64 v) { fprintf(stderr, "0x%llx", (unsigned long long)v); }

/* --------------------------------------------------------- comparison
 * driver.c's, word for word */

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

static int match_result(const SpecVal **pp, u64 got, u64 got_hi) {
  const SpecVal *p = *pp;
  if (p->kind == SV_EITHER) {
    unsigned n = (unsigned)p->bits;
    int ok = 0;
    for (unsigned k = 1; k <= n && !ok; k++) ok = matches2(&p[k], got, got_hi);
    *pp = p + n + 1;
    return ok;
  }
  *pp = p + 1;
  return matches2(p, got, got_hi);
}

/* ------------------------------------------------------------ host refs
 * A spec file's `ref.extern N` is a host object the API can hand in: one
 * foreign object per N, so that the same N is the same reference.  The
 * tables carry it as driver.c needs it, (N + 1) << 2, with GC_HOST_TAG for
 * the same reference in the any hierarchy, which wasm.h cannot express. */

#define MAX_HOST 1024
static wasm_foreign_t *hosts[MAX_HOST];

static wasm_store_t *store;

static wasm_ref_t *host_ref(u64 bits) {
  u64 n = (bits >> 2) - 1;
  if (n >= MAX_HOST) return NULL;
  if (!hosts[n]) hosts[n] = wasm_foreign_new(store);
  return wasm_foreign_as_ref(hosts[n]);
}

/* the table's bits of a reference the API gave back */
static u64 ref_bits(const wasm_ref_t *r) {
  if (!r) return 0;
  for (u64 n = 0; n < MAX_HOST; n++)
    if (hosts[n] && wasm_ref_same(wasm_foreign_as_ref_const(hosts[n]), r))
      return (n + 1) << 2;
  return 0x10;   /* some other reference: not null, and no host object */
}

/* ------------------------------------------------------------- running */

#define MAX_MODULES 4096
static wasm_engine_t *engine;
static wasm_module_t *mods[MAX_MODULES];
static wasm_instance_t *insts[MAX_MODULES];
static wasm_extern_vec_t inst_ex[MAX_MODULES];

static struct { char *name; size_t len; unsigned slot; } reg[MAX_MODULES];
static unsigned nreg;

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

static wasm_module_t *module_of(unsigned slot) {
  if (mods[slot]) return mods[slot];
  const char *path = cur_file->paths[slot];
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  wasm_byte_vec_t bin;
  wasm_byte_vec_new_uninitialized(&bin, (size_t)n);
  size_t got = fread(bin.data, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { wasm_byte_vec_delete(&bin); return NULL; }
  wasm_message_t err;
  /* the same file in another slot is the same module */
  for (unsigned i = 0; cur_file->paths[i]; i++)
    if (mods[i] && !strcmp(cur_file->paths[i], path)) {
      wasm_byte_vec_delete(&bin);
      return mods[slot] = mods[i];
    }
  mods[slot] = warm_module_new(store, &bin, &err);
  if (!mods[slot]) { out("spec-run: "); out(path); out(": "); out(err.data); out("\n"); }
  wasm_byte_vec_delete(&err);
  wasm_byte_vec_delete(&bin);
  return mods[slot];
}

/* The export named name of the instance in slot, or NULL */
static const wasm_extern_t *export_of(unsigned slot, const char *name, size_t len) {
  if (!insts[slot]) return NULL;
  wasm_exporttype_vec_t types;
  wasm_module_exports(mods[slot], &types);
  const wasm_extern_t *found = NULL;
  for (size_t i = 0; i < types.size && !found; i++) {
    const wasm_name_t *n = wasm_exporttype_name(types.data[i]);
    if (n->size == len && !memcmp(n->data, name, len)) found = inst_ex[slot].data[i];
  }
  wasm_exporttype_vec_delete(&types);
  return found;
}

/* Instantiates slot, its imports looked up among the registered instances;
 * 0 when it does not link or its start function traps */
static int instantiate(unsigned slot) {
  wasm_module_t *m = module_of(slot);
  if (!m) return 0;
  wasm_importtype_vec_t imps;
  wasm_module_imports(m, &imps);
  wasm_extern_vec_t ex;
  wasm_extern_vec_new_uninitialized(&ex, imps.size);
  int ok = 1;
  for (size_t i = 0; i < imps.size && ok; i++) {
    const wasm_name_t *mn = wasm_importtype_module(imps.data[i]);
    const wasm_name_t *fn = wasm_importtype_name(imps.data[i]);
    const wasm_extern_t *e = NULL;
    for (unsigned r = nreg; r-- > 0 && !e;)
      if (reg[r].len == mn->size && !memcmp(reg[r].name, mn->data, mn->size))
        e = export_of(reg[r].slot, fn->data, fn->size);
    if (!e) ok = 0;
    else ex.data[i] = (wasm_extern_t *)e;
  }
  wasm_importtype_vec_delete(&imps);
  if (ok) {
    wasm_trap_t *trap = NULL;
    wasm_instance_t *in = wasm_instance_new(store, m, &ex, &trap);
    if (trap) wasm_trap_delete(trap);
    if (in) {
      insts[slot] = in;
      wasm_instance_exports(in, &inst_ex[slot]);
    } else {
      ok = 0;
    }
  }
  free(ex.data);
  return ok;
}

static void do_register(const char *name, size_t len, unsigned slot) {
  if (nreg == MAX_MODULES) return;
  reg[nreg].name = malloc(len + 1);
  memcpy(reg[nreg].name, name, len);
  reg[nreg].name[len] = 0;
  reg[nreg].len = len;
  reg[nreg].slot = slot;
  nreg++;
}

static int is_vreg_kind(unsigned char k) {
  return k == SV_F32 || k == SV_F64 || k == SV_F32_NAN_CANON ||
         k == SV_F32_NAN_ARITH || k == SV_F64_NAN_CANON ||
         k == SV_F64_NAN_ARITH || k == SV_V128 || k == SV_V128_NAN;
}

static int run_one(const SpecFile *f) {
  const char *const *spec_module_names = f->module_names;
  unsigned cur = 0xffffu;

  cur_file = f;
  for (unsigned c = 0; c < f->ncmds; c++) {
    const SpecCmd *cmd = &f->cmds[c];
    switch (cmd->op) {
      case CMD_MODULE: {
        int ok = instantiate(cmd->module);
        if (cmd->expect == EXPECT_FAIL_INSTANTIATE || cmd->expect == EXPECT_TRAP) {
          if (!ok) npass++;
          else fail_at(cmd->line, "module instantiated but should not have");
          if (ok) cur = cmd->module;
          break;
        }
        if (!ok) {
          fail_at(cmd->line, "instantiation failed");
          break;
        }
        cur = cmd->module;
        if (spec_module_names[cmd->module][0])
          do_register(spec_module_names[cmd->module],
                      strlen(spec_module_names[cmd->module]), cmd->module);
        npass++;
        break;
      }

      case CMD_REGISTER:
        if (insts[cmd->module]) do_register(cmd->name, cmd->name_len, cmd->module);
        break;

      case CMD_INVOKE: {
        unsigned target = cmd->module == 0xffffu ? cur : cmd->module;
        if (target == 0xffffu || !insts[target]) {
          fail_at(cmd->line, "no instance for the action");
          break;
        }
        const wasm_extern_t *e = export_of(target, cmd->name, cmd->name_len);
        const wasm_func_t *fn = e ? wasm_extern_as_func_const(e) : NULL;
        if (!fn) { fail_at(cmd->line, "unknown export"); break; }

        wasm_functype_t *ft = wasm_func_type(fn);
        const wasm_valtype_vec_t *ps = wasm_functype_params(ft);
        const wasm_valtype_vec_t *rs = wasm_functype_results(ft);
        size_t n = ps->size > rs->size ? ps->size : rs->size;
        warm_val_raw_t *vals = calloc(n ? n : 1, sizeof *vals);
        for (unsigned i = 0; i < cmd->nargs && i < ps->size; i++) {
          const SpecVal *a = &cmd->args[i];
          wasm_valkind_t k = wasm_valtype_kind(ps->data[i]);
          if (wasm_valkind_is_ref(k)) vals[i].ref = a->bits ? host_ref(a->bits) : NULL;
          else {
            memcpy(vals[i].v128, &a->bits, 8);
            memcpy(vals[i].v128 + 8, &a->hi, 8);
          }
        }
        wasm_trap_t *trap = warm_func_call_raw(fn, vals, n);

        if (cmd->expect == EXPECT_EXCEPTION) {
          wasm_message_t msg;
          int exn = 0;
          if (trap) {
            wasm_trap_message(trap, &msg);
            exn = !strcmp(msg.data, "uncaught exception");
            if (exn) npass++;
            else {
              nfail++;
              out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
              out(": expected an exception, got a trap: "); out(msg.data); out("\n");
            }
            wasm_byte_vec_delete(&msg);
          } else fail_at(cmd->line, "expected an exception");
        } else if (cmd->expect == EXPECT_TRAP || cmd->expect == EXPECT_EXHAUSTION) {
          if (trap) npass++;
          else fail_at(cmd->line, "expected a trap");
        } else if (trap) {
          wasm_message_t msg;
          wasm_trap_message(trap, &msg);
          nfail++;
          out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
          out(": unexpected trap: "); out(msg.data); out("\n");
          wasm_byte_vec_delete(&msg);
        } else {
          int ok = 1;
          const SpecVal *want = cmd->res;
          for (unsigned i = 0; i < cmd->nres && i < rs->size && ok; i++) {
            u64 got, got_hi;
            wasm_valkind_t k = wasm_valtype_kind(rs->data[i]);
            if (wasm_valkind_is_ref(k)) {
              got = ref_bits(vals[i].ref);
              got_hi = 0;
              if (vals[i].ref) wasm_ref_delete(vals[i].ref);
            } else {
              memcpy(&got, vals[i].v128, 8);
              memcpy(&got_hi, vals[i].v128 + 8, 8);
              if (k == WASM_I32 || k == WASM_F32) got &= 0xffffffffull;
            }
            const SpecVal *entry = want;
            int wide = entry->kind == SV_V128 || entry->kind == SV_V128_NAN ||
                       (entry->kind == SV_EITHER && (entry[1].kind == SV_V128 ||
                                                     entry[1].kind == SV_V128_NAN));
            /* a reference the table says is GC_HOST_TAG'd is the same host
               object in the any hierarchy, which wasm.h hands back as is */
            if (wasm_valkind_is_ref(k) && entry->kind == SV_EXTERNREF && (entry->bits & 2))
              got |= 2;
            if (!match_result(&want, got, got_hi)) {
              ok = 0;
              nfail++;
              out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
              out(": "); out(cmd->name); out(" returned "); put_hex(got);
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
        }
        if (trap) wasm_trap_delete(trap);
        free(vals);
        wasm_functype_delete(ft);
        (void)is_vreg_kind;
        break;
      }

      case CMD_GET: {
        unsigned target = cmd->module == 0xffffu ? cur : cmd->module;
        if (target == 0xffffu || !insts[target]) {
          fail_at(cmd->line, "no instance for the action");
          break;
        }
        const wasm_extern_t *e = export_of(target, cmd->name, cmd->name_len);
        const wasm_global_t *g = e ? wasm_extern_as_global_const(e) : NULL;
        if (!g) { fail_at(cmd->line, "unknown global export"); break; }
        wasm_val_t v;
        wasm_global_get(g, &v);
        u64 got = 0;
        if (wasm_valkind_is_ref(v.kind)) {
          got = ref_bits(v.of.ref);
          if (v.of.ref) wasm_ref_delete(v.of.ref);
        } else if (v.kind == WASM_I32 || v.kind == WASM_F32) {
          u32 b;
          memcpy(&b, &v.of, 4);
          got = b;
        } else {
          memcpy(&got, &v.of, 8);
        }
        const SpecVal *want = cmd->res;
        if (cmd->nres && !match_result(&want, got, 0)) {
          nfail++;
          out("FAIL "); out(cur_file->name); out(":"); put_u(cmd->line);
          out(": global "); out(cmd->name); out(" is "); put_hex(got);
          out(", expected "); put_hex(cmd->res[0].bits);
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
  return nfail ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    for (unsigned i = 0; i < spec_nfiles; i++) printf("%s\n", spec_files[i].name);
    return 0;
  }
  wasm_config_t *config = wasm_config_new();
  for (int i = 2; i < argc; i++) warm_config_flag_add(config, argv[i]);
  engine = wasm_engine_new_with_config(config);
  store = wasm_store_new(engine);
  for (unsigned i = 0; i < spec_nfiles; i++)
    if (!strcmp(spec_files[i].name, argv[1])) {
      int rc = run_one(&spec_files[i]);
      fflush(stderr);
      return rc;
    }
  out("spec-run: no such file: ");
  out(argv[1]);
  out("\n");
  return 2;
}
