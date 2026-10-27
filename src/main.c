#include <time.h>
#include "ir.h"
#include "target.h"
#include "component.h"
#include "canon.h"
#include "wit.h"
#include "wat.h"
#include "clink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

static const char *usage_text =
  "warmc -- WebAssembly to native ahead-of-time compiler: AArch64 for Linux\n"
  "         and macOS, x86-64 for Windows\n"
  "\n"
  "usage: warmc [options] input.wasm|input.wat ...\n"
  "\n"
  "  several core modules make one program holding them all, each compiled\n"
  "  as it would be alone; it is asked for one by its file's name, the\n"
  "  directory and the last extension left off -- the first argument, or\n"
  "  with --wasi the host's --module NAME\n"
  "\n"
  "  a module may be given in either the binary or the text format: a name\n"
  "  ending in .wat is read as text and one ending in .wasm as binary; any\n"
  "  other is binary when it begins with the binary magic\n"
  "\n"
  "  a *component* is compiled too, when nothing it exports needs conversion\n"
  "  code -- compiling one that is not says why, and --dump-link shows what\n"
  "  was resolved\n"
  "\n"
  "  -o FILE       output path (default a.out, a.exe for windows, or out.o\n"
  "                with -c)\n"
  "  -j N          compile with at most N worker threads (-jN also works).\n"
  "                Without it the count comes from the total body size and\n"
  "                the core count; WARMC_JOBS sets the same thing and -j\n"
  "                wins over it.  Fewer threads than N are used when the\n"
  "                module has too few functions to keep them busy, so -j1\n"
  "                is the one value that is always exact -- which is what\n"
  "                makes it the flag to reach for when output differs\n"
  "                between runs\n"
  "  -O0 -O1 -O2 -O3  optimisation level (default -O2)\n"
  "                O0 skips the IR optimiser (register allocation, module\n"
  "                reachability and merging identical bodies still run),\n"
  "                O1 the optimising pipeline, O2 adds guard pages,\n"
  "                inlining, devirtualisation, unrolling and gcpure.\n"
  "                O3 adds the passes that change what a program can see,\n"
  "                fma and dead-frames, described below: every optimisation\n"
  "                warmc has, where O2 keeps wasm's semantics.\n"
  "                -fno-inline turns inlining off, and src/inline.c has the\n"
  "                measurements that decided both.  Options apply in order,\n"
  "                so a -f flag goes after the -O it amends\n"
  "  -f[no-]PASS   enable or disable one pass: fold, dce, cse, licm, branch,\n"
  "                thread, nodesplit, mem, bce, strength, inline, reassoc,\n"
  "                rotate, addroff, memptr, align, regalloc, guard-pages,\n"
  "                vectorize, edge-tests, unroll, selftail, ldcomb, escape,\n"
  "                partial-escape, gloop, gssa, loopclose, cpool, ictab, reach,\n"
  "                prune, merge, devirt, reinline, throw-jump, closed-world,\n"
  "                sealed, once, gcpure, split, labcache, ifconv, mulwide,\n"
  "                barelide, small-loop-align, trampoline, go-sp, go-resume,\n"
  "                go-noret, wrap-params, fma, dead-frames, exact-trap\n"
  "                fma is off below -O3: it fuses a float multiply\n"
  "                into the add that reads it, rounded once, which is not\n"
  "                wasm's arithmetic and can change a result's last bit\n"
  "                dead-frames is off below -O3: it assumes C's\n"
  "                stack, where nothing reads a function's frame once it\n"
  "                returns, and removes the stores into it nothing reads\n"
  "                before then; wasm keeps them, so a program that reads\n"
  "                stack memory it never wrote can see other bytes\n"
  "                exact-trap is off at every level: with it, memory,\n"
  "                tables and globals hold what wasm says when a trap\n"
  "                happens -- every write before the trapping instruction\n"
  "                made, none after -- for an embedder that goes on using\n"
  "                an instance after a trap.  Without it they are\n"
  "                unspecified after one: a store a later one overwrites\n"
  "                can be left out, a global kept in a register until the\n"
  "                loop or function ends, a load that can trap moved above\n"
  "                a store.  It keeps those stores and moves no such load,\n"
  "                stores a global kept in a register before whatever can\n"
  "                trap, has the vectoriser check its ranges against the\n"
  "                memory's size, on AArch64 tests a store wider than a byte\n"
  "                and a short bulk op before it is made, where a fault on\n"
  "                the guard page could leave it half written, and turns\n"
  "                off trampoline, go-sp and dead-frames, which no flag then\n"
  "                turns back on\n"
  "                labcache and barelide build on what the collector\n"
  "                allows -- the allocation buffer kept open over a loop of\n"
  "                small allocations, the load-barrier mask kept over a loop\n"
  "                with no safepoint: turning them off says whether a\n"
  "                collection bug the optimiser brings out is one of theirs\n"
  "  -fno-loadbar  emit no GC load barriers (-floadbar, the default, emits\n"
  "                them).  Unsound for a module with GC references, which\n"
  "                relocation heals through the barrier; test/run_nobar.sh\n"
  "                uses it to show that a module without them compiles to\n"
  "                the same bytes either way\n"
  "  -fgssa        turn a global a function uses as a variable into SSA\n"
  "                values, with a parameter at each merge, where that does\n"
  "                fewer memory operations on it, counted by loop depth, or\n"
  "                where a loop touches it five times on a way round with no\n"
  "                call that can see it.  -fgssa-all does it for every such\n"
  "                global: a net loss, since a global load is one instruction\n"
  "                off the instance and a live value costs edge copies and\n"
  "                register pressure (asbench/bintree, cbench/printf and\n"
  "                hsbench/fib 4-11% slower)\n"
  "  --validate-only  validate and stop; exit 2 malformed, 3 invalid.  Given\n"
  "                several, each one's verdict a line on stdout: its path, a\n"
  "                tab, and 0 ok or the status and the message\n"
  "  --dump-component  print a component's structure, one line per definition\n"
  "  --dump-canon      print what the canonical ABI makes of each of its types\n"
  "  --dump-component-wit  render a component's interfaces back as WIT text\n"
  "  --dump-adapter    print the canonical ABI conversion code, as wasm text\n"
  "  --dump-link       print a component's instance graph, resolved\n"
  "  --dump-wit    parse a .wit document and print what it declares\n"
  "  --emit-wasm   assemble a text module to binary and stop, writing -o.\n"
  "                Given several, each is written beside itself, its last\n"
  "                extension .wasm, its verdict a line as --validate-only's\n"
  "  --wasi        link a wasi host and run the program, instead of the CLI\n"
  "                harness that invokes an export by name.  Which host is\n"
  "                decided by the input: a core module gets the\n"
  "                wasi_snapshot_preview1 one and runs _start, a component\n"
  "                gets the WASI 0.2 one and runs wasi:cli/run@0.2.x#run,\n"
  "                whose result is its exit status -- 0 or 1 -- unless it\n"
  "                calls wasi:cli/exit's exit-with-code for another.\n"
  "                A wasi:* function this host does not implement traps when\n"
  "                it is called, naming itself, rather than refusing the link\n"
  "                The host archive is found beside --rt; --wasi=PATH overrides\n"
  "                The program it produces takes --dir=HOST::GUEST to preopen\n"
  "                a directory, --env NAME=VALUE to pass one variable, and\n"
  "                --inherit-env for the whole environment.  Nothing is\n"
  "                inherited unless asked for.  --tcplisten=ADDR:PORT hands\n"
  "                the guest a non-blocking listening socket to accept on,\n"
  "                numbered after the preopened directories; without it the\n"
  "                four sock_* calls have no descriptor to work on and the\n"
  "                guest reaches no network at all\n"
  "  --symbol-prefix=P  name the module descriptor P_module, so several\n"
  "                modules can be linked into one program.  With -c and\n"
  "                several modules, one object: the i-th, from 1, as alone\n"
  "                with --symbol-prefix=Pi\n"
  "  --custom-sections=default|all|none  which of the module's custom\n"
  "                sections the output carries, in a read-only section of its\n"
  "                own (.wasm.custom, __TEXT,__wasm_custom, .wcustom) that a\n"
  "                --shared library also hands out by name:\n"
  "                PREFIX_custom_section.  default leaves out the debug\n"
  "                information: the name section, whose function names the\n"
  "                symbols carry already, and DWARF's .debug_* sections\n"
  "  --no-function-names  name each function's symbol wasm_func_N, by its\n"
  "                index, rather than as the module's name section does\n"
  "  --no-export-names  emit only the exports a name can still be needed\n"
  "                for -- _start and main -- and drop the rest of the export\n"
  "                table.  The program can then no longer be asked for an\n"
  "                export by name: the CLI harness's `PROG name args` form\n"
  "                and its export listing stop working, as does\n"
  "                warmc_find_export in an embedder.  Refused for a\n"
  "                component, where a name is how one instance is bound to\n"
  "                another\n"
  "  --print-ir    dump each function's IR before and after optimisation\n"
  "  --stats       print optimisation counters\n"
  "  --profile     count executed instructions at run time\n"
  "  --profile=P   compile for a wasm profile: full (default) or\n"
  "                deterministic.  Unrelated to the bare --profile above,\n"
  "                which is warmc's own instruction counter\n"
  "  -c            emit a relocatable object and stop\n"
  "  --shared      build a shared library (.dylib, .so, .dll) that other\n"
  "                programs call through their C FFI: each exported function\n"
  "                of i32, i64, f32 and f64 becomes a C function of its name,\n"
  "                and a header declares them with the library's helpers\n"
  "                PREFIX_last_trap, _trap_name, _memory and _bind_import.\n"
  "                PREFIX is --symbol-prefix, or the output's name less any\n"
  "                lib and extension.  Imports must be functions, which the\n"
  "                loading program binds before its first call\n"
  "  --header PATH where --shared writes the header (default PREFIX.h beside\n"
  "                the library).  A Windows library also gets PREFIX.def\n"
  "                beside it, which `lib /def:` or `llvm-dlltool -d` makes an\n"
  "                import library of\n"
  "  --target T    linux (ELF, default), macos (Mach-O) or windows (PE,\n"
  "                x64; default output a.exe).  With --system-ld the macOS\n"
  "                link needs libSystem, found through\n"
  "                `xcrun --show-sdk-path`; $WARMC_MACOS_LIBDIR overrides that\n"
  "  --rt PATH     runtime archive or object to link (default $WARMC_RT)\n"
  "  --system-ld   link with an external linker instead of doing it here:\n"
  "                ld.lld for linux; for macos ld on a macOS host and\n"
  "                ld64.lld elsewhere ($WARMC_LD overrides)\n"
  "  --ld PATH     which one, and implies --system-ld\n"
  "  --native-module NAME  call the C function of each function import of\n"
  "                module NAME, by its name, with the platform's C calling\n"
  "                convention: i32, i64, f32 and f64 alone, a pointer an i64\n"
  "                native address, and the import only ever called.  Its\n"
  "                warmc_memory_base, () -> i64, is memory 0's base, not C's,\n"
  "                and its warmc_call (or warmc_call.ANYTHING) calls the C\n"
  "                function at the address its last argument, an i64, holds.\n"
  "                AArch64's, on linux and macos; implies --system-ld\n"
  "  --link ARGS   more for the system linker, in its shell syntax: objects,\n"
  "                archives, -l and -framework; repeatable\n"
  "  --no-dead-strip  keep every section in the link, referenced or not\n"
  "  -s, --strip   leave the names of the module's and the runtime's own\n"
  "                functions out of the program's symbol table, which only\n"
  "                debuggers and profilers read\n"
  "  --keep-obj    do not delete the intermediate object\n"
  "  -v            print the link command\n"
  "  --magic=D[:64]  print the multiply, add and shift that divide by D\n"
  "                (unsigned, 32-bit or :64), and stop; several, comma-separated,\n"
  "                a line each; test/magic.py's hook\n"
  "  -h, --help    this message\n";

