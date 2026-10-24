# warmc

**warmc** is an ahead-of-time compiler that turns WebAssembly into standalone
native executables, or into shared libraries that other languages call. It
needs no JIT, interpreter or VM process at run time.

You give it a `.wasm` or `.wat` file and it produces a Linux, macOS or Windows
binary. That binary is linked against a small freestanding runtime, which
provides traps, a concurrent garbage collector for Wasm GC, exception
handling, and sandboxed WASI preview 1 and 0.2 hosts.

warmc is a single C11 program with no LLVM or other compiler-library
dependency. Everything between the WebAssembly bytes and the finished
executable is written from scratch: the text-format assembler, the validator,
an SSA optimiser, instruction selectors and register allocators for AArch64
and x86-64, ELF, Mach-O and COFF object writers, and a linker.

```console
$ build/warmc --target macos --rt build/warmrt-macos.a --wasi -o hello hello.wasm
$ ./hello
Hello world!
```

---

## Features

- **Native output for three platforms**

  | `--target` | Output | ISA |
  |---|---|---|
  | `linux` (default) | static ELF executable, no libc | AArch64 |
  | `macos` | Mach-O PIE, ad-hoc code-signed, linked to libSystem | arm64 |
  | `windows` | PE32+ console executable | x86-64-v3 (AVX2, BMI2, FMA) |

  Cross-compiling works out of the box: any host can produce any of the
  three, using the runtime archives that `make` builds for every target.
  With `--shared`, each target gets a shared library instead: a `.so`, a
  `.dylib` or a `.dll`.

- **Self-contained toolchain.** warmc links executables and shared libraries
  itself. It reads `.a` archives and ELF, Mach-O and COFF objects,
  dead-strips unused sections, and resolves Windows DLL imports from a
  built-in table. It needs no `ld`, SDK import libraries or `lld` for normal
  use.
