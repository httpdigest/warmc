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
#endif

static const char *usage_text =
  "warmc -- WebAssembly to native ahead-of-time compiler: AArch64 for Linux\n"
  "         and macOS, x86-64 for Windows\n"
  "\n"
  "usage: warmc [options] input.wasm|input.wat\n"
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
  "                gcpure, split, labcache, ifconv, mulwide, barelide,\n"
  "                small-loop-align, trampoline, go-sp, fma, dead-frames,\n"
  "                exact-trap\n"
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
  "  --validate-only  validate and stop; exit 2 malformed, 3 invalid\n"
  "  --dump-component  print a component's structure, one line per definition\n"
  "  --dump-canon      print what the canonical ABI makes of each of its types\n"
  "  --dump-component-wit  render a component's interfaces back as WIT text\n"
  "  --dump-adapter    print the canonical ABI conversion code, as wasm text\n"
  "  --dump-link       print a component's instance graph, resolved\n"
  "  --dump-wit    parse a .wit document and print what it declares\n"
  "  --emit-wasm   assemble a text module to binary and stop, writing -o\n"
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
  "                the guest a listening socket to accept on; without it the\n"
  "                four sock_* calls have no descriptor to work on and the\n"
  "                guest reaches no network at all\n"
  "  --symbol-prefix=P  name the module descriptor P_module, so several\n"
  "                modules can be linked into one program\n"
  "  --custom-sections=default|all|none  which of the module's custom\n"
  "                sections the output carries, in a read-only section of its\n"
  "                own (.wasm.custom, __TEXT,__wasm_custom, .wcustom) that a\n"
  "                --shared library also hands out by name:\n"
  "                PREFIX_custom_section.  default leaves out the debug\n"
  "                information: the name section, which warmc does not\n"
  "                read, and DWARF's .debug_* sections\n"
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
  "  --no-dead-strip  keep every section in the link, referenced or not\n"
  "  --keep-obj    do not delete the intermediate object\n"
  "  -v            print the link command\n"
  "  --magic=D[:64]  print the multiply, add and shift that divide by D\n"
  "                (unsigned, 32-bit or :64), and stop; test/magic.py's hook\n"
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

int main(int argc, char **argv) {
#ifdef _WIN32
  /* Dumps are byte-compared against golden files; keep the streams free
     of CRLF translation so output matches the POSIX hosts exactly. */
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  const char *input = NULL, *output = NULL, *target = "linux";
  const char *rt = warmc_getenv("WARMC_RT");
  const char *ld = warmc_getenv("WARMC_LD");
  const char *wasi = warmc_getenv("WARMC_WASI");
  bool want_wasi = false;
  bool obj_only = false, keep_obj = false, verbose = false, validate_only = false;
  bool dead_strip = true;
  bool shared = false;
  const char *header = NULL, *sym_prefix = NULL;

  bool system_ld = warmc_getenv("WARMC_SYSTEM_LD") != NULL;
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
    else if (!strcmp(a, "--no-export-names")) g_no_export_names = true;
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

    else if (!strncmp(a, "--magic=", 8)) {
      const char *arg = a + 8;
      bool wide = false;
      char *end = NULL;
      unsigned long long d = strtoull(arg, &end, 0);
      if (end && *end == ':') wide = !strcmp(end + 1, "64");
      Magic mg = opt_magic_unsigned((uint64_t)d, wide);
      printf("%llu %d %d\n", (unsigned long long)mg.m, mg.add, mg.sh);
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
      else if (!strcmp(name, "gssa")) opt.gssa = on;
      else if (!strcmp(name, "gssa-all")) { opt.gssa_all = on; if (on) opt.gssa = true; }
      else if (!strcmp(name, "small-loop-align")) opt.small_loop_align = on;
      else if (!strcmp(name, "labcache")) opt.labcache = on;
      else if (!strcmp(name, "barelide")) opt.barelide = on;
      else if (!strcmp(name, "merge")) codegen_set_merge(on);
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
      else warmc_fatal("unknown pass %s", name);
    }
    else if (a[0] == '-') warmc_fatal("unknown option %s (try --help)", a);
    else input = a;
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
  const char *outdef = fmt == TGT_PE ? "a.exe" : "a.out";
  const char *entry_sym = fmt == TGT_PE ? "warmc_win_entry"
                        : macho ? "_main" : "_start";

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
  compile_module(m, &o, &opt);
  if (lib) shlib_emit(lib, &o);
  STAMP(_t3);
  if (wtm) fprintf(stderr, "front: compile_module total %.1f (peak %.0f MB)\n",
                   _t3 - _t2, peak_rss_mb());
  if (opt.stats) {
    fprintf(stderr,
            "warmc stats: folded=%llu dce=%llu cse=%llu licm=%llu branch=%llu\n"
            "             loads_fwd=%llu dead_stores=%llu checks_removed=%llu params=%llu\n"
            "             strength=%llu inlined=%llu rotated=%llu vectorised=%llu\n"
            "             self_tail=%llu ldcomb=%llu spills=%llu insts=%llu\n"
            "             ldpairs=%llu stpairs=%llu cold_frames=%llu early_rets=%llu"
            " exact_store_checks=%llu exact_memop_checks=%llu\n"
            "             reloads=%llu caller_saves=%llu csa_restores=%llu"
            " v128_spills=%llu rb_trips=%llu split_cuts=%llu split_invs=%llu pieces=%llu"
            " loop_pieces=%llu recomputes=%llu dead_frame_stores=%llu tramp_funcs=%llu gosp_funcs=%llu\n"
            "             escape_removed=%llu escape_forwarded=%llu escape_made=%llu gloop=%llu\n"
            "             loop_closed=%llu reassoc=%llu cmp_reused=%llu csel_fused=%llu mul_wide=%llu fma=%llu gssa=%llu\n"
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
            " unr_rej_escape=%llu unr_addr_folded=%llu gc_nullchk=%llu\n",
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
            (unsigned long long)g_stats.gc_nullchk_removed);
  }

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

  char cmd[8192];
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
    snprintf(cmd, sizeof cmd,
             "%s -arch arm64 -platform_version macos 11.0 11.0 -o %s %s %s%s%s "
             "-lSystem -L%s",
             ld, output ? output : outdef, objpath,
             want_wasi ? wasi : "", want_wasi ? " " : "", rt, libdir);
  } else {
    snprintf(cmd, sizeof cmd, "%s -static -e _start -o %s %s %s%s%s",
             ld, output ? output : outdef, objpath,
             want_wasi ? wasi : "", want_wasi ? " " : "", rt);
  }
  if (verbose) fprintf(stderr, "warmc: %s\n", cmd);
  int rc = system(cmd);
  if (rc != 0) warmc_fatal("link failed (%d)", rc);
  if (!keep_obj) { remove(objpath); warmc_temp_output(NULL); }

  module_free(m);
  xfree(bin);
  return 0;
}