static const char *wasi_archive(const char *rt, const char *wasi, bool p2,
                                char *buf, size_t bufn) {
  if (!wasi) {
    const char *base = strrchr(rt, '/');
#ifdef _WIN32
    const char *bs = strrchr(rt, '\\');
    if (bs && (!base || bs > base)) base = bs;
#endif
    base = base ? base + 1 : rt;
    if (strncmp(base, "warmrt", 6))
      warmc_fatal("--wasi cannot guess the host archive from %s; "
                  "pass --wasi=PATH", rt);
    snprintf(buf, bufn, "%.*s%s%s", (int)(base - rt), rt,
             p2 ? "warmwasip2" : "warmwasi", base + 6);
    wasi = buf;
  }
  FILE *probe = fopen(wasi, "rb");
  if (!probe)
    warmc_fatal("--wasi: cannot open %s (run `make` to build it)", wasi);
  fclose(probe);
  return wasi;
}

static bool is_directory(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ends_with(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && !strcmp(s + n - m, suffix);
}

/* Every function import of a --native-module becomes a call of the C function
   of its name: the module asks for it by name, and the system linker finds it
   in what --link gives it.  Only calls: C has no wasm function to hand back
   for ref.func, a table or an export, and only the values C passes in
   registers as wasm has them.  How many there are. */
static uint32_t mark_native_imports(Module *m, const char *const *mods, uint32_t nmods) {
  uint32_t n = 0;
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) {
    Func *fn = &m->funcs[fi];
    if (!fn->imported) continue;
    const Import *im = &m->imports[fn->import_index];
    bool native = false;
    for (uint32_t k = 0; k < nmods && !native; k++)
      native = strlen(mods[k]) == im->module_len && !memcmp(mods[k], im->module, im->module_len);
    if (!native) continue;
    const CompType *t = &m->types[fn->type_index];
    bool ok = t->nresults <= 1;
    for (uint32_t k = 0; k < t->nparams && ok; k++)
      ok = t->params[k] == VT_I32 || t->params[k] == VT_I64 ||
           t->params[k] == VT_F32 || t->params[k] == VT_F64;
    for (uint32_t k = 0; k < t->nresults && ok; k++)
      ok = t->results[k] == VT_I32 || t->results[k] == VT_I64 ||
           t->results[k] == VT_F32 || t->results[k] == VT_F64;
    if (!ok)
      warmc_fatal("native import %.*s.%.*s: a C function is called with i32, i64, f32 "
                  "and f64 alone, and returns at most one of them",
                  (int)im->module_len, im->module, (int)im->name_len, im->name);
    if (!strcmp(im->name, "warmc_memory_base")) {
      if (t->nparams || t->nresults != 1 || t->results[0] != VT_I64 || !m->nmemories)
        warmc_fatal("native import %.*s.warmc_memory_base: memory 0's base, an i64 of a "
                    "module with a memory, and takes no arguments",
                    (int)im->module_len, im->module);
      fn->native_memory_base = true;
    } else if (!strcmp(im->name, "warmc_call") || !strncmp(im->name, "warmc_call.", 11)) {
      if (!t->nparams || t->params[t->nparams - 1] != VT_I64)
        warmc_fatal("native import %.*s.%s: a call of the C function at the address its "
                    "last argument, an i64, gives",
                    (int)im->module_len, im->module, im->name);
      fn->native_indirect = true;
    } else if (!im->name_len || strlen(im->name) != im->name_len) {
      warmc_fatal("native import %.*s: a C function's name, not empty and without a NUL",
                  (int)im->module_len, im->module);
    }
    fn->native = true;
    n++;
  }
  if (!n) return 0;
  for (uint32_t e = 0; e < m->nexports; e++)
    if (m->exports[e].kind == EK_FUNC && m->funcs[m->exports[e].index].native)
      warmc_fatal("export %.*s is a native import: a native import can only be called",
                  (int)m->exports[e].name_len, m->exports[e].name);
  if (m->has_start && m->funcs[m->start_func].native)
    warmc_fatal("the start function is a native import: a native import can only be called");
  for (uint32_t g = 0; g < m->nglobals; g++)
    for (uint32_t k = 0; k < m->globals[g].init.n; k++) {
      const InitInsn *x = &m->globals[g].init.ins[k];
      if (x->op == IOP_REF_FUNC && x->lo < m->nfuncs && m->funcs[x->lo].native)
        warmc_fatal("global %u holds native import %u: a native import can only be called",
                    g, (uint32_t)x->lo);
    }
  for (uint32_t e = 0; e < m->nelems; e++)
    for (uint32_t i = 0; i < m->elems[e].n; i++)
      for (uint32_t k = 0; k < m->elems[e].items[i].n; k++) {
        const InitInsn *x = &m->elems[e].items[i].ins[k];
        if (x->op == IOP_REF_FUNC && x->lo < m->nfuncs && m->funcs[x->lo].native)
          warmc_fatal("element segment %u holds native import %u: a native import can only "
                      "be called", e, (uint32_t)x->lo);
      }
  return n;
}

static const char *macos_libdir(void) {
  const char *env = warmc_getenv("WARMC_MACOS_LIBDIR");
  if (env && *env) return env;

#ifdef __APPLE__
  static char buf[4096];
  FILE *p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
  if (!p) return NULL;
  char sdk[3800];
  char *got = fgets(sdk, sizeof sdk, p);
  pclose(p);
  if (!got) return NULL;
  size_t n = strlen(sdk);
  while (n && (sdk[n - 1] == '\n' || sdk[n - 1] == '\r')) sdk[--n] = 0;
  if (!n) return NULL;

  snprintf(buf, sizeof buf, "%s/usr/lib", sdk);
  struct stat st;
  if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
  return buf;
#else
  return NULL;
#endif
}

/* The prefix of a --shared library's helpers: --symbol-prefix, or the
   output's file name less a leading lib and its extensions, as C. */
static void shared_prefix(const char *given, const char *output, char *out,
                          size_t cap) {
  const char *b = given;
  size_t n;
  if (b) {
    n = strlen(b);
  } else {
    b = strrchr(output, '/');
#ifdef _WIN32
    const char *bs = strrchr(output, '\\');
    if (bs && (!b || bs > b)) b = bs;
#endif
    b = b ? b + 1 : output;
    if (!strncmp(b, "lib", 3) && b[3] && b[3] != '.') b += 3;
    n = strcspn(b, ".");
  }
  size_t k = 0;
  if (n && b[0] >= '0' && b[0] <= '9' && k + 1 < cap) out[k++] = '_';
  for (size_t i = 0; i < n && k + 1 < cap; i++) {
    char c = b[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_';
    out[k++] = ok ? c : '_';
  }
  if (!k) { snprintf(out, cap, "lib"); return; }
  out[k] = 0;
}

static uint8_t *read_file(const char *path, size_t *len_out) {
  FILE *f = fopen(path, "rb");
  if (!f) warmc_fatal("cannot open %s", path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) warmc_fatal("cannot size %s", path);
  uint8_t *buf = xmalloc((size_t)n + 1);
  if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) warmc_fatal("short read on %s", path);
  fclose(f);
  *len_out = (size_t)n;
  return buf;
}