- **WebAssembly 3.0.** This covers the 1.0 and 2.0 core plus multi-value,
  bulk memory, reference types, typed function references, **GC** (structs,
  arrays, i31, casts), **SIMD** and **relaxed SIMD**, **tail calls**,
  **exception handling** (`try_table` / `exnref`), **memory64** and table64,
  **multi-memory**, and extended constant expressions. See [WebAssembly
  feature support](#webassembly-feature-support).
- **Passes the official spec testsuite.** The pinned upstream testsuite has
  257 `.wast` files. warmc records zero failures across 5,664 validation and
  60,695 execution assertions, at `-O1`, at `-O2`, and at
  `-O2 --profile=deterministic`.
- **Optimising compiler.** It optimises over SSA with block parameters. Passes
  include:
  - inlining and devirtualisation, repeated as each exposes more: a call on
    an object whose class is known resolves through its constant vtable, and
    a throw inlined beside its catch becomes a jump
  - dominator-based CSE, LICM, loop rotation and unrolling
  - an auto-vectoriser, with runtime alias checks
  - bounds-check elimination and guard-page memory
  - escape analysis for GC allocations, partial too: an object that escapes
    on some paths is allocated only on those, and one merged with null
    becomes its fields and a flag
  - store-to-load forwarding
  - magic-number division, and 128-bit multiplies written in 32-bit halves
    turned back into `mul` and `umulh`/`smulh`
  - a linear-scan register allocator that runs under several spill rules and
    keeps the cheapest, with rematerialisation, a second-chance pass and
    live-range splitting at loop boundaries

  Functions compile in parallel and are laid out in a deterministic order.
- **Fast code.** Across 335 benchmark kernels compiled from Go, TinyGo, Rust,
  Zig, AssemblyScript, Kotlin, C, C++ and Haskell, warmc's steady-state code
  takes 0.45× wasmtime's time (geometric mean), with much lower peak memory.
  Against the same programs compiled natively at `-O2` it takes 1.03× the
  time. See [Performance](#performance).
- **Runtime for modern languages:**
  - a concurrent, compacting, ZGC-style collector with coloured pointers,
    precise stack maps and load barriers
  - table-driven exception unwinding through its own unwind tables
  - guard-page trapping and stack-overflow detection
- **WASI preview 1.** All 46 `wasi_snapshot_preview1` functions are bound. The
  sandbox is capability-based: preopened directories, no `..` escapes,
  symlinks resolved by hand, and an opt-in environment and TCP listener.
- **WASI 0.2 and the Component Model.** warmc runs `wasi:cli` command
  components, such as TinyGo or Rust `wasm32-wasip2` output, against built-in
  `wasi:cli`, `wasi:clocks`, `wasi:filesystem`, `wasi:io` and `wasi:random`
  hosts. It also links multi-module components together, generating
  canonical-ABI adapters between them.
- **Inspection tools.** You can dump a component's structure, its
  canonical-ABI layout, the generated adapter code, the resolved instance
  graph, the WIT it implies, or any WIT package.
- **Embeddable.** Compile with `-c` to get a relocatable object that exposes
  one module descriptor. A small C API instantiates modules, wires them
  together, binds host functions and calls exports.
- **Shared libraries.** With `--shared`, each export becomes a C function,
  declared in a generated header. C, C++, Python's `ctypes`, Java's foreign
  function API or any other C FFI can call it.

---

## Building

### Prerequisites

| Tool | Why |
|---|---|
| **clang** with the AArch64 and X86 backends (a recent LLVM; Homebrew's works) | builds `warmc` and cross-compiles the three runtimes |
| **llvm-ar** | archives the runtimes (Linux archives fall back to `ar`) |
| **lld** (Linux) | links the ThinLTO build, whose objects GNU ld cannot read |
| GNU **make** | the build |

Supported build hosts:
- macOS on Apple silicon
- Linux on AArch64
- Windows x86-64 under an MSYS2 MINGW64, CLANG64 or MSYS shell

On other hosts, such as Linux x86-64, warmc builds and cross-compiles fine.
The test suite then runs AArch64 output under `qemu-aarch64`.

### Build

```sh
make -j
```

This builds everything into `build/`:

| Artefact | Purpose |
|---|---|
| `build/warmc` | the compiler |
| `build/warmrt.a`, `warmrt-macos.a`, `warmrt-windows.a` | the runtime for the Linux, macOS and Windows targets |
| `build/warmwasi.a`, `warmwasi-macos.a`, `warmwasi-windows.a` | WASI preview 1 hosts |
| `build/warmwasip2.a`, `warmwasip2-macos.a` | WASI 0.2 hosts (none for Windows yet) |
| `build/warmgcth*.a` | the runtime plus GC thread self-tests, used by the test suite |

`make clean` removes `build/`. `make asan` and `make tsan` build sanitiser
versions of the compiler, `build/warmc-asan` and `build/warmc-tsan`.

---

## Quick start

```sh
make -j

# Linking needs a runtime archive: pass --rt, or set WARMC_RT once.
export WARMC_RT=$PWD/build/warmrt-macos.a      # on macOS
# export WARMC_RT=$PWD/build/warmrt.a          # on Linux

# A WASI program (here, a hand-written one from the test suite)
build/warmc --target macos --wasi -o hello test/wasi/hello.wat
./hello
```

> **Note:** the default target is `linux`, even on a macOS host. Pass
> `--target macos` to get a binary you can run on the Mac.

The `example/` directory has two real-world inputs. Each comes with its
pre-built `.wasm` and a macOS binary.

```sh
# TinyGo: prints, then writes ./helloworld.txt, so it needs a preopened directory
build/warmc --target macos --wasi -o tinygo-hello example/tinygo/main.wasm
./tinygo-hello --dir=.

# Kotlin/Wasm (uses Wasm GC)
build/warmc --target macos --wasi -o kt-hello example/kotlin/out/hello.wasm
./kt-hello                # points = 5, sum = 70
```

---

## Usage

```
warmc [options] input.wasm|input.wat
```

A file named `*.wat` is read as text and one named `*.wasm` as binary. For
any other name, warmc decides from the contents: binary if they begin with
the `\0asm` magic number, text otherwise.

### Choosing a target

| Target | Flags | Default output |
|---|---|---|
| Linux / AArch64 | `--rt build/warmrt.a` | `a.out` |
| macOS / arm64 | `--target macos --rt build/warmrt-macos.a` | `a.out` |
| Windows / x86-64 | `--target windows --rt build/warmrt-windows.a` | `a.exe` |

`-o FILE` names the output. `-c` stops after writing a relocatable object
(`out.o` unless you pass `-o`). `--shared` builds a shared library, by default
`libNAME.so`, `libNAME.dylib` or `NAME.dll` for an input named `NAME.wasm` or
`NAME.wat` (see [Shared libraries](#shared-libraries)).

When you pass `--wasi`, warmc finds the matching host archive next to the
runtime by name: `warmrt-macos.a` becomes `warmwasi-macos.a` for a core module
and `warmwasip2-macos.a` for a component. Use `--wasi=PATH` to choose a
different host archive.

### Running exports directly: the CLI harness

If you compile a module **without** `--wasi`, the executable becomes a small
harness that calls any export by name:

```wat
;; fib.wat
(module
  (func $fib (export "fib") (param $n i32) (result i64)
    (if (result i64) (i32.lt_u (local.get $n) (i32.const 2))
      (then (i64.extend_i32_u (local.get $n)))
      (else (i64.add (call $fib (i32.sub (local.get $n) (i32.const 1)))
                     (call $fib (i32.sub (local.get $n) (i32.const 2)))))))
  (func (export "div") (param i32 i32) (result i32)
    (i32.div_s (local.get 0) (local.get 1))))
```

```console
$ build/warmc --target macos --rt build/warmrt-macos.a -o fib fib.wat
$ ./fib                       # no _start or main export, so it lists the exports
exports:
  fib (i32) -> i64
  div (i32, i32) -> i32
$ ./fib fib 40
i64:102334155
$ ./fib div 1 0
trap: integer divide by zero
$ echo $?
1
```

The harness accepts:

```
PROG [--catch] [--bench N] EXPORT ARGS... [-- EXPORT ARGS...]...
```

- With **no arguments**, the harness runs `_start`, or `main` if there is no
  `_start`. If the module has neither, it lists the exports.
- **Integer arguments** are decimal or `0x` hex, with an optional sign.
  **Float arguments are raw IEEE-754 bit patterns**. For example, 3.0 as an
  `f64` is `0x4008000000000000`.
- **Results** print as `type:value`. Integers are decimal. Floats and `v128`
  values print as hex bit patterns (`f64:0x4014000000000000`). Multiple results
  share one line.
- You can chain several calls with `--`. `--catch` reports a trap and carries
  on with the next call.
- `--bench N` times the export. It warms up first (at least 3 calls and
  100 ms), then takes up to N samples, at most 512, each a batch of calls
  lasting about 3 ms. It stops early once it has 5 samples and has spent
  40 ms. It prints `bench: NAME n=… min_ns=… med_ns=… max_ns=… mean_ns=…`,
  per call, to stderr. The result still goes to stdout.
- **Exit codes:**
  - `0` success
  - `1` trap, printed to stderr as `trap: <spec message>`
  - `2` unknown export, too few arguments (extra ones are ignored), a `v128`
    parameter, or more than 7 integer or 8 float parameters
  - `4` unresolved import (the harness provides no imports)

`--no-export-names` removes the export-name table from the binary, except for
`_start` and `main`. The harness still runs those with no arguments, but it
cannot call an export by name or list the exports.

### WASI preview 1 programs

Most toolchains that target `wasm32-wasi` / `wasip1` produce modules that
warmc compiles directly:

| Language | Producing the module |
|---|---|
| TinyGo | `tinygo build -target=wasip1 -o app.wasm .` |
| Go | `GOOS=wasip1 GOARCH=wasm go build -o app.wasm .` |
| Rust | `rustc --target wasm32-wasip1 -C opt-level=3 -o app.wasm main.rs`, or `cargo build --target wasm32-wasip1` |
| Zig | `zig build-exe -target wasm32-wasi -O ReleaseFast main.zig` |
| AssemblyScript | `asc main.ts -O3 --exportStart _start --use abort= -o app.wasm` (with a WASI shim; see `test/asbench/wasi.ts`) |
| Kotlin/Wasm | `kotlinc-wasm -Xwasm-target=wasm-wasi …` (two steps, klib then module; see `build_kotlin` in `test/gobench.py`) |
| C | `clang -O3 main.c -o app.wasm` with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk)'s clang (`make wasi-sdk` fetches the pinned release); add `-mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false -lsetjmp` for `setjmp`/`longjmp` |
| C++ | `clang++ -O3 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false main.cpp -lunwind -o app.wasm` (wasi-sdk), or without the exception flags for code that never throws |
| Haskell | `wasm32-wasi-ghc -O2 Main.hs -o app.wasm`, with GHC's wasm backend from [ghc-wasm-meta](https://gitlab.haskell.org/haskell-wasm/ghc-wasm-meta) |

Then compile it:

```sh
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
```

The resulting program reads its own sandbox flags anywhere before a `--`. Every
other argument, an unknown flag included, becomes the guest's `argv`:

| Flag | Effect |
|---|---|
| `--dir=HOST[::GUEST]` | preopen a host directory, optionally under a different guest path |
| `--env NAME=VALUE` | pass one environment variable |
| `--inherit-env` | pass the whole host environment (nothing is inherited by default) |
| `--tcplisten=[ADDR:]PORT` | hand the guest a listening socket (default address 127.0.0.1). Without it, the guest has no network access. |

```sh
./app --dir=./data::/data --env MODE=fast input.txt
```

Sandbox behaviour:
- Paths are resolved one component at a time.
- Absolute paths, and `..` that climbs above a preopen, are refused.
- Symlinks are followed by hand: at most 8, and never to an absolute target.
- Rights are enforced per descriptor.

The exit status is the guest's `proc_exit` code. The host reserves a few codes
of its own:

| Code | Meaning |
|---|---|
| 70 | the host's own import stub failed to instantiate |
| 71 | link failure |
| 72 | no `_start` |
| 73 | trap, printed to stderr |
| 74 | a `--dir` that cannot be opened, a `--tcplisten` that cannot listen, or no memory for the flags |

On Linux a WASI program is a static executable with no libc, like every
other Linux output: `rt/linuxposix.c` answers the WASI hosts' POSIX calls
over raw system calls. It does so under names of its own (`warmc_lx_*`), so
an embedder can still link the `-c` object into a C program that uses glibc.

### WASI 0.2 command components

If the input is a component that exports `wasi:cli/run@0.2.x#run`, `--wasi`
links the WASI 0.2 host:

```sh
tinygo build -target=wasip2 -o app.wasm main.go     # or: cargo build --target wasm32-wasip2
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
./app --dir=. --env FOO=bar arg1 arg2
```

The host implements `wasi:cli` (environment, exit, stdio, terminal),
`wasi:clocks`, `wasi:filesystem` (types and preopens), `wasi:io` (error, poll
and streams) and `wasi:random`. It accepts any `@0.2.x` interface version.

The exit status is `0` or `1`, from `run`'s result, unless the component
calls `wasi:cli/exit`'s `exit-with-code` with another (Rust's
`std::process::exit` does). As for preview 1, the host exits with 72 when
there is no `run` export, 73 on a trap, and 74 for a `--dir` that cannot be
opened or no memory for the flags. If the component calls a
`wasi:*` function this host does not implement, the program traps when the
call happens and names the function. It is not refused at link time.

### Embedding compiled modules in C

For a core module, `-c` produces an object that exports a single symbol, the
module descriptor `warmc_module`. With `--symbol-prefix=P`, the descriptor is
named `P_module`, so several modules can live in one program. A component's
object instead exports a descriptor for each of its core modules plus
`warmc_component`, `warmc_modules`, `warmc_module_names` and
`warmc_nmodules`; `--symbol-prefix` does not apply to it.

The runtime owns the process entry point. To take control, replace the CLI
harness by defining `warmc_driver_hook`.

```c
/* host.c */
#include "warm_rt.h"

extern const ModuleDesc guest_module;    /* warmc -c --symbol-prefix=guest */
const ModuleDesc warmc_module = { 0 };   /* the default harness still refers to this */

/* A host import: it receives its binding's inst (NULL below; warmc_bind_import
   can pass the guest instead), then the wasm arguments as u64. */
static u64 print_i64(WarmInstance *self, u64 v) { /* ... */ return 0; }

static int drive(int argc, char **argv) {
  /* One binding per import, in import order. */
  WarmBinding binds[] = { { 0, "print_i64", (void *)print_i64 } };
  WarmInstance *g = warmc_instantiate_bound(&guest_module, binds);

  const ExportDesc *e = warmc_find_export(g, "fib");
  FuncRef fr = warmc_export_func(g, e);   /* call through fr.instance, not g */
  u64 iargs[8] = { 30 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 ret[WARMC_RETBUF_WORDS] = { 0 };
  u32 trap = warmc_invoke_catching(fr.code, (WarmInstance *)fr.instance,
                                   iargs, fargs, ret);
  if (trap) { /* warmc_trap_name(trap) */ }
  /* the i64 result is in ret[WARMC_RET_X0] */
  return 0;
}
int (*warmc_driver_hook)(int, char **) = drive;
```

Build it on macOS like this. Host code is compiled freestanding, like the
runtime, and uses `warmc_rt_write` and friends for I/O.

```sh
build/warmc --target macos -c --symbol-prefix=guest -o guest.o guest.wat
clang --target=arm64-apple-macos11 -DWARMC_DARWIN=1 -O2 -ffreestanding \
      -fno-stack-protector -fno-builtin -nostdlib -I rt -c -o host.o host.c
ld -arch arm64 -platform_version macos 11.0 11.0 -o app host.o guest.o \
   build/warmrt-macos.a -lSystem -L"$(xcrun --show-sdk-path)/usr/lib"
```

On Linux, compile the host with `--target=aarch64-linux-gnu` and link with
`ld.lld -static -e _start -o app host.o guest.o build/warmrt.a`. The Windows
recipe is in `test/hostbuild.py`.

The rest of the API, in [`rt/warm_rt.h`](rt/warm_rt.h):

| Call | Purpose |
|---|---|
| `warmc_instantiate(md)`, `warmc_instantiate_catching(md, &err)` | instantiate a module and run its start function; instead of exiting, the `_catching` form returns NULL and stores the trap code in `err` |
| `warmc_register(name, inst)` | make an instance's exports importable under module `name`, so modules can import one another. Shared memories, tables, globals and tags all work. |
| `warmc_bind_import(inst, module, name, fn, self)` | rebind an already-linked function import to a host function, which then receives `self` first (`test/tinygo/driver.c` passes the guest, to reach its memory) |
| `warmc_invoke(...)`, `warmc_invoke_catching(...)` | call an export. Integer and reference args go in `iargs`: up to 7 on arm64 and 3 on x64. Floats go in `fargs[2*i]`: up to 8 on arm64 and 4 on x64. Results come back in `ret[WARMC_RET_X0]` (integer), `ret[WARMC_RET_V0]` (float) or `ret[WARMC_RET_MULTI(i)]` (multi-value). |
| `warmc_invoke_s`, `warmc_invoke_catching_s` | x64 only: for functions with more than 3 integer or 4 float parameters, which take the rest from an image of their stack slots, 8 bytes per scalar and 16 per `v128`. On arm64 a function with more than 7 integer or 8 float parameters cannot be called from C. |
| `warmc_global_cell(inst, idx)`, `inst->mem_base` / `mem_size` | access globals and linear memory 0 |
| `warmc_gc_collect()`, `warmc_gc_hold()` / `warmc_gc_release()` | drive the collector; register C-side roots |

Limits: at most 4096 instances per process, and instances are never freed.
At `-O2` each module's first memory, and each further one it defines, takes
one of 1024 guarded address-space reservations, so at most 1024 such modules
can be instantiated.

`test/link/driver.c` (multi-module linking) and `test/tinygo/driver.c`
(hand-written WASI imports) are complete examples.

### Shared libraries

`--shared` builds a library that programs in other languages load and call
through their C foreign function interface: a `.dylib` for macOS, a `.so` for
Linux and a `.dll` for Windows. Each exported function whose parameters and
results are `i32`, `i64`, `f32` or `f64` becomes a C function of the export's
name, and warmc writes a header that declares them.

```wat
;; calc.wat
(module
  (import "env" "scale" (func $scale (param i64) (result i64)))
  (memory (export "memory") 1)
  (func (export "add") (param i32 i32) (result i32)
    (i32.add (local.get 0) (local.get 1)))
  (func (export "divmod") (param i32 i32) (result i32 i32)
    (i32.div_s (local.get 0) (local.get 1))
    (i32.rem_s (local.get 0) (local.get 1)))
  (func (export "scaled") (param i64) (result i64)
    (call $scale (local.get 0))))
```

```console
$ build/warmc --target macos --rt build/warmrt-macos.a --shared -o libcalc.dylib calc.wat
$ ls
calc.h  calc.wat  libcalc.dylib
```

`calc.h` declares the exports, a type for each import, and four functions of
the library's own, named after it:

```c
int32_t add(int32_t p0, int32_t p1);
void divmod(int32_t p0, int32_t p1, int32_t *r0, int32_t *r1);
int64_t scaled(int64_t p0);

typedef int64_t (*calc_import_env_scale)(void *ctx, int64_t p0);

uint32_t calc_last_trap(void);
const char *calc_trap_name(uint32_t trap);
uint8_t *calc_memory(size_t *size);
int calc_bind_import(const char *module, const char *name, void *fn, void *ctx);
```

```c
/* main.c */
#include <stdio.h>
#include "calc.h"

static int64_t scale(void *ctx, int64_t v) { return v * *(int *)ctx; }

int main(void) {
  static int factor = 10;
  calc_bind_import("env", "scale", (void *)scale, &factor);  /* before the first call */

  int32_t q, r;
  divmod(17, 5, &q, &r);
  printf("%d %d %d %lld\n", add(2, 3), q, r, (long long)scaled(4));

  divmod(1, 0, &q, &r);                     /* traps, and returns */
  printf("%s\n", calc_trap_name(calc_last_trap()));
}
```

```console
$ cc -o main main.c -L. -lcalc -Wl,-rpath,@executable_path
$ ./main
5 3 2 40
integer divide by zero
```

How a library behaves:
- **The instance** is made by the first call, with the imports bound by then.
  Bind every import before that call with
  `calc_bind_import(module, name, fn, ctx)`; the host function receives `ctx`
  first, then the wasm arguments. If an import is still unbound, the first
  call names it on stderr, and every call then fails with trap 100 (`link
  failure`). `calc_bind_import` returns 0, 1 for an import the module does not
  have, or 2 once the first call has been made.
- **Threads.** Any thread may call; the calls take turns on the one instance.
  An import may call back into the library.
- **Traps** do not unwind into the caller. The call returns 0, results
  returned through pointers are left as they were, and `calc_last_trap()`
  gives the calling thread's trap code (0 after a call that returned).
  `calc_trap_name` names it. A wasm exception that leaves the export is trap
  13, `uncaught exception`. The instance stays usable.
- **Several results** come back through pointers after the parameters.
- **Memory.** `calc_memory(&size)` returns linear memory 0 and its size. A call
  that grows the memory may move it.
- **Stack.** Recursion is limited by the calling thread's own stack. Running
  out traps as `call stack exhausted`.
- **Faults.** The first call installs the runtime's fault handlers (SIGSEGV
  and SIGBUS, or a vectored exception handler on Windows). Faults outside
  wasm code go on to the handler that was there before, so a JVM's handler, or
  the program's own, keeps working.
- **Options.** The first call reads the runtime's `WARMC_GC_*` variables from
  the environment, as a program does when it starts.

Names:
- `--symbol-prefix=P` sets the prefix of the library's own functions.
  Without it, the prefix is the output's name without a leading `lib` and
  without its extension. `--header PATH` writes the header somewhere other
  than `PREFIX.h` beside the library.
- An export whose name is not a C identifier gets one, with `_` for each
  other character: `add-one` becomes `add_one`.
- A name that C or the runtime already uses (a C keyword, `main`, `_start`,
  or one starting `warmc_`) gets the prefix: `int` becomes `calc_int`. The
  header gives the original name of every renamed export.
- An export that C cannot call gets no symbol, and warmc warns about it. That
  covers `v128` and reference types, more than 16 results, and, on arm64,
  more than 7 integer or 8 float parameters.

warmc refuses to build a library that imports a memory, table, global or tag,
or a function returning more than one result. On Windows an imported
function may take at most three parameters. `--shared` cannot be
combined with `--wasi`, `--no-export-names`, `--system-ld` or a component.

| Target | Default output | Linking a program against it |
|---|---|---|
| macOS | `libNAME.dylib`, install name `@rpath/libNAME.dylib` | `-L DIR -lNAME -Wl,-rpath,DIR` |
| Linux | `libNAME.so`, with no dependencies: the runtime needs no libc | `-L DIR -lNAME -Wl,-rpath,DIR` |
| Windows | `NAME.dll`, plus `NAME.def` beside it | an import library made from the `.def`: `llvm-dlltool -m i386:x86-64 -d NAME.def -l libNAME.dll.a`, or MSVC's `lib /def:NAME.def /machine:x64` |

`NAME` is the input's name without its extension. A Windows DLL needs an
x86-64-v3 CPU, like a Windows program. On an older CPU its first call says so,
and every call fails with trap 100.

Other languages load the same library through their own FFI. Python's
`ctypes`:

```python
import ctypes

calc = ctypes.CDLL("./libcalc.dylib")
scale = ctypes.CFUNCTYPE(ctypes.c_int64, ctypes.c_void_p, ctypes.c_int64)(
    lambda ctx, v: 10 * v)                      # keep it alive while calc may call it
calc.calc_bind_import(b"env", b"scale", scale, None)

calc.add.argtypes = [ctypes.c_int32, ctypes.c_int32]
calc.add.restype = ctypes.c_int32
calc.scaled.argtypes = [ctypes.c_int64]
calc.scaled.restype = ctypes.c_int64
print(calc.add(2, 3), calc.scaled(4))           # 5 40
```

Java's foreign function and memory API (Java 22 or newer), run with
`java --enable-native-access=ALL-UNNAMED Calc.java`:

```java
import java.lang.foreign.*;
import java.lang.invoke.*;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

public class Calc {
  static long scale(MemorySegment ctx, long v) { return 10 * v; }

  public static void main(String[] args) throws Throwable {
    Linker linker = Linker.nativeLinker();
    Arena arena = Arena.global();
    SymbolLookup calc = SymbolLookup.libraryLookup(Path.of("./libcalc.dylib"), arena);

    MethodHandle bind = linker.downcallHandle(calc.find("calc_bind_import").orElseThrow(),
        FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS));
    MemorySegment scale = linker.upcallStub(
        MethodHandles.lookup().findStatic(Calc.class, "scale",
            MethodType.methodType(long.class, MemorySegment.class, long.class)),
        FunctionDescriptor.of(JAVA_LONG, ADDRESS, JAVA_LONG), arena);
    int rc = (int) bind.invokeExact(arena.allocateFrom("env"), arena.allocateFrom("scale"),
                                    scale, MemorySegment.NULL);

    MethodHandle add = linker.downcallHandle(calc.find("add").orElseThrow(),
        FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT));
    System.out.println((int) add.invokeExact(2, 3));                    // 5
  }
}
```

`test/shared/` has a fuller C program, C++ use of the header, and Python and
Java tests; `test/run_shared.sh` runs them all.

### Components and WIT

warmc parses almost any Component Model binary. It compiles a component to
native code when the whole instance graph can be resolved at compile time. In
practice that means one of two cases:

- **WASI 0.2 command components.** Every import is a `wasi:*@0.2.*` instance,
  and the component exports `wasi:cli/run#run`.
- **Self-contained components whose exports take and return only `s32`, `u32`,
  `s64` and `u64`.** These run under the CLI harness, like a core module.

Inside such a component, warmc handles:
- several core modules and instances, and nested components
- outer aliases and exported instances (named `iface#fn`)
- resources (`new`, `drop` and `rep`, with destructors)
- lift-to-lower calls between component instances. warmc generates *fused
  adapter* modules that copy UTF-8 strings, lists, records, tuples, variants,
  enums, flags, options and results between memories.

Anything else is refused with a reason when you compile it. `--dump-link`
shows the resolved instance graph, and its `blocked:` lines name the bindings
that could not be resolved; the other reasons, such as an export that needs
conversion code, are given only when you compile.

Components are binary only: warmc's WAT assembler accepts core modules only.
Assemble component text with
[`wasm-tools`](https://github.com/bytecodealliance/wasm-tools), using
`wasm-tools parse x.wat -o x.wasm`.

| Flag | Prints |
|---|---|
| `--dump-component` | one line per definition, with nested components indented |
| `--dump-canon` | canonical-ABI size, alignment and flattening for each type of the outermost component, plus its canon definitions (nested components are not shown) |
| `--dump-adapter` | the canonical-ABI conversion code, as wasm text |
| `--dump-link` | the resolved instance graph: modules, instances, import bindings, lifts and lowers, exports, and why it is blocked, if it is |
| `--dump-component-wit` | the component's interfaces rendered back as WIT |
| `--dump-wit PATH` | what a `.wit` file, or a package directory with `deps/`, declares |

```console
$ build/warmc --dump-link composed.wasm
module 0: cm0, 2 funcs, 0 imports, 3 exports
module 1: cm1, 1 funcs, 0 imports, 2 exports
module 2: cm2, 2 funcs, 2 imports, 1 exports
module 3: ad1, 7 funcs, 5 imports, 1 exports
instance 2: module 3, 5 imports
  bind "caller" "mem" <- instance 1 "mem" [memory]
  bind "callee" "mem" <- instance 0 "mem" [memory]
  ...
canon 0: lift instance 0 "make" [func], core (i32) -> (i32), memory=yes
```

The WIT parser handles packages and versions, nested `package { }` blocks,
`use` across packages, worlds, resources, every type, `%` escapes and `deps/`
directories. `include` is recorded but not expanded. Feature-gate annotations
are parsed but not enforced.

### Other tools

| Flag | Effect |
|---|---|
| `--validate-only` | parse and validate, then stop. Exit codes: `0` valid, `2` malformed, `3` invalid. |
| `--emit-wasm` | assemble a `.wat` to binary (`-o`, default `out.wasm`) and stop |
| `--print-ir` | dump each function's IR, before and after optimisation, to stderr |
| `--stats` | print optimisation counters: folds, inlined calls, spills, `ldp`/`stp` pairs, vectorised loops, and so on |
| `--profile` | instrument the code to count executed instructions; prints `icount: N` at exit |
| `WARMC_TIME=1` | print per-phase compile and link timings, the peak memory reached by each compile phase, and the slowest functions to compile |

---

## Optimisation

| Level | What it does |
|---|---|
| `-O2` (default) | the full pipeline: inlining, devirtualisation, unrolling, GC-aware passes, and guard-page memory (which also enables the vectoriser and native-pointer caching) |
| `-O1` | the optimising pipeline without guard pages, inlining, devirtualisation, unrolling or the GC-purity passes |
| `-O0` | skips the IR optimiser. Register allocation and module-level reachability still run. |

You can switch individual passes with `-fPASS` / `-fno-PASS`. Flags apply in
command-line order, so put them **after** `-O`.

| Pass | What it does |
|---|---|
| `fold` | constant folding and algebraic simplification, including a constant reinterpreted between integer and float and two xor-shifts that cancel |
| `dce` | dead-code elimination |
| `cse` | common-subexpression elimination over dominators |
| `licm` | loop-invariant code motion, including hoisting invariant stores and trap guards; a value is taken out of the innermost loop that reads it and no further, and a native address stays by the access it serves, which folds it |
| `branch` | constant-branch folding and block merging |
| `edge-tests` | in loops, fold a test or branch that a dominating branch on the same value has already decided, such as an inlined bounds check behind a loop's own exit test |
| `fma` | off unless asked for: a float multiply whose one reader is an add or a subtract becomes one fused multiply-add (`fmadd`, `fmla`), rounded once. wasm rounds the multiply and the add each, so a result can differ in its last bit; `-ffma` is for code that does not depend on that, as native C compilers assume by default |
| `mulwide` | a 64-by-64 multiply that wasm spells in 32-bit halves -- `__multi3` from Rust, C and Zig, Go's `bits.Mul64` -- becomes one `mul` for the low half and `umulh`, or `smulh` for sign-extended operands, for the high |
| `ifconv` | if-conversion: a diamond or triangle whose arms are cheap and cannot trap becomes selects (`csel`), and so does a small `br_table` of cheap arms, its default dropped where the index is provably in range |
| `thread` | jump threading through a block that only switches on a parameter, such as the dispatch Go's compiler puts around every function, or only compares one with a constant, when an edge passes that parameter a constant |
| `nodesplit` | copy the part of a loop that a switch outside it enters in the middle, so that the loop has one way in again and the loop passes can take it |
| `mem` | store-to-load forwarding, also of bytes stored as one type and loaded as another, and dead-store removal, also of a store of the value the slot already holds, using per-function effect summaries |
| `bce` | bounds-check elimination by value range and dominating checks |
| `strength` | strength reduction: induction variables, shifts, magic-number division; division by a loop-invariant divisor through a reciprocal made before the loop; the remainder of a division just made as a multiply-subtract; a post-decrement loop test on the new count |
| `inline` | call-site-scored inlining (on by default only at `-O2`) |
| `reassoc` | rebalance associative integer chains for latency, also through a wrap to 32 bits, and add the value a loop carries round last in its chain, so that an iteration waits on one op of it, in v128 lanes too |
| `rotate` | loop rotation, of loops whose latch ends in an unconditional jump |
| `unroll` | 4× unrolling of constant-trip-count loops; complete unrolling of short ones, and of a loop nest whose outer loop runs at most 8 times; a loop that runs once loses its back edge |
| `vectorize` | auto-vectoriser: contiguous accesses and ones interleaved two, three or four ways (`ld3` and `ld4` on arm64), if-converted bodies, bytes and halfwords computed in lanes twice as wide where their values need it (widening multiply-adds, a rounding shift as an add-high-narrow), integer, widening and float min/max reductions, argmin and argmax (an index kept beside a min or max), search loops, saturating arithmetic, high-half multiplies, byte reversal and int/float conversions; several copies of the body per iteration, each with its own accumulator; a loop a producer unrolled by three, unrolled again until it spans whole vectors; with runtime alias checks and a scalar fallback |
| `addroff`, `memptr` | fold address arithmetic into displacements; compute a native pointer once per base, and once per block for constant addresses too large for a displacement |
| `ldcomb` | merge byte-assembling loads into one wide load |
| `selftail` | turn self `return_call` into a loop |
| `gloop`, `gssa` | keep globals in registers across loops; promote a global to SSA where that does fewer memory operations on it, such as Go's stack pointer or a shadow stack pointer moved in a loop (`-fgssa-all` promotes every one, which costs more than it saves) |
| `loopclose` | loop-closed SSA for values used after a loop |
| `escape` | remove GC objects that never escape and forward their fields |
| `partial-escape` | allocate a GC object that escapes on some paths only where it escapes |
| `devirt`, `closed-world` | turn `call_ref` and `call_indirect` into direct calls, also through vtables in immutable globals; with a closed world, use vtable slots too |
| `ictab` | a `call_indirect` through a table nothing writes loads its target from a code table per type, whose slots that fail the call's checks hold trap stubs, so the call needs no signature, null or instance check |
| `reinline` | after the cleanup passes, inline again what devirtualisation made direct and the calls an allocation leaves by |
| `throw-jump` | a `throw` caught in its own function, without the exnref, becomes a jump to the catch |
| `gcpure` | treat immutable GC fields and `array.len` as pure; drop redundant null and bounds checks, and casts and type tests that a dominating `ref.test` or cast already decides |
| `labcache`, `barelide` | keep the GC allocation buffer open, and hoist the load-barrier mask, in safepoint-free loops |
| `reach`, `prune`, `merge` | module-wide reachability; skip unreachable bodies; share byte-identical functions |
| `split` | live-range splitting at loop boundaries in loops that make no call, so a value the loop carries keeps a register inside it although it is spilled outside |
| `align`, `small-loop-align` | code alignment; a loop small enough for one 64-byte fetch block is placed inside one |
| `cpool`, `regalloc`, `guard-pages` | constant pools; the real register allocator (off: every value lives in a stack slot); guard-page bounds checking |

`-fno-loadbar` stops emitting GC load barriers. That is unsound for a module
with GC references, because relocation heals them through the barrier; it
exists so that `test/run_nobar.sh` can show that a module without GC
references compiles to the same bytes either way.

`-j N` (or `WARMC_JOBS`) caps the number of compile threads. By default warmc
picks a count from the module size and the number of cores. Functions are
placed in index order whatever the scheduling, but `-j1` is the setting that
is always exact. Use it if you ever see output differ between runs.

### How memory is protected

At `-O2`, each 32-bit linear memory reserves 4 GiB plus a 32 MiB guard region
of address space. An access whose static offset is within the guard needs no
explicit check: a fault there becomes the `out of bounds memory access` trap.
At `-O1` and `-O0`, and always for memory64, accesses are checked explicitly,
and the `bce` pass removes the checks it can prove redundant. Stack overflow
is detected by a prologue check in every function that makes calls. It traps
as `call stack exhausted` rather than crashing.

---

## Command-line reference

| Option | Meaning |
|---|---|
| `-o FILE` | output path (default `a.out`, `a.exe` on Windows, `out.o` with `-c`, `libNAME.so`/`libNAME.dylib`/`NAME.dll` with `--shared`) |
| `-c` | write a relocatable object and stop |
| `--shared` | build a shared library (see [Shared libraries](#shared-libraries)) |
| `--header PATH` | where `--shared` writes the header (default `PREFIX.h` beside the library) |
| `--target linux\|macos\|windows` | output format and ISA (default `linux`; `darwin` means `macos`) |
| `--rt PATH` | runtime archive to link (default `$WARMC_RT`) |
| `--wasi[=PATH]` | link a WASI host and run `_start` (core module) or `wasi:cli/run` (component) |
| `-O0` `-O1` `-O2` | optimisation level (default `-O2`) |
| `-fPASS`, `-fno-PASS` | toggle one pass (see [Optimisation](#optimisation)) |
| `-j N`, `-jN` | maximum number of compile threads |
| `--profile=full\|deterministic` | the WebAssembly profile. `deterministic` makes relaxed-SIMD and NaN results platform-independent. |
| `--symbol-prefix=P` | name the module descriptor `P_module`; with `--shared`, the prefix of the library's own functions instead; ignored for a component |
| `--no-export-names` | drop the export-name table, keeping only `_start` and `main` |
| `--no-dead-strip` | keep unreferenced sections in the link |
| `--system-ld`, `--ld PATH` | link with an external linker instead (Linux and macOS only, not with `--shared`) |
| `--keep-obj` | keep the intermediate object |
| `-v` | print the external link command |
| `--validate-only`, `--emit-wasm`, `--dump-*`, `--print-ir`, `--stats`, `--profile`, `-fno-loadbar` | see above |
| `--magic=D[:64]` | print the multiply, add and shift that divide by `D` (unsigned; 32-bit, or 64-bit with `:64`) and stop |
| `-h`, `--help` | print the option summary |

`--target`, `--rt`, `--ld` and `--header` also take their value after an `=`,
as in `--target=macos`.

### Environment variables

| Variable | Read by | Effect |
|---|---|---|
| `WARMC_RT`, `WARMC_WASI` | compiler | defaults for `--rt` and `--wasi=` |
| `WARMC_LD` | compiler | the external linker `--system-ld` runs (unlike `--ld`, it does not turn `--system-ld` on) |
| `WARMC_SYSTEM_LD` | compiler | same as `--system-ld` |
| `WARMC_MACOS_LIBDIR` | compiler | directory holding `libSystem.tbd` for `--system-ld` on macOS (otherwise found with `xcrun`) |
| `WARMC_JOBS` | compiler | same as `-j` |
| `WARMC_TIME` | compiler | print phase timings, peak memory and the slowest functions |
| `WARMC_GC_HEAP_KB` | program | GC heap reservation (default 1 GiB) |
| `WARMC_GC_MIN_KB`, `WARMC_GC_GROWTH` | program | collection trigger: minimum size, and growth as a percentage of live data |
| `WARMC_GC_CONC` | program | `0` disables the background collector thread |
| `WARMC_GC_STATS` | program | print a GC report at exit |
| `WARMC_GC_VERIFY`, `WARMC_GC_STRESS` | program | heap verification after each cycle; collect on every allocation |
| `WARMC_RANDOM_SEED` | program | make WASI preview 1 `random_get` deterministic |

Many more `WARMC_*` variables exist for debugging individual passes, the
register allocator, the linker and the collector. Search for `getenv` in
`src/` and for `rt_getenv` in `rt/`. A shared library reads the runtime's
variables, `WARMC_GC_*` among them, at its first call.

---

## WebAssembly feature support

| Feature | Status |
|---|---|
| Core 1.0 / 2.0: multi-value, bulk memory, reference types, saturating conversions, sign extension | ✅ |
| SIMD (`v128`) and relaxed SIMD | ✅ (NEON on AArch64, AVX2 on x86-64) |
| Tail calls | ✅ |
| Typed function references | ✅ |
| GC: struct, array, i31, recursive types and subtyping, casts | ✅ |
| Exception handling: `try_table`, `throw`, `throw_ref`, `exnref` | ✅ |
| Exception handling: legacy `try` / `catch` / `delegate` | ❌ |
| memory64 and table64 | ✅ (memory capped at 65536 pages; a 32-bit memory at 65535) |
| Multi-memory | ✅ |
| Extended constant expressions | ✅ |
| Deterministic profile | ✅ `--profile=deterministic` |
| Threads, shared memory, atomics | ❌ a shared memory is rejected when parsing, atomic instructions when validating |
| Stack switching, stringref, wide arithmetic, custom page sizes, custom descriptors | ❌ |
| Component Model | partial, see [Components and WIT](#components-and-wit) |

---

## How it works

```
 .wat ──► WAT assembler ──┐
                          ├─► parser ─► validator ─► SSA IR builder ─► optimiser ─► instruction selection ─► register allocation
 .wasm ───────────────────┘                                                         (AArch64 or x86-64)       (linear scan)
                                                                                                                   │
          executable ◄── built-in linker ◄── runtime archive (+ WASI host) ◄── ELF / Mach-O / COFF object ◄────────┘
```

**Front end** (`src/parse.c`, `validate.c`, `wat.c`, `component.c`, `wit.c`).
These decode, assemble and validate core modules, components and WIT. The text
assembler handles both flat and folded syntax, names in every index space,
inline imports, exports and segments, and GC type definitions.

**Middle end** (`src/ir*.c`, `opt.c`, `inline.c`). Functions become SSA with
block parameters instead of phi nodes. SSA is built directly from wasm locals,
and exception edges lead to landing-pad blocks. The optimiser runs the pass
pipeline described above, using module-wide effect summaries, reachability and
devirtualisation.

**Back end** (`src/isel*.c`, `regalloc.inc`, `a64.c`, `x64.c`, `codegen.c`).
Instruction selection fuses compares into branches and selects, folds shifts,
bitfield extracts, multiply-adds and addressing modes, and pairs loads and
stores into `ldp`/`stp`. `br_table` becomes a jump table. The register
allocator is a linear scan over SSA webs with lifetime holes. It
rematerialises constants, recomputes a spilled frame address or other cheap
operation from registers instead of reloading it, holds a spilled value that
a loop reads in a register that is free across the loop, and chooses between
callee-saved registers and caller-saved registers with save and restore
around calls, whichever is cheaper. Functions compile in parallel and are
laid out deterministically.

**Linker** (`src/link.c`, `pe.c`, `elf.c`, `macho.c`, `coff.c`):
- Linux: a static, non-PIE ELF with no libc.
- macOS: a PIE that imports libSystem through a GOT and is signed ad hoc
  in-process.
- Windows: a PE32+ whose DLL imports come from a curated table, so no SDK is
  needed.
- With `--shared`: an ELF shared object with its own dynamic symbol table,
  hash table and relative relocations; a Mach-O dylib with an export trie; or
  a DLL with an export directory and base relocations. `src/shlib.c` adds a
  stub per export that enters the runtime through `rt/lib.c`, which reads the
  platform's C calling convention and makes the instance on the first call.
- On Linux and macOS the executable keeps its function names, local ones
  included: each compiled wasm function is `wasm_func_N` (its index in the
  module), next to the runtime's own functions, so `perf`, Instruments,
  `sample` and debuggers can name the code they land in.

**Runtime** (`rt/`):
- **Platforms.** On Linux the runtime makes raw syscalls, on macOS it uses
  libSystem, and on Windows it uses Win32. The WASI hosts are written against
  POSIX: `rt/linuxposix.c` provides it over Linux system calls, and
  `rt/winposix.c` over Win32.
- **Traps.** Signal or vectored exception handlers turn guard-page faults into
  wasm traps.
- **Garbage collector.** It is concurrent and compacting:
  - Pointers are coloured. AArch64 top-byte-ignore lets compiled code
    dereference tagged references directly, and x64 masks the tag off.
  - Marking and evacuation of sparse pages run on a background thread.
  - The program stops only for short handshake rounds.
  - Roots come from precise per-call-site stack maps.
  - A load barrier in compiled code heals stale references.
- **Exceptions.** They are unwound by walking warmc's own compact unwind
  tables, with no DWARF and no libunwind.

---

## Performance

`test/baseline.json` records warmc against two references on the author's
Apple silicon machine: wasmtime 50.0.0 running the same wasm, and the same
source compiled natively. warmc runs at `-O2`. Each figure is the geometric
mean of warmc's time divided by the reference's, so below 1 means warmc is
faster. Each program's empty-program time is subtracted first, so the ratios
compare steady-state code rather than start-up or compile time. The
wasmtime and native times are timed interleaved with warmc's, round by
round (`make bench-record`); warmc's column is re-timed on its own against
them on the same machine (`make bench-record-warmc`), each kernel's figure
the best of two runs of seven rounds.

The wasm comes from each language's usual producer: TinyGo 0.43 at
`-opt=2`, Go 1.27.3, rustc nightly 1.101 at `opt-level=3` with LTO, Zig 0.17
`ReleaseFast`, AssemblyScript 0.28.20 at `-O3`, Kotlin 2.4.21, wasi-sdk 34
(clang 23.1) at `-O3`, and GHC 9.14 at `-O2`. Only the Zig and AssemblyScript
corpora use wasm SIMD. The native builds use the same language's own
compiler at `-O2`, or its nearest equivalent: clang 23.1 `-O2`, rustc
`opt-level=2`, Zig `ReleaseFast`, TinyGo `-opt=2`, `go build` and GHC `-O2`
(with `-with-rtsopts=-V0`, see below).
AssemblyScript and Kotlin/Wasm have no native compiler to compare with.

| Corpus | Source language | Kernels | warmc / wasmtime | Range | warmc / native | Range | Median peak RSS, MiB (warmc / wasmtime / native) |
|---|---|---:|---:|---|---:|---|---|
| `gobench` | TinyGo | 39 | 0.57 | 0.06 – 1.04 | 0.84 | 0.13 – 2.97 | 2.0 / 8.9 / 1.9 |
| `stdgo` | Go | 49 | 0.56 | 0.03 – 1.07 | 1.52 | 0.08 – 8.78 | 7.4 / 27.4 / 4.7 |
| `vecbench` | TinyGo (vectorisable loops) | 43 | 0.17 | 0.03 – 0.96 | 0.68 | 0.10 – 1.18 | 2.0 / 8.9 / 1.9 |
| `asbench` | AssemblyScript | 37 | 0.57 | 0.16 – 0.97 | – | – | 1.9 / 8.7 / – |
| `rsbench` | Rust | 39 | 0.61 | 0.12 – 0.95 | 1.01 | 0.60 – 2.63 | 2.0 / 9.1 / 1.9 |
| `zigbench` | Zig | 22 | 0.70 | 0.17 – 1.00 | 0.94 | 0.21 – 1.71 | 2.0 / 8.9 / 1.9 |
| `ktbench` | Kotlin (Wasm GC) | 25 | 0.10 | 0.003 – 0.68 | – | – | 3.5 / 22.7 / – |
| `cbench` | C | 27 | 0.73 | 0.27 – 0.98 | 1.01 | 0.008 – 4.44 | 2.0 / 9.0 / 1.8 |
| `cppbench` | C++ | 26 | 0.66 | 0.21 – 1.03 | 0.86 | 0.02 – 1.50 | 2.4 / 9.5 / 2.1 |
| `hsbench` | Haskell | 28 | 0.59 | 0.35 – 0.87 | 1.77 | 0.58 – 6.06 | 10.0 / 32.4 / 12.8 |

### Against wasmtime

Over all 335 kernels, warmc's code takes **0.45×** wasmtime's time, and
0.44× without `stdgo`. Three kernels are slower than wasmtime:

| Kernel | warmc / wasmtime | What is known |
|---|---:|---|
| `stdgo/memcpy` | 1.07 | the platform's memmove under both engines, whose ratio spreads by more than 10% between runs |
| `gobench/strswitch` | 1.04 | code placement: the same instructions laid out with no loop header aligned run 5% faster, below wasmtime |
| `cppbench/algorithms` | 1.03 | code placement, as for `strswitch`: 3% faster with no loop header aligned |

TinyGo's `-opt=2` unrolls loops that its size-first default leaves alone,
which hides them from some of warmc's loop passes. LLVM's unrolled byte search
is vectorised as the rolled one is (`vecbench/search`, 0.29×), and a
callee whose loop LLVM unrolled into several calls through a closure is
inlined where the closure is a constant, so the calls inline too
(`gobench/closures`, 0.15×). A loop LLVM unrolled by three, whose stride
neither divides a vector nor is a whole number of them, is unrolled again
until it spans three vectors and vectorised: `vecbench/neighbour` runs at
0.20×, and `cbench/matmul`, whose inner loop is the same shape in `f64`, at
0.41×.

`stdgo` is Go built by Go's own compiler, which is not LLVM. It compiles
every function to a loop around a `br_table` over the function's resume
points, so that a goroutine switch can unwind the wasm stack and re-enter the
function where it left off, and it sends each jump inside the function -- a
loop's back edge among them -- through that dispatch with a constant index.
warmc threads those jumps to their targets (`thread`), gives the dispatch a
cold copy of the loops it can enter in the middle so that the originals have
one way in again (`nodesplit`), and keeps Go's stack pointer in a register
between calls (`gssa`). With the loops back, the vectoriser takes Go's
64-bit address arithmetic too: `stdgo/strsum` runs at 0.03× and `stdgo/sum`
at 0.04×.

### Against native code

Over the 272 kernels with a native build, warmc's code takes **1.03×** the
native time (median 1.04), and 0.97× without Haskell. wasmtime's code takes
2.05× the native time on the same kernels. 118 kernels run faster under warmc
than natively, and another 39 are within 10% of the native build.

The native builds start ahead of the wasm in a few ways that have nothing to
do with warmc's compiler:
- **SIMD.** LLVM's native auto-vectoriser uses NEON, while the TinyGo, C, C++
  and Rust corpora are built without wasm SIMD. The vector code they get is
  what warmc's own vectoriser makes of scalar loops. It takes saturating
  arithmetic, high-half multiplies, byte reversals and int/float conversions,
  and runs several copies of a loop's body per iteration with an accumulator
  each: `satadd`, `mulhi16` and `bswap` run at 0.59×, 0.67× and 0.85×
  native, and `vecbench/neighbour`, a loop LLVM unrolled by three, at 0.88×.
  `vecbench/rgbgrey` reads pixels three bytes apart, loads them with one
  `ld3` and computes in 16-bit lanes as the native build does, at 0.92×.
- **Hand-written assembly.** `stdgo/sha256` is 9× because native Go computes
  SHA-256 with the ARMv8 SHA-256 instructions, where the wasm build runs the
  portable Go code.
- **The producer's wasm back end.** Go's wasm port keeps nearly everything
  64-bit and dispatches through resume points (see above). GHC's wasm backend
  returns every function to a trampoline in the runtime, where its native code
  generator jumps directly; that is most of `hsbench`'s 1.77×.

The native build is slower where the platform's own mechanism costs more
than wasm's:
- `cbench/setjmp` runs at 0.01× native: macOS's `setjmp` and `longjmp` save
  and restore the signal mask, a system call each, and warmc lowers them onto
  wasm exceptions.
- `cppbench/exceptions` runs at 0.02×: a native C++ throw goes through the
  platform's two-phase unwinder, and warmc unwinds through its own compact
  tables.
- `stdgo/atomics` runs at 0.08×: native Go's atomics are real atomic
  instructions with barriers, while Go on wasm is single-threaded and its
  atomics are plain loads and stores.

45 kernels print a different result natively, though they do the same work.
Go's `int`, Rust's `usize`, C's `long` and Haskell's `Int` are 32 bits on
wasm32 and 64 natively. Native floating-point code is also not always rounded
as wasm's is: clang fuses multiply-adds natively, for one, which warmc does
only when asked to with `-ffma`. Without those 45
kernels the geometric mean is 1.05×. The Haskell natives are built with
`-with-rtsopts=-V0`: without it the runtime's exit waits out its 10 ms ticker,
which made the empty program's time mostly that wait and credited every
kernel with time it never spent. One native time is too short to time
reliably, under 5 ms: `vecbench/rowsum`, which is left out of the native
figures.

Re-run the comparison on your own machine before relying on these numbers:

```sh
python3 test/gobench.py --corpus rsbench --rounds 5     # one corpus, against wasmtime
python3 test/gobench.py --corpus rsbench --native       # and against the native build
make bench-compare                                      # all corpora vs the recorded baseline
make bench-record                                       # re-record it, native column included
make bench-record-warmc                                 # re-time warmc's column alone against it
```

---

## Testing

```sh
make check          # the full differential test suite (rows run in parallel; logs in build/checklogs)
make smoke          # a few of its rows that reach most of warmc in seconds; what CI runs on every push
make check-encoder  # the AArch64 encoder against GNU as, llvm-mc or clang; the x64 one against llvm-mc
make check-spec     # the official WebAssembly testsuite, at three optimisation settings
make conformance    # write CONFORMANCE.md: spec results grouped by proposal
make fuzz           # random modules vs wasm-interp (FUZZ_SEED, FUZZ_COUNT)
make gc-jitter      # GC schedule sweep with randomised collector delays
make check-linux    # run the GC tests, and on a macOS host with tinygo the TinyGo WASI corpus, as real Linux binaries (podman/docker arm64)
```

Most of the suite is **differential**. warmc's output is compared with:
- wabt's `wasm-interp`, for the hand-written cases in `test/cases` (at every
  `-O` level and with individual passes turned off). `test/cases_gc`,
  `cases_traps` and `cases_relaxed` are checked against the results written
  in their scripts.
- wasmtime, for WASI preview 1 and 0.2 programs (stdout, stderr, exit code and
  the resulting directory tree)
- wasmtime, for the benchmark corpora in Go, Rust, Zig, AssemblyScript,
  Kotlin, C, C++ and Haskell
- wasmtime, at `-O0`, `-O1` and `-O2`, for 310 test programs that are not
  benchmarks: `test/ctests` (C, 36), `cpptests` (C++, 40), `rstests` (Rust,
  34), `gotests` (TinyGo, 36), `stdgotests` (Go, 44), `astests`
  (AssemblyScript, 40), `kttests` (Kotlin, 22), `zigtests` (Zig, 35) and
  `hstests` (Haskell, 23). Each one covers a single language area: integer
  and float edge cases, strings, collections, closures, exceptions, generics,
  the GC and the runtime library. Each prints only results that wasm fixes
  exactly; for example, it prints no arithmetic NaN bits.
- `wasm-tools`, for component, WIT and WAT handling
- the Component Model's reference `definitions.py`, for canonical-ABI layout

`test/run_shared.sh` builds a shared library and calls it from C, C++,
Python's `ctypes` and Java's foreign function API. The Java part needs Java 22
or newer, and skips itself otherwise.

Requirements:
- `make check` needs `python3`, `perl` and wabt (`wat2wasm`, `wasm-interp`).
  It also needs clang's `wasm32` target with `wasm-ld`.
- Four dependencies are pinned in the Makefile and cached under `build/`:
  `wasm-tools` (built with `cargo` by `make wasm-tools`), the spec testsuite
  (`make testsuite`), the Kotlin/Wasm stdlib matching your `kotlinc`
  (`make kotlin-stdlib`), and wasi-sdk, the clang and sysroot the C and C++
  corpora are built with (`make wasi-sdk`). Each cached copy is stamped with
  its version, so bumping a pin or upgrading `kotlinc` fetches it again.
  `make check` brings `wasm-tools`, wasi-sdk and the Kotlin stdlib up to date
  by itself; `make check-spec` does the same for the testsuite.
- Rows that need wasmtime, TinyGo, Go, Rust (`wasm32-wasip1` / `wasm32-wasip2`),
  Zig, `asc`, `kotlinc-wasm` or `wasm32-wasi-ghc` skip themselves when the tool
  is missing. GHC's wasm backend is not fetched by the Makefile: install it
  with ghc-wasm-meta's `bootstrap.sh` (the `hsbench` corpus was recorded with
  `FLAVOUR=9.14`), which puts it in `~/.ghc-wasm`, where `test/gobench.py`
  finds it without `~/.ghc-wasm/env` being sourced.

---

## Repository layout

```
src/            the compiler
  main.c          command line
  util.c          allocation, diagnostics, byte buffers and object symbol tables
  parse.c validate.c wat.c opcodes.c      front end
  component.c canon.c clink.c wit.c witgen.c   Component Model and WIT
  ir.c ir_build.c opt.c inline.c          SSA IR and optimiser
  isel*.c isel_*.inc regalloc.inc         instruction selection and register allocation
  a64.c x64.c                             machine-code encoders
  codegen.c compile.c module_data.c       per-function emission, parallel driver, module descriptor
  elf.c macho.c coff.c pe.c link.c        object writers and linker
  shlib.c                                 --shared: the C entry points, header and .def
rt/             the runtime linked into every program and shared library
  rt.c            instantiation, traps, GC, exceptions, CLI harness
  start.S start_x64.S                     entry, invoke, throw and GC trampolines
  lib.c                                   a shared library's face: the C calls, its lock and its traps
  hook.c gcthook.c gcthtest.c             defaults a driver or the GC thread tests replace
  modules.c component.c                   the descriptor tables of a program that is not a component
  wasi.c wasi.wat                         WASI preview 1 host
  wasifs.c                                the filesystem of both WASI hosts
  wasip2.c                                WASI 0.2 host
  linuxposix.c                            the WASI hosts' POSIX calls over Linux syscalls
  win.inc winposix.c                      Windows platform layer
  warm_rt.h                               the embedding API
test/           test drivers, cases, benchmark corpora, spec harness
example/        TinyGo and Kotlin sample programs (source, .wasm and a macOS build)
```

---

## Limitations

- **x86-64 is supported for Windows only.** Linux and macOS output is AArch64.
- **No threads.** Shared memories and atomics are rejected.
- **No WASI 0.2 on Windows yet.** On Windows, the preview 1 host has no
  sockets, so `--tcplisten` fails there.
- **WASI 0.2** has no `wasi:sockets` or `wasi:http`.
- **Components** compile only when every import is WASI 0.2 and every
  non-`run` export uses only 32-bit and 64-bit integers. The canonical ABI is
  32-bit only.
- **The name section is ignored**, so functions appear as `wasm_func_N`.
- **Exceptions** do not propagate through host (C) frames.
- **The collector** does not run while wasm is re-entered from a host call.
- **Instances** are never freed, and a process holds at most 4096 of them.
- **A shared library** holds one instance. Calls from several threads take
  turns on it, and its imports cannot be rebound once it has been made. On
  Windows an imported function takes at most three parameters.