/* What an -O level decides; every other pass is on at every level but -O0,
   which skips the IR optimiser, and is the -f flags' to change.  Each -O
   sets all of these, so the last one given is the level. */
static void set_level(OptOpts *o, int level) {
  o->level = level;
  o->align = o->split = o->trampoline = o->go_sp = level >= 1;
  o->guard_pages = o->inl = o->devirt = o->unroll = o->gcpure = level >= 2;
  /* -O3: also the passes that change what a program can see */
  o->fma = o->dead_frames = level >= 3;
}

static void print_stats(void) {
  fprintf(stderr,
          "warmc stats: folded=%llu dce=%llu cse=%llu licm=%llu branch=%llu\n"
          "             loads_fwd=%llu dead_stores=%llu checks_removed=%llu params=%llu\n"
          "             strength=%llu inlined=%llu rotated=%llu vectorised=%llu\n"
          "             self_tail=%llu ldcomb=%llu spills=%llu insts=%llu\n"
          "             ldpairs=%llu stpairs=%llu cold_frames=%llu early_rets=%llu entry_checks=%llu loop_phases=%llu"
          " exact_store_checks=%llu exact_memop_checks=%llu\n"
          "             reloads=%llu caller_saves=%llu csa_restores=%llu"
          " v128_spills=%llu rb_trips=%llu split_cuts=%llu split_invs=%llu pieces=%llu"
          " loop_pieces=%llu recomputes=%llu dead_frame_stores=%llu tramp_funcs=%llu gosp_funcs=%llu"
          " go_resume_pruned=%llu go_resume_split=%llu go_noret_calls=%llu wrap_params=%llu\n"
          "             escape_removed=%llu escape_forwarded=%llu escape_made=%llu gloop=%llu\n"
          "             loop_closed=%llu reassoc=%llu cmp_reused=%llu csel_fused=%llu mul_wide=%llu fma=%llu gssa=%llu bitrevs=%llu small_loops=%llu\n"
          "             bar_masks=%llu bar_elided=%llu\n"
          "             devirt_ref=%llu devirt_indirect=%llu"
          " devirt_null_kept=%llu const_reads=%llu throw_jumps=%llu"
          " inl_more=%llu\n"
          "             inl_flat=%llu inl_spliced=%llu inl_once=%llu"
          " inl_retcall_sites=%llu inl_trans=%llu inl_escape=%llu rej_size=%llu"
          " rej_budget=%llu rej_tail=%llu rej_retcalls=%llu rej_pads=%llu"
          " rej_score=%llu rej_depth=%llu rej_rec=%llu max_frame=%llu\n"
          "             unrolled=%llu unr_guarded=%llu unr_rej_shape=%llu"
          " unr_rej_body=%llu unr_rej_exit=%llu unr_rej_trip=%llu"
          " unr_rej_escape=%llu unr_addr_folded=%llu gc_nullchk=%llu\n"
          "             slot_tests=%llu slot_nullchks=%llu once_dead=%llu once_dominated=%llu once_looped=%llu\n",
          (unsigned long long)g_stats.folded, (unsigned long long)g_stats.dce_removed,
          (unsigned long long)g_stats.cse_removed, (unsigned long long)g_stats.licm_hoisted,
          (unsigned long long)g_stats.branch_simplified,
          (unsigned long long)g_stats.loads_forwarded,
          (unsigned long long)g_stats.stores_removed,
          (unsigned long long)g_stats.checks_removed,
          (unsigned long long)g_stats.params_removed,
          (unsigned long long)g_stats.strength_reduced,
          (unsigned long long)g_stats.inlined,
          (unsigned long long)g_stats.rotated,
          (unsigned long long)g_stats.vectorized,
          (unsigned long long)g_stats.self_tail,
          (unsigned long long)g_stats.loads_combined,
          (unsigned long long)g_stats.spills,
          (unsigned long long)g_stats.insts_emitted,
          (unsigned long long)g_stats.ldpairs,
          (unsigned long long)g_stats.stpairs,
          (unsigned long long)g_stats.cold_frames,
          (unsigned long long)g_stats.early_rets,
          (unsigned long long)g_stats.entry_checks,
          (unsigned long long)g_stats.loop_phases,
          (unsigned long long)g_stats.exact_store_checks,
          (unsigned long long)g_stats.exact_memop_checks,
          (unsigned long long)g_stats.reloads,
          (unsigned long long)g_stats.caller_saves,
          (unsigned long long)g_stats.csa_restores,
          (unsigned long long)g_stats.spills_v128_xcall,
          (unsigned long long)g_stats.range_budget_trips,
          (unsigned long long)g_stats.split_cuts,
          (unsigned long long)g_stats.split_invs,
          (unsigned long long)g_stats.pieces,
          (unsigned long long)g_stats.loop_pieces,
          (unsigned long long)g_stats.recomputes,
          (unsigned long long)g_stats.dead_frame_stores,
          (unsigned long long)g_stats.tramp_funcs,
          (unsigned long long)g_stats.gosp_funcs,
          (unsigned long long)g_stats.go_resume_pruned,
          (unsigned long long)g_stats.go_resume_split,
          (unsigned long long)g_stats.go_noret_calls,
          (unsigned long long)g_stats.wrap_params,
          (unsigned long long)g_stats.escape_removed,
          (unsigned long long)g_stats.escape_forwarded,
          (unsigned long long)g_stats.escape_made,
          (unsigned long long)g_stats.globals_promoted,
          (unsigned long long)g_stats.loops_closed,
          (unsigned long long)g_stats.reassoc,
          (unsigned long long)g_stats.cmp_reused,
          (unsigned long long)g_stats.csel_fused,
          (unsigned long long)g_stats.mul_wide,
          (unsigned long long)g_stats.fma_fused,
          (unsigned long long)g_stats.globals_ssa,
          (unsigned long long)g_stats.bitrevs,
          (unsigned long long)g_stats.small_loops,
          (unsigned long long)g_stats.bar_masks,
          (unsigned long long)g_stats.bar_elided,
          (unsigned long long)g_stats.devirt_ref,
          (unsigned long long)g_stats.devirt_indirect,
          (unsigned long long)g_stats.devirt_null_kept,
          (unsigned long long)g_stats.const_reads,
          (unsigned long long)g_stats.throw_jumps,
          (unsigned long long)g_stats.inl_more,
          (unsigned long long)g_stats.inl_flat,
          (unsigned long long)g_stats.inl_spliced,
          (unsigned long long)g_stats.inl_once_forced,
          (unsigned long long)g_stats.inl_retcall_sites,
          (unsigned long long)g_stats.inl_transitive,
          (unsigned long long)g_stats.inl_escape,
          (unsigned long long)g_stats.inl_rej_size,
          (unsigned long long)g_stats.inl_rej_budget,
          (unsigned long long)g_stats.inl_rej_tailcall,
          (unsigned long long)g_stats.inl_rej_retcalls,
          (unsigned long long)g_stats.inl_rej_pads,
          (unsigned long long)g_stats.inl_rej_score,
          (unsigned long long)g_stats.inl_rej_depth,
          (unsigned long long)g_stats.inl_rej_recursion,
          (unsigned long long)g_stats.max_frame_bytes,
          (unsigned long long)g_stats.unrolled,
          (unsigned long long)g_stats.unr_guarded,
          (unsigned long long)g_stats.unr_rej_shape,
          (unsigned long long)g_stats.unr_rej_body,
          (unsigned long long)g_stats.unr_rej_exit,
          (unsigned long long)g_stats.unr_rej_trip,
          (unsigned long long)g_stats.unr_rej_escape,
          (unsigned long long)g_stats.unr_addr_folded,
          (unsigned long long)g_stats.gc_nullchk_removed,
          (unsigned long long)g_stats.slot_tests,
          (unsigned long long)g_stats.slot_nullchks,
          (unsigned long long)g_stats.once_dead,
          (unsigned long long)g_stats.once_dominated,
          (unsigned long long)g_stats.once_looped);
}

/* --validate-only given several inputs: each one's verdict, a line apiece
   on stdout -- PATH, a tab, 0 and "ok", or the status warmc exits with and
   its message -- and the next taken all the same.  One process for a
   thousand verdicts, where each was one: run_components.py's sweep of
   mutated components, whose every process the sanitisers slow to start. */
static void validate_several(const char **inputs, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    DiagCatch c;
    if (setjmp(c.jb)) {
      warmc_diag_catch(NULL);
      printf("%s\t%d %s: %s\n", inputs[i], c.code, c.tag, c.msg ? c.msg : "");
      free(c.msg);
      fflush(stdout);
      continue;
    }
    warmc_diag_catch(&c);
    size_t len;
    uint8_t *bin = read_file(inputs[i], &len);
    if (ends_with(inputs[i], ".wat") || (!ends_with(inputs[i], ".wasm") && wat_is_text(bin, len))) {
      size_t blen;
      uint8_t *assembled = wat_assemble((const char *)bin, len, inputs[i], &blen);
      xfree(bin);
      bin = assembled;
      len = blen;
    }
    if (wasm_is_component(bin, len)) {
      comp_free(comp_parse(bin, len));
    } else {
      Module *m = wasm_parse(bin, len);
      wasm_validate(m);
      module_free(m);
    }
    xfree(bin);
    warmc_diag_catch(NULL);
    printf("%s\t0 ok\n", inputs[i]);
    fflush(stdout);
  }
}

/* --emit-wasm given several inputs: each text module assembled and written
   beside it, its last extension .wasm, and its verdict a line on stdout as
   validate_several's.  The spec suite's text modules and run_wat.py's
   fixtures, thousands of them, were a process each. */
static void emit_several(const char **inputs, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    DiagCatch c;
    if (setjmp(c.jb)) {
      warmc_diag_catch(NULL);
      printf("%s\t%d %s: %s\n", inputs[i], c.code, c.tag, c.msg ? c.msg : "");
      free(c.msg);
      fflush(stdout);
      continue;
    }
    warmc_diag_catch(&c);
    size_t len;
    uint8_t *bin = read_file(inputs[i], &len);
    if (!ends_with(inputs[i], ".wat") && (ends_with(inputs[i], ".wasm") || !wat_is_text(bin, len))) {
      xfree(bin);
      warmc_fatal("--emit-wasm takes a text module; %s is already binary", inputs[i]);
    }
    size_t blen;
    uint8_t *out = wat_assemble((const char *)bin, len, inputs[i], &blen);
    xfree(bin);
    const char *b = strrchr(inputs[i], '/');
    b = b ? b + 1 : inputs[i];
    const char *dot = strrchr(b, '.');
    size_t stem = dot && dot != b ? (size_t)(dot - inputs[i]) : strlen(inputs[i]);
    char path[4096];
    snprintf(path, sizeof path, "%.*s.wasm", (int)stem, inputs[i]);
    FILE *f = fopen(path, "wb");
    if (!f) { xfree(out); warmc_fatal("cannot write %s", path); }
    bool ok = !blen || fwrite(out, 1, blen, f) == blen;
    fclose(f);
    xfree(out);
    if (!ok) warmc_fatal("short write on %s", path);
    warmc_diag_catch(NULL);
    printf("%s\t0 ok\n", inputs[i]);
    fflush(stdout);
  }
}

/* A core module read from path, the text format assembled */
static Module *read_module(const char *path, uint8_t **binp) {
  size_t len;
  uint8_t *bin = read_file(path, &len);
  if (ends_with(path, ".wat") || (!ends_with(path, ".wasm") && wat_is_text(bin, len))) {
    size_t blen;
    uint8_t *assembled = wat_assemble((const char *)bin, len, path, &blen);
    xfree(bin);
    bin = assembled;
    len = blen;
  }
  if (wasm_is_component(bin, len))
    warmc_fatal("%s is a component; several modules make one program of core modules only", path);
  Module *m = wasm_parse(bin, len);
  wasm_validate(m);
  *binp = bin;
  return m;
}

/* Several core modules, one program: each compiled as it would be alone,
   under a symbol prefix of its own, and a table of them by name -- what
   rt/modules.c holds for one -- which the program is asked for by name:
   the CLI harness's first argument, the WASI host's --module.  Only the
   one named is made an instance, so each keeps the code a program of its
   own would have (sole_instance as there).  A test suite links hundreds of
   modules into one program this way where it linked hundreds of programs,
   and a program's first run, which macOS checks, is what those cost.
   With -c and --symbol-prefix=P the object is the caller's to link: the
   i-th module (from 1) is as alone with --symbol-prefix=Pi -- its
   descriptor Pi_module, its functions' names its own -- and there is no
   table: the spec suite's modules of one .wast, an object a level where
   each was one. */
static void compile_several(const char **inputs, uint32_t n, OptOpts *opt, TargetFmt fmt,
                            const char *output, bool obj_only, bool keep_obj, bool want_wasi,
                            const char *wasi, const char *rt, const char *entry_sym,
                            bool dead_strip, const char *sym_prefix) {
  char **names = xcalloc(n, sizeof(char *)), **prefixes = xcalloc(n, sizeof(char *));
  Module **mods = xcalloc(n, sizeof(Module *));
  uint8_t **bins = xcalloc(n, sizeof(uint8_t *));
  ObjBuilder o;
  memset(&o, 0, sizeof o);
  opt->sole_instance = !obj_only;
  for (uint32_t i = 0; i < n; i++) {
    /* named by the file's name, its directory and last extension left off */
    const char *b = strrchr(inputs[i], '/');
    b = b ? b + 1 : inputs[i];
    const char *dot = strrchr(b, '.');
    size_t len = dot && dot != b ? (size_t)(dot - b) : strlen(b);
    names[i] = xstrndup(b, len);
    for (uint32_t k = 0; k < i && !sym_prefix; k++)
      if (!strcmp(names[k], names[i]))
        warmc_fatal("%s and %s would both be the module %s", inputs[k], inputs[i], names[i]);
    char pfx[256];
    if (sym_prefix) snprintf(pfx, sizeof pfx, "%s%u", sym_prefix, i + 1);
    else snprintf(pfx, sizeof pfx, "warmc_m%u", i);
    prefixes[i] = xstrndup(pfx, strlen(pfx));
    mods[i] = read_module(inputs[i], &bins[i]);
    if (!obj_only && !want_wasi)
      for (uint32_t k = 0; k < mods[i]->nimports; k++) {
        const Import *im = &mods[i]->imports[k];
        if (im->module_len == 22 && !memcmp(im->module, "wasi_snapshot_preview1", 22))
          warmc_fatal("%s imports wasi_snapshot_preview1.%.*s, and nothing linked into the "
                      "program provides it; pass --wasi to link the host that does",
                      inputs[i], (int)im->name_len, im->name);
      }
    codegen_set_symbol_prefix(prefixes[i]);
    if (!sym_prefix) codegen_set_display_prefix(names[i]);
    codegen_rename_funcs(mods[i], prefixes[i]);
    compile_module(mods[i], &o, opt);
  }
  codegen_set_display_prefix("");
  if (!sym_prefix)
    codegen_emit_module_table(&o, (const char *const *)prefixes, (const char *const *)names, n);
  if (opt->stats) print_stats();

  char objpath[4096];
  if (obj_only) snprintf(objpath, sizeof objpath, "%s", output);
  else snprintf(objpath, sizeof objpath, "%s.o", output);
  if (fmt == TGT_MACHO) write_macho64(&o, objpath);
  else if (fmt == TGT_PE) write_coff64(&o, objpath);
  else write_elf64(&o, objpath);
  if (!obj_only) {
    if (!keep_obj) warmc_temp_output(objpath);
    warmc_link_exec(fmt, output, objpath, want_wasi ? wasi : NULL, rt, entry_sym, dead_strip);
    if (!keep_obj) { remove(objpath); warmc_temp_output(NULL); }
  }
  for (uint32_t i = 0; i < n; i++) {
    module_free(mods[i]);
    xfree(bins[i]);
    xfree(names[i]);
    xfree(prefixes[i]);
  }
  xfree(mods); xfree(bins); xfree(names); xfree(prefixes);
}

static int warmc_main(int argc, char **argv);

/* ---- WARMC_CACHE=DIR: what a compile made, kept under what it made it of --
   The key is a digest of warmc itself, every argument (the output by its
   file name alone, which a program's signature carries), every file an
   argument names, every archive beside the runtime, which the link may take
   from, and every WARMC_ setting but those only a program reads
   (warmc_runtime_setting: the collector's, the random seed), so that the
   GC suite run in each of its modes compiles once.  What is kept is the
   exit status, what warmc said on stdout and stderr, and the output: a
   compile with a key already kept makes its output a hard link to the kept
   one -- a program macOS has checked on its first run is not checked again
   -- says what it said then, and exits as it did, a module refused as
   malformed or invalid included, and --validate-only's and --dump-*'s
   verdicts, which write no file.  The test suites compile and refuse the
   same modules the same way run after run: make check sets it.  Not for
   what writes more than the output (--shared, --header, --keep-obj,
   --emit-wasm), --print-ir, an external linker, whose own files are not in
   the key, a compile that crashed or was killed, nor in a sanitiser's
   build, whose report is the point.  The writers remove what they write
   over, which may be an entry under another name. */
#if !defined(_WIN32) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define NO_COMPILE_CACHE 1
#endif
#endif
#else
#define NO_COMPILE_CACHE 1
#endif

#ifndef NO_COMPILE_CACHE
static struct {
  char entry[4096];       /* DIR/xx/KEY: status, err, and stdout and out */
  char err[4200], out1[4200];   /* where stderr and stdout go meanwhile */
  const char *output;     /* -o's file; NULL for a command that writes none */
  int real_err, real_out; /* the stderr and stdout warmc was given */
  bool done;
} g_cc = { .real_err = -1, .real_out = -1 };

static bool cc_regular(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static void cc_digest_file(WarmDigest *d, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { warmc_digest_update(d, "-", 1); return; }
  static char buf[1 << 16];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) warmc_digest_update(d, buf, n);
  fclose(f);
}

static int cc_cmp(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool cc_key(int argc, char **argv, char hex[65]) {
  static const char *const no[] = { "--shared", "--header", "--keep-obj", "--emit-wasm",
    "--print-ir", "--system-ld", "--ld", "--help", "-h" };
  const char *rt = getenv("WARMC_RT");
  bool says = false;    /* a command whose result is what it says */
  for (int i = 1; i < argc; i++) {
    for (size_t k = 0; k < sizeof no / sizeof no[0]; k++) {
      size_t n = strlen(no[k]);
      if (!strncmp(argv[i], no[k], n) && (argv[i][n] == 0 || argv[i][n] == '=')) return false;
    }
    if (!strncmp(argv[i], "--dump-", 7) || !strcmp(argv[i], "--validate-only")) says = true;
    if (!strcmp(argv[i], "-o") && i + 1 < argc) g_cc.output = argv[i + 1];
    if (!strcmp(argv[i], "--rt") && i + 1 < argc) rt = argv[i + 1];
    if (!strncmp(argv[i], "--rt=", 5)) rt = argv[i] + 5;
  }
  /* a file of its own: never a device, which a link would replace */
  struct stat ost;
  if (g_cc.output && stat(g_cc.output, &ost) == 0 && !S_ISREG(ost.st_mode)) return false;
  if (!g_cc.output && !says) return false;

  WarmDigest d;
  warmc_digest_init(&d);
  warmc_digest_update(&d, "warmc compile cache 2", 22);
  char self[4096];
  uint32_t sn = sizeof self;
#ifdef __APPLE__
  if (_NSGetExecutablePath(self, &sn) != 0) return false;
#else
  ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
  if (r <= 0) return false;
  self[r] = 0;
  (void)sn;
#endif
  cc_digest_file(&d, self);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      const char *b = strrchr(argv[++i], '/');
      warmc_digest_update(&d, "-o", 3);
      warmc_digest_update(&d, b ? b + 1 : argv[i], strlen(b ? b + 1 : argv[i]) + 1);
      continue;
    }
    warmc_digest_update(&d, argv[i], strlen(argv[i]) + 1);
    const char *eq = argv[i][0] == '-' ? strchr(argv[i], '=') : NULL;
    const char *path = eq ? eq + 1 : argv[i];
    if (cc_regular(path)) cc_digest_file(&d, path);
  }
  if (rt) {
    char dir[4096];
    const char *slash = strrchr(rt, '/');
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - rt) : 1, slash ? rt : ".");
    DIR *dp = opendir(dir);
    char *names[512];
    int nn = 0;
    struct dirent *e;
    while (dp && (e = readdir(dp)) && nn < 512) {
      size_t n = strlen(e->d_name);
      if (n > 2 && !strcmp(e->d_name + n - 2, ".a")) names[nn++] = xstrndup(e->d_name, n);
    }
    if (dp) closedir(dp);
    qsort(names, (size_t)nn, sizeof *names, cc_cmp);
    for (int k = 0; k < nn; k++) {
      char p[8192];
      snprintf(p, sizeof p, "%s/%s", dir, names[k]);
      warmc_digest_update(&d, names[k], strlen(names[k]) + 1);
      cc_digest_file(&d, p);
      xfree(names[k]);
    }
  }
  int ne = 0;
  for (char **e = environ; *e; e++) ne++;
  char **env = xcalloc((size_t)ne + 1, sizeof(char *));
  int k = 0;
  for (char **e = environ; *e; e++) {
    const char *eq = strchr(*e, '=');
    char name[256];
    snprintf(name, sizeof name, "%.*s", eq ? (int)(eq - *e) : 0, *e);
    if (!strncmp(*e, "WARMC_", 6) && !warmc_runtime_setting(name)) env[k++] = *e;
  }
  qsort(env, (size_t)k, sizeof *env, cc_cmp);
  for (int i = 0; i < k; i++) warmc_digest_update(&d, env[i], strlen(env[i]) + 1);
  xfree(env);

  uint8_t h[32];
  warmc_digest_final(&d, h);
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
  return true;
}

static void cc_copy_from(int in, int fd) {
  char buf[1 << 14];
  ssize_t n;
  while ((n = read(in, buf, sizeof buf)) > 0)
    if (write(fd, buf, (size_t)n) != n) break;
}

static void cc_copy_fd(const char *path, int fd) {
  int in = open(path, O_RDONLY);
  if (in < 0) return;
  cc_copy_from(in, fd);
  close(in);
}

static int cc_open(const char *entry, const char *name) {
  char p[4400];
  snprintf(p, sizeof p, "%s/%s", entry, name);
  return open(p, O_RDONLY);
}

static bool cc_link_or_copy(const char *from, const char *to) {
  warmc_remove_file(to);
  if (link(from, to) == 0) return true;
  struct stat st;
  if (stat(from, &st) != 0) return false;
  int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
  if (out < 0) return false;
  cc_copy_fd(from, out);
  close(out);
  return true;
}

/* stdout and stderr back where they were, what this compile said on them
   passed on, and the files that held it gone unless an entry took them */
static void cc_restore(void) {
  if (g_cc.real_err < 0) return;
  fflush(stdout);
  fflush(stderr);
  cc_copy_fd(g_cc.out1, g_cc.real_out);
  cc_copy_fd(g_cc.err, g_cc.real_err);
  dup2(g_cc.real_out, 1);
  dup2(g_cc.real_err, 2);
  close(g_cc.real_out);
  close(g_cc.real_err);
  g_cc.real_err = g_cc.real_out = -1;
  remove(g_cc.out1);
  remove(g_cc.err);
}

/* the entry: the exit status, what was said, and the output a success made
   -- unless the compile read a setting the key leaves out */
static void cc_keep(int rc) {
  if (g_cc.real_err < 0 || rc < 0 || rc > 3 || warmc_runtime_setting_read()) return;
  fflush(stdout);
  fflush(stderr);
  char tmp[4300], p[4400];
  snprintf(tmp, sizeof tmp, "%s.%d", g_cc.entry, (int)getpid());
  if (mkdir(tmp, 0755) != 0) return;
  bool ok = true;
  if (rc == 0 && g_cc.output && cc_regular(g_cc.output)) {
    snprintf(p, sizeof p, "%s/out", tmp);
    ok = cc_link_or_copy(g_cc.output, p);
  }
  snprintf(p, sizeof p, "%s/err", tmp);
  ok = ok && cc_link_or_copy(g_cc.err, p);
  snprintf(p, sizeof p, "%s/stdout", tmp);
  ok = ok && cc_link_or_copy(g_cc.out1, p);
  snprintf(p, sizeof p, "%s/status", tmp);
  FILE *f = ok ? fopen(p, "w") : NULL;
  if (f) { fprintf(f, "%d\n", rc); fclose(f); }
  if (f && rename(tmp, g_cc.entry) == 0) return;
  const char *const parts[] = { "out", "err", "stdout", "status" };
  for (size_t i = 0; i < 4; i++) { snprintf(p, sizeof p, "%s/%s", tmp, parts[i]); remove(p); }
  rmdir(tmp);
}

/* a compile that ends by exit(): a diagnostic's verdict is kept */
static void cc_at_exit(void) {
  if (warmc_exit_status) cc_keep(warmc_exit_status);
  cc_restore();
}

/* the kept status where the entry was there (its output made of it, what
   it said said again), or -1 */
static int cc_begin(int argc, char **argv) {
  const char *dir = getenv("WARMC_CACHE");
  char hex[65];
  if (!dir || !*dir || !cc_key(argc, argv, hex)) return -1;
  char sub[4200], p[4300];
  snprintf(sub, sizeof sub, "%s/%.2s", dir, hex);
  snprintf(g_cc.entry, sizeof g_cc.entry, "%s/%s", sub, hex);
  /* every file of the entry open before any is used: make check trims
     the cache while a later run's compiles may read it, and what is open
     stays whole -- an entry is there in full, or it is not there */
  int fst = cc_open(g_cc.entry, "status"), fso = cc_open(g_cc.entry, "stdout");
  int fse = cc_open(g_cc.entry, "err");
  int rc = -1, fout = -1;
  char buf[32];
  ssize_t n = fst >= 0 && fso >= 0 && fse >= 0 ? read(fst, buf, sizeof buf - 1) : -1;
  if (n > 0) {
    buf[n] = 0;
    if (sscanf(buf, "%d", &rc) != 1) rc = -1;
  }
  if (rc == 0 && g_cc.output) {
    struct stat st;
    fout = cc_open(g_cc.entry, "out");
    if (fout < 0 || fstat(fout, &st) != 0 || !S_ISREG(st.st_mode)) {
      rc = -1;
    } else {
      /* a hard link, which a program macOS has checked keeps; a copy of
         what is open where the name is gone */
      snprintf(p, sizeof p, "%s/out", g_cc.entry);
      warmc_remove_file(g_cc.output);
      if (link(p, g_cc.output) != 0) {
        int o = open(g_cc.output, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
        if (o < 0) rc = -1;
        else { cc_copy_from(fout, o); close(o); }
      }
    }
  }
  if (rc >= 0) {
    fflush(stdout);
    fflush(stderr);
    cc_copy_from(fso, 1);
    cc_copy_from(fse, 2);
    utimes(g_cc.entry, NULL);
  }
  const int fds[] = { fst, fso, fse, fout };
  for (size_t i = 0; i < 4; i++) if (fds[i] >= 0) close(fds[i]);
  if (rc >= 0) return rc;
  mkdir(dir, 0755);
  mkdir(sub, 0755);
  snprintf(g_cc.err, sizeof g_cc.err, "%s.%d.err", g_cc.entry, (int)getpid());
  snprintf(g_cc.out1, sizeof g_cc.out1, "%s.%d.out", g_cc.entry, (int)getpid());
  int fe = open(g_cc.err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  int fo = open(g_cc.out1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fe < 0 || fo < 0) {
    if (fe >= 0) close(fe);
    if (fo >= 0) close(fo);
    return -1;
  }
  fflush(stdout);
  fflush(stderr);
  g_cc.real_err = dup(2);
  g_cc.real_out = dup(1);
  dup2(fe, 2);
  dup2(fo, 1);
  close(fe);
  close(fo);
  atexit(cc_at_exit);
  return -1;
}
#endif

int main(int argc, char **argv) {
#ifndef NO_COMPILE_CACHE
  int kept = cc_begin(argc, argv);
  if (kept >= 0) return kept;
  int rc = warmc_main(argc, argv);
  cc_keep(rc);
  cc_restore();
  return rc;
#else
  return warmc_main(argc, argv);
#endif
}

static int warmc_main(int argc, char **argv) {
#ifdef _WIN32
  /* Dumps are byte-compared against golden files; keep the streams free
     of CRLF translation so output matches the POSIX hosts exactly. */
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  const char *input = NULL, *output = NULL, *target = "linux";
  const char **inputs = xmalloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
  uint32_t ninputs = 0;
  const char *rt = warmc_getenv("WARMC_RT");
  const char *ld = warmc_getenv("WARMC_LD");
  const char *wasi = warmc_getenv("WARMC_WASI");
  bool want_wasi = false;
  bool obj_only = false, keep_obj = false, verbose = false, validate_only = false;
  bool dead_strip = true;
  bool shared = false;
  const char *header = NULL, *sym_prefix = NULL;

  bool system_ld = warmc_getenv("WARMC_SYSTEM_LD") != NULL;
  const char *native_mods[64];
  uint32_t nnative_mods = 0;
  char link_extra[8192] = "";
  bool dump_component = false, dump_wit = false, emit_wasm = false;
  bool dump_canon = false;
  bool dump_adapter = false, dump_cwit = false, dump_link = false;

  OptOpts opt;
  memset(&opt, 0, sizeof opt);
  set_level(&opt, 2);
  opt.fold = opt.dce = opt.cse = opt.licm = opt.branch = true;
  opt.thread = opt.nodesplit = true;
  opt.gssa = true;
  opt.small_loop_align = true;
  opt.memopt = opt.bce = opt.strength = true;

  opt.rotate = true;
  opt.addroff = true;
  opt.memptr = true;
  opt.regalloc = true;
  opt.vectorize = true;
  opt.edgetests = true;
  opt.selftail = true;
  opt.ldcomb = true;
  opt.escape = true;
  opt.pescape = true;
  opt.gloop = true;
  opt.loopclose = true;
  opt.cpool = true;
  opt.ictab = true;
  opt.reassoc = true;
  opt.labcache = true;
  opt.barelide = true;
  opt.reach = true;
  opt.prune = true;
  opt.reinline = true;
  opt.throwjump = true;
  opt.ifconv = true;
  opt.mulwide = true;
  opt.closed_world = true;
  opt.sealed = true;
  opt.once = true;
  opt.go_resume = opt.go_noret = true;
  opt.wrap_params = true;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { fputs(usage_text, stdout); return 0; }
    else if (!strcmp(a, "-c")) obj_only = true;
    else if (!strcmp(a, "-v")) verbose = true;
    else if (!strncmp(a, "-j", 2)) {
      const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
      char *end = NULL;
      long n = v ? strtol(v, &end, 10) : 0;
      if (!v || !*v || *end || n < 1) {
        fprintf(stderr, "warmc: -j wants a worker count of 1 or more\n");
        return 1;
      }
      compile_set_jobs(n);
    }
    else if (!strcmp(a, "--keep-obj")) keep_obj = true;
    else if (!strcmp(a, "--no-dead-strip")) dead_strip = false;
    else if (!strcmp(a, "-s") || !strcmp(a, "--strip")) link_set_strip(true);
    else if (!strcmp(a, "--no-export-names")) g_no_export_names = true;
    else if (!strcmp(a, "--no-function-names")) codegen_set_function_names(false);
    else if (!strncmp(a, "--custom-sections=", 18)) {
      const char *v = a + 18;
      if (!strcmp(v, "default")) codegen_set_custom_sections(CUSTOM_DEFAULT);
      else if (!strcmp(v, "all")) codegen_set_custom_sections(CUSTOM_ALL);
      else if (!strcmp(v, "none")) codegen_set_custom_sections(CUSTOM_NONE);
      else warmc_fatal("--custom-sections takes default, all or none, not %s", v);
    }
    else if (!strcmp(a, "-fno-loadbar")) codegen_set_load_barrier(false);
    else if (!strcmp(a, "-floadbar")) codegen_set_load_barrier(true);
    else if (!strncmp(a, "--symbol-prefix=", 16)) sym_prefix = a + 16;
    else if (!strcmp(a, "--shared")) shared = true;
    else if (!strncmp(a, "--header=", 9)) header = a + 9;
    else if (!strcmp(a, "--header") && i + 1 < argc) header = argv[++i];
    else if (!strncmp(a, "--target=", 9)) target = a + 9;
    else if (!strncmp(a, "--rt=", 5)) rt = a + 5;
    else if (!strncmp(a, "--ld=", 5)) { ld = a + 5; system_ld = true; }
    else if (!strcmp(a, "-o") && i + 1 < argc) output = argv[++i];
    else if (!strcmp(a, "--target") && i + 1 < argc) target = argv[++i];
    else if (!strcmp(a, "--rt") && i + 1 < argc) rt = argv[++i];
    else if (!strcmp(a, "--ld") && i + 1 < argc) { ld = argv[++i]; system_ld = true; }
    else if (!strcmp(a, "--system-ld")) system_ld = true;
    else if ((!strcmp(a, "--native-module") && i + 1 < argc) ||
             !strncmp(a, "--native-module=", 16)) {
      if (nnative_mods == sizeof native_mods / sizeof native_mods[0])
        warmc_fatal("too many --native-module options");
      native_mods[nnative_mods++] = a[15] == '=' ? a + 16 : argv[++i];
    }
    else if ((!strcmp(a, "--link") && i + 1 < argc) || !strncmp(a, "--link=", 7)) {
      const char *v = a[6] == '=' ? a + 7 : argv[++i];
      if (strlen(link_extra) + strlen(v) + 2 > sizeof link_extra)
        warmc_fatal("--link: too long");
      strcat(link_extra, " ");
      strcat(link_extra, v);
    }

    else if (!strncmp(a, "--magic=", 8)) {
      /* D[:64][,D[:64]]...: a line for each, test/magic.py asking for
         thousands in one process */
      const char *arg = a + 8;
      for (;;) {
        bool wide = false;
        char *end = NULL;
        unsigned long long d = strtoull(arg, &end, 0);
        if (end && *end == ':') { wide = !strncmp(end + 1, "64", 2); end += 3; }
        Magic mg = opt_magic_unsigned((uint64_t)d, wide);
        printf("%llu %d %d\n", (unsigned long long)mg.m, mg.add, mg.sh);
        if (!end || *end != ',') break;
        arg = end + 1;
      }
      return 0;
    }
    else if (a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3' && !a[3])
      set_level(&opt, a[2] - '0');
    else if (!strcmp(a, "--validate-only")) validate_only = true;
    else if (!strcmp(a, "--dump-component")) dump_component = true;
    else if (!strcmp(a, "--dump-canon")) dump_canon = true;
    else if (!strcmp(a, "--dump-component-wit")) dump_cwit = true;
    else if (!strcmp(a, "--dump-adapter")) dump_adapter = true;
    else if (!strcmp(a, "--dump-link")) dump_link = true;
    else if (!strcmp(a, "--dump-wit")) dump_wit = true;
    else if (!strcmp(a, "--emit-wasm")) emit_wasm = true;
    else if (!strcmp(a, "--wasi")) want_wasi = true;
    else if (!strncmp(a, "--wasi=", 7)) { want_wasi = true; wasi = a + 7; }
    else if (!strcmp(a, "--print-ir")) opt.print_ir = true;
    else if (!strcmp(a, "--stats")) opt.stats = true;

    else if (!strcmp(a, "--profile")) opt.profile = true;
    else if (!strncmp(a, "--profile=", 10)) {
      const char *p = a + 10;
      if (!strcmp(p, "deterministic")) opt.det_profile = true;
      else if (!strcmp(p, "full")) opt.det_profile = false;
      else {
        fprintf(stderr, "warmc: unknown profile '%s' "
                        "(full or deterministic)\n", p);
        return 1;
      }
    }
    else if (!strncmp(a, "-f", 2)) {
      const char *name = a + 2;
      bool on = true;
      if (!strncmp(name, "no-", 3)) { on = false; name += 3; }
      if (!strcmp(name, "fold")) opt.fold = on;
      else if (!strcmp(name, "dce")) opt.dce = on;
      else if (!strcmp(name, "cse")) opt.cse = on;
      else if (!strcmp(name, "licm")) opt.licm = on;
      else if (!strcmp(name, "branch")) opt.branch = on;
      else if (!strcmp(name, "thread")) opt.thread = on;
      else if (!strcmp(name, "nodesplit")) opt.nodesplit = on;
      else if (!strcmp(name, "mem")) opt.memopt = on;
      else if (!strcmp(name, "bce")) opt.bce = on;
      else if (!strcmp(name, "strength")) opt.strength = on;
      else if (!strcmp(name, "inline")) opt.inl = on;
      else if (!strcmp(name, "reassoc")) opt.reassoc = on;
      else if (!strcmp(name, "rotate")) opt.rotate = on;
      else if (!strcmp(name, "addroff")) opt.addroff = on;
      else if (!strcmp(name, "memptr")) opt.memptr = on;
      else if (!strcmp(name, "align")) opt.align = on;
      else if (!strcmp(name, "guard-pages")) opt.guard_pages = on;
      else if (!strcmp(name, "regalloc")) opt.regalloc = on;
      else if (!strcmp(name, "split")) opt.split = on;
      else if (!strcmp(name, "vectorize")) opt.vectorize = on;
      else if (!strcmp(name, "edge-tests")) opt.edgetests = on;
      else if (!strcmp(name, "selftail")) opt.selftail = on;
      else if (!strcmp(name, "ldcomb")) opt.ldcomb = on;
      else if (!strcmp(name, "escape")) opt.escape = on;
      else if (!strcmp(name, "partial-escape")) opt.pescape = on;
      else if (!strcmp(name, "gloop")) opt.gloop = on;
      else if (!strcmp(name, "loopclose")) opt.loopclose = on;
      else if (!strcmp(name, "cpool")) opt.cpool = on;
      else if (!strcmp(name, "ictab")) opt.ictab = on;
      else if (!strcmp(name, "trampoline")) opt.trampoline = on;
      else if (!strcmp(name, "go-sp")) opt.go_sp = on;
      else if (!strcmp(name, "go-resume")) opt.go_resume = on;
      else if (!strcmp(name, "go-noret")) opt.go_noret = on;
      else if (!strcmp(name, "wrap-params")) opt.wrap_params = on;
      else if (!strcmp(name, "gssa")) opt.gssa = on;
      else if (!strcmp(name, "gssa-all")) { opt.gssa_all = on; if (on) opt.gssa = true; }
      else if (!strcmp(name, "small-loop-align")) opt.small_loop_align = on;
      else if (!strcmp(name, "labcache")) opt.labcache = on;
      else if (!strcmp(name, "barelide")) opt.barelide = on;
      else if (!strcmp(name, "merge")) codegen_set_merge(on);
      else if (!strcmp(name, "compress-data")) codegen_set_compress_data(on);
      else if (!strcmp(name, "reach")) opt.reach = on;
      else if (!strcmp(name, "prune")) opt.prune = on;
      else if (!strcmp(name, "devirt")) opt.devirt = on;
      else if (!strcmp(name, "reinline")) opt.reinline = on;
      else if (!strcmp(name, "throw-jump")) opt.throwjump = on;
      else if (!strcmp(name, "unroll")) opt.unroll = on;
      else if (!strcmp(name, "ifconv")) opt.ifconv = on;
      else if (!strcmp(name, "mulwide")) opt.mulwide = on;
      else if (!strcmp(name, "fma")) opt.fma = on;
      else if (!strcmp(name, "dead-frames")) opt.dead_frames = on;
      else if (!strcmp(name, "exact-trap")) opt.exact_trap = on;
      else if (!strcmp(name, "gcpure")) opt.gcpure = on;
      else if (!strcmp(name, "closed-world")) opt.closed_world = on;
      else if (!strcmp(name, "sealed")) opt.sealed = on;
      else if (!strcmp(name, "once")) opt.once = on;
      else warmc_fatal("unknown pass %s", name);
    }
    else if (a[0] == '-') warmc_fatal("unknown option %s (try --help)", a);
    else input = inputs[ninputs++] = a;
  }
  /* -fexact-trap: these keep what a trap would see where it cannot see it,
     a global in an argument or a frame's stores left out, whatever their
     own flags and their order say */
  if (opt.exact_trap) opt.trampoline = opt.go_sp = opt.dead_frames = false;
  if (!input) { fputs(usage_text, stderr); return 2; }
  /* A library's runtime finds its module as warmc_module; the prefix names
     the library's C helpers instead. */
  if (sym_prefix && !shared) codegen_set_symbol_prefix(sym_prefix);
  if (shared && want_wasi)
    warmc_fatal("--shared and --wasi: a library's imports are bound by the "
                "program that loads it, so there is no wasi host to link");
  if (shared && g_no_export_names)
    warmc_fatal("--shared needs the export names --no-export-names drops");

  TargetFmt fmt = TGT_ELF;
  if (!strcmp(target, "macos") || !strcmp(target, "darwin")) fmt = TGT_MACHO;
  else if (!strcmp(target, "windows")) fmt = TGT_PE;
  else if (strcmp(target, "linux")) warmc_fatal("unknown target %s", target);
  if (fmt == TGT_PE) g_target = &warmc_target_x64;
  bool macho = fmt == TGT_MACHO;
  codegen_set_darwin_abi(macho);
  const char *outdef = fmt == TGT_PE ? "a.exe" : "a.out";
  const char *entry_sym = fmt == TGT_PE ? "warmc_win_entry"
                        : macho ? "_main" : "_start";

  if (ninputs > 1 && validate_only && !dump_component && !dump_cwit && !dump_canon &&
      !dump_adapter && !dump_link && !dump_wit) {
    validate_several(inputs, ninputs);
    xfree(inputs);
    return 0;
  }
  if (ninputs > 1 && emit_wasm) {
    if (output)
      warmc_fatal("--emit-wasm writes each of several modules beside it; -o names one");
    emit_several(inputs, ninputs);
    xfree(inputs);
    return 0;
  }
  if (ninputs > 1) {
    if (shared || validate_only || dump_component || dump_wit || dump_canon || dump_adapter ||
        dump_cwit || dump_link || nnative_mods || system_ld)
      warmc_fatal("several modules make one program with the built-in linker: "
                  "--shared, --validate-only, --dump-*, --native-module and --system-ld "
                  "take one");
    if (sym_prefix && !obj_only)
      warmc_fatal("--symbol-prefix with several modules: their object (-c), each module "
                  "under the prefix and its place");
    if (!obj_only && !rt) warmc_fatal("no runtime specified; pass --rt or set WARMC_RT");
    char wasipath[4096];
    const char *w = want_wasi ? wasi_archive(rt, wasi, false, wasipath, sizeof wasipath) : NULL;
    compile_several(inputs, ninputs, &opt, fmt, output ? output : obj_only ? "out.o" : outdef,
                    obj_only, keep_obj, want_wasi, w, rt, entry_sym, dead_strip,
                    sym_prefix);
    xfree(inputs);
    return 0;
  }
  xfree(inputs);

  if (dump_wit || is_directory(input) ||
      (validate_only && ends_with(input, ".wit"))) {
    WitRegistry *r = wit_load(input);
    if (dump_wit) wit_registry_dump(r, stdout);
    wit_registry_free(r);
    return 0;
  }

  size_t len;
  uint8_t *bin = read_file(input, &len);

  bool named_wat = ends_with(input, ".wat");
  bool named_wasm = ends_with(input, ".wasm");
  if (named_wat || (!named_wasm && wat_is_text(bin, len))) {
    size_t blen;
    uint8_t *assembled = wat_assemble((const char *)bin, len, input, &blen);
    xfree(bin);
    bin = assembled;
    len = blen;
    if (emit_wasm) {
      const char *out = output ? output : "out.wasm";
      FILE *f = fopen(out, "wb");
      if (!f) warmc_fatal("cannot write %s", out);
      if (len && fwrite(bin, 1, len, f) != len) warmc_fatal("short write on %s", out);
      fclose(f);
      xfree(bin);
      return 0;
    }
  } else if (emit_wasm) {
    warmc_fatal("--emit-wasm takes a text module; %s is already binary", input);
  }

  if (wasm_is_component(bin, len)) {

    if (shared)
      warmc_fatal("--shared: %s is a component; only a core module can be "
                  "built as a library so far", input);
    if (g_no_export_names)
      warmc_fatal("--no-export-names: %s is a component, and a component binds "
                  "one instance to another by export name", input);
    Component *c = comp_parse(bin, len);
    if (dump_component) comp_dump(c, stdout);
    if (dump_cwit) wit_dump_component(c, stdout);
    if (dump_canon) canon_dump(c, false, stdout);
    if (dump_adapter) canon_dump_adapters(c, false, stdout);
    if (dump_link) {
      LinkPlan *lp = clink_plan(c);
      clink_dump(lp, stdout);
      clink_free(lp);
    }
    if (!validate_only && !dump_component && !dump_cwit && !dump_canon
        && !dump_adapter && !dump_link) {

      LinkPlan *lp = clink_plan(c);
      const char *why = clink_why_not(lp);
      if (why) warmc_fatal("%s is a component warmc cannot compile yet: %s",
                           input, why);
      bool command = false;
      for (uint32_t i = 0; i < lp->nexports; i++)
        if (clink_is_command(lp->exports[i].name)) command = true;
      if (command && !want_wasi && !obj_only)
        warmc_fatal("%s exports %s, so it needs a wasi host: pass --wasi",
                    input, CLINK_COMMAND_EXPORT);
      if (want_wasi && !command)
        warmc_fatal("--wasi: %s is a component that does not export %s, so "
                    "there is nothing for the host to run", input,
                    CLINK_COMMAND_EXPORT);
      /* make builds no WASI 0.2 host for Windows */
      if (want_wasi && !wasi && fmt == TGT_PE)
        warmc_fatal("--wasi: there is no WASI 0.2 host for Windows");
      if (!obj_only && !rt) warmc_fatal("no runtime specified; pass --rt or set WARMC_RT");
      ObjBuilder co;
      memset(&co, 0, sizeof co);
      clink_compile(lp, &co, &opt);
      char cobj[4096];
      if (obj_only) snprintf(cobj, sizeof cobj, "%s", output ? output : "out.o");
      else snprintf(cobj, sizeof cobj, "%s.o", output ? output : outdef);
      if (macho) write_macho64(&co, cobj);
      else if (fmt == TGT_PE) write_coff64(&co, cobj);
      else write_elf64(&co, cobj);
      if (!obj_only) {
        if (!keep_obj) warmc_temp_output(cobj);
        char cwasi[4096];
        warmc_link_exec(fmt, output ? output : outdef, cobj,
                        want_wasi ? wasi_archive(rt, wasi, true, cwasi,
                                                 sizeof cwasi) : NULL,
                        rt, entry_sym, dead_strip);
        if (!keep_obj) { remove(cobj); warmc_temp_output(NULL); }
      }
      clink_free(lp);
      comp_free(c);
      xfree(bin);
      return 0;
    }
    comp_free(c);
    xfree(bin);
    return 0;
  }

  bool wtm = warmc_getenv("WARMC_TIME") != NULL;
  struct timespec _ts; double _t0 = 0, _t1 = 0, _t2 = 0, _t3 = 0;
#define STAMP(v) do { if (wtm) { clock_gettime(CLOCK_MONOTONIC, &_ts); \
    (v) = _ts.tv_sec * 1e3 + _ts.tv_nsec / 1e6; } } while (0)
  STAMP(_t0);
  Module *m = wasm_parse(bin, len);
  STAMP(_t1);
  wasm_validate(m);
  uint32_t nnative = mark_native_imports(m, native_mods, nnative_mods);
  if (nnative && fmt == TGT_PE)
    warmc_fatal("--native-module: native imports are AArch64's alone, for linux and macos");
  if (nnative && shared)
    warmc_fatal("--native-module: a --shared library's imports are bound by the program "
                "that loads it");
  /* the built-in linker resolves nothing outside warmc's own runtime */
  if (nnative) system_ld = true;
  if (*link_extra && !system_ld && !obj_only)
    warmc_fatal("--link: for the system linker (--system-ld)");
  STAMP(_t2);
  if (wtm) fprintf(stderr, "front: parse %.1f validate %.1f (peak %.0f MB)\n",
                   _t1 - _t0, _t2 - _t1, peak_rss_mb());
  if (validate_only) { module_free(m); xfree(bin); return 0; }

  if (!obj_only && !want_wasi && !shared)
    for (uint32_t i = 0; i < m->nimports; i++) {
      const Import *im = &m->imports[i];
      if (im->module_len == 22 &&
          !memcmp(im->module, "wasi_snapshot_preview1", 22))
        warmc_fatal("%s imports wasi_snapshot_preview1.%.*s, and nothing "
                    "linked into the program provides it; pass --wasi to link "
                    "the host that does, or -c to link a host of your own",
                    input, (int)im->name_len, im->name);
    }

  char libout[4096], prefix[256];
  Shlib *lib = NULL;
  if (shared) {
    if (!output) {
      const char *b = strrchr(input, '/');
      b = b ? b + 1 : input;
      size_t n = strcspn(b, ".");
      snprintf(libout, sizeof libout, "%s%.*s%s", fmt == TGT_PE ? "" : "lib",
               (int)n, b, macho ? ".dylib" : fmt == TGT_PE ? ".dll" : ".so");
      output = libout;
    }
    shared_prefix(sym_prefix, output, prefix, sizeof prefix);
    lib = shlib_plan(m, prefix, input);
    if (fmt == TGT_PE) g_stack_probes = true;
  }

  ObjBuilder o;
  memset(&o, 0, sizeof o);
  opt.sole_instance = shared || !obj_only;
  compile_module(m, &o, &opt);
  if (lib) shlib_emit(lib, &o);
  STAMP(_t3);
  if (wtm) fprintf(stderr, "front: compile_module total %.1f (peak %.0f MB)\n",
                   _t3 - _t2, peak_rss_mb());
  if (opt.stats) print_stats();

  char objpath[4096];
  if (obj_only) snprintf(objpath, sizeof objpath, "%s", output ? output : "out.o");
  else snprintf(objpath, sizeof objpath, "%s.o", output ? output : outdef);

  if (macho) write_macho64(&o, objpath);
  else if (fmt == TGT_PE) write_coff64(&o, objpath);
  else write_elf64(&o, objpath);
  if (!obj_only && !keep_obj) warmc_temp_output(objpath);

  if (lib) {
    char hdr[4096];
    if (!header) {
      const char *slash = strrchr(output, '/');
      snprintf(hdr, sizeof hdr, "%.*s%s.h",
               slash ? (int)(slash - output + 1) : 0, output, prefix);
      header = hdr;
    }
    shlib_write_header(lib, header, output);
    if (fmt == TGT_PE) {
      char def[4096];
      const char *slash = strrchr(output, '/');
      snprintf(def, sizeof def, "%.*s%s.def",
               slash ? (int)(slash - output + 1) : 0, output, prefix);
      shlib_write_def(lib, def, output);
    }
    if (!obj_only) {
      if (!rt) warmc_fatal("no runtime specified; pass --rt or set WARMC_RT");
      if (system_ld)
        warmc_fatal("--shared links with the built-in linker only");
      uint32_t nsyms;
      char **syms = shlib_symbols(lib, &nsyms);
      warmc_link_shared(fmt, output, objpath, rt, syms, nsyms, dead_strip);
      xfree(syms);
      if (!keep_obj) { remove(objpath); warmc_temp_output(NULL); }
    }
    shlib_free(lib);
    module_free(m);
    xfree(bin);
    return 0;
  }

  if (obj_only) return 0;

  if (!rt) warmc_fatal("no runtime specified; pass --rt or set WARMC_RT");
  /* The external linker has to speak the output's format: ld.lld is ELF
     only, and handed the Mach-O arguments below it refuses every one. */
  if (!ld) {
#ifdef __APPLE__
    ld = macho ? "ld" : "ld.lld";
#else
    ld = macho ? "ld64.lld" : "ld.lld";
#endif
  }

  char wasipath[4096];
  if (want_wasi) wasi = wasi_archive(rt, wasi, false, wasipath, sizeof wasipath);

  if (!system_ld) {

    warmc_link_exec(fmt, output ? output : outdef, objpath,
                    want_wasi ? wasi : NULL, rt, entry_sym,
                    dead_strip);
    if (!keep_obj) { remove(objpath); warmc_temp_output(NULL); }
    module_free(m);
    xfree(bin);
    return 0;
  }

  char cmd[20000];
  if (fmt == TGT_PE)
    warmc_fatal("--system-ld --target windows: not wired up; the built-in "
                "linker handles the PE link (an SDK-import-lib escape hatch "
                "can come later)");
  if (macho) {
    const char *libdir = macos_libdir();
    if (!libdir)
      warmc_fatal("no macOS SDK found for -lSystem; install the command line "
                  "tools (xcode-select --install) or set WARMC_MACOS_LIBDIR "
                  "to the directory holding libSystem.tbd");
    /* the SDK's frameworks, which a --link -framework names */
    char fw[4200] = "";
    snprintf(fw, sizeof fw, "%s/../../System/Library/Frameworks", libdir);
    if (!is_directory(fw)) fw[0] = 0;
    snprintf(cmd, sizeof cmd,
             "%s -arch arm64 -platform_version macos 11.0 11.0 -o %s %s %s%s%s "
             "-lSystem -L%s%s%s%s",
             ld, output ? output : outdef, objpath,
             want_wasi ? wasi : "", want_wasi ? " " : "", rt, libdir,
             *fw ? " -F" : "", fw, link_extra);
  } else {
    snprintf(cmd, sizeof cmd, "%s -static -e _start -o %s %s %s%s%s%s",
             ld, output ? output : outdef, objpath,
             want_wasi ? wasi : "", want_wasi ? " " : "", rt, link_extra);
  }
  if (verbose) fprintf(stderr, "warmc: %s\n", cmd);
  int rc = system(cmd);
  if (rc != 0) warmc_fatal("link failed (%d)", rc);
  if (!keep_obj) { remove(objpath); warmc_temp_output(NULL); }

  module_free(m);
  xfree(bin);
  return 0;
}
