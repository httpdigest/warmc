# warmc

**warmc** is an ahead-of-time compiler that turns WebAssembly into standalone
native executables. It needs no JIT, interpreter or VM process at run time.

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

- **Self-contained toolchain.** warmc links executables itself. It reads
  `.a` archives and ELF, Mach-O and COFF objects, dead-strips unused
  sections, and resolves Windows DLL imports from a built-in table. It needs no
  `ld`, SDK import libraries or `lld` for normal use.
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
  - magic-number division
  - a linear-scan register allocator with rematerialisation

  Functions compile in parallel and are laid out in a deterministic order.
- **Fast code.** Across 207 benchmark kernels compiled from Go, TinyGo, Rust,
  Zig, AssemblyScript, Kotlin, C, C++ and Haskell, warmc's steady-state code
  takes 0.48× wasmtime's time (geometric mean), with much lower peak memory.
  See [Performance](#performance).
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

---

## Building

### Prerequisites

| Tool | Why |
|---|---|
| **clang** with the AArch64 and X86 backends (a recent LLVM; Homebrew's works) | builds `warmc` and cross-compiles the three runtimes |
| **llvm-ar** | archives the runtimes (Linux archives fall back to `ar`) |
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

# warmc always needs a runtime archive: pass --rt, or set WARMC_RT once.
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

warmc decides whether the input is binary or text from its contents (the
`\0asm` magic number), not from its file name. A file named `*.wasm` is always
read as binary.

### Choosing a target

| Target | Flags | Default output |
|---|---|---|
| Linux / AArch64 | `--rt build/warmrt.a` | `a.out` |
| macOS / arm64 | `--target macos --rt build/warmrt-macos.a` | `a.out` |
| Windows / x86-64 | `--target windows --rt build/warmrt-windows.a` | `a.exe` |

`-o FILE` names the output. `-c` stops after writing a relocatable object
(`out.o` unless you pass `-o`).

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
- `--bench N` calls the export N times and prints min, median, max and mean
  nanoseconds to stderr.
- **Exit codes:**
  - `0` success
  - `1` trap, printed to stderr as `trap: <spec message>`
  - `2` unknown export or wrong argument count
  - `4` unresolved import (the harness provides no imports)

`--no-export-names` removes the export-name table from the binary, which also
disables this harness.

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
| Kotlin/Wasm | `kotlinc-wasm -Xwasm -Xwasm-target=wasm-wasi …` (two steps, klib then module; see `build_kotlin` in `test/gobench.py`) |
| C | `clang -O3 main.c -o app.wasm` with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk)'s clang (`make wasi-sdk` fetches the pinned release); add `-mllvm -wasm-enable-sjlj -lsetjmp` for `setjmp`/`longjmp` |
| C++ | `clang++ -O3 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false main.cpp -lunwind -o app.wasm` (wasi-sdk), or without the exception flags for code that never throws |
| Haskell | `wasm32-wasi-ghc -O2 Main.hs -o app.wasm`, with GHC's wasm backend from [ghc-wasm-meta](https://gitlab.haskell.org/haskell-wasm/ghc-wasm-meta) |

Then compile it:

```sh
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
```

The resulting program reads its own sandbox flags anywhere before a `--`. Every
other argument becomes the guest's `argv`:

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
| 71 | link failure |
| 72 | no `_start` |
| 73 | trap, printed to stderr |
| 74 | bad host flag |

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
`std::process::exit` does). If the component calls a
`wasi:*` function this host does not implement, the program traps when the
call happens and names the function. It is not refused at link time.

### Embedding compiled modules in C

`-c` produces an object that exports a single symbol, the module descriptor
`warmc_module`. With `--symbol-prefix=P`, the descriptor is named `P_module`,
so several modules can live in one program. The runtime owns the process entry
point. To take control, replace the CLI harness by defining
`warmc_driver_hook`.

```c
/* host.c */
#include "warm_rt.h"

extern const ModuleDesc guest_module;    /* warmc -c --symbol-prefix=guest */
const ModuleDesc warmc_module = { 0 };   /* the default harness still refers to this */

/* A host import: it receives the bound instance, then the wasm arguments as u64. */
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
| `warmc_instantiate(md)`, `warmc_instantiate_catching(md, &err)` | instantiate a module and run its start function; the `_catching` form returns a trap code instead of exiting |
| `warmc_register(name, inst)` | make an instance's exports importable under module `name`, so modules can import one another. Shared memories, tables, globals and tags all work. |
| `warmc_bind_import(inst, module, name, fn, self)` | rebind an already-linked function import to a host function |
| `warmc_invoke(...)`, `warmc_invoke_catching(...)` | call an export. Integer and reference args go in `iargs[0..6]`. Floats go in `fargs[2*i]`. Results come back in `ret[WARMC_RET_X0]` (integer), `ret[WARMC_RET_V0]` (float) or `ret[WARMC_RET_MULTI(i)]` (multi-value). |
| `warmc_invoke_s`, `warmc_invoke_catching_s` | x64 only: needed for functions with more than 3 integer or 4 float parameters |
| `warmc_global_cell(inst, idx)`, `inst->mem_base` / `mem_size` | access globals and linear memory 0 |
| `warmc_gc_collect()`, `warmc_gc_hold()` / `warmc_gc_release()` | drive the collector; register C-side roots |

Limits: at most 4096 instances per process, and instances are never freed.

`test/link/driver.c` (multi-module linking) and `test/tinygo/driver.c`
(hand-written WASI imports) are complete examples.

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

Anything else is refused with a reason. `--dump-link` shows that reason in its
`blocked:` lines.

Components are binary only: warmc's WAT assembler accepts core modules only.
Assemble component text with
[`wasm-tools`](https://github.com/bytecodealliance/wasm-tools), using
`wasm-tools parse x.wat -o x.wasm`.

| Flag | Prints |
|---|---|
| `--dump-component` | one line per definition, with nested components indented |
| `--dump-canon` | canonical-ABI size, alignment and flattening for every type, plus each canon definition |
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
| `WARMC_TIME=1` | print per-phase compile and link timings |

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
| `fold` | constant folding and algebraic simplification |
| `dce` | dead-code elimination |
| `cse` | common-subexpression elimination over dominators |
| `licm` | loop-invariant code motion, including hoisting invariant stores and trap guards |
| `branch` | constant-branch folding and block merging |
| `thread` | jump threading through a block that only switches on a parameter, such as the dispatch Go's compiler puts around every function, when an edge passes that parameter a constant |
| `nodesplit` | copy the part of a loop that a switch outside it enters in the middle, so that the loop has one way in again and the loop passes can take it |
| `mem` | store-to-load forwarding and dead-store removal, using per-function effect summaries |
| `bce` | bounds-check elimination by value range and dominating checks |
| `strength` | strength reduction: induction variables, shifts, magic-number division |
| `inline` | call-site-scored inlining (on by default only at `-O2`) |
| `reassoc` | rebalance associative integer chains for latency |
| `rotate` | loop rotation |
| `unroll` | 4× unrolling of constant-trip-count loops |
| `vectorize` | auto-vectoriser: contiguous and interleaved accesses, if-converted bodies, integer and widening reductions, search loops; with runtime alias checks and a scalar fallback |
| `addroff`, `memptr` | fold address arithmetic into displacements; compute a native pointer once per base |
| `ldcomb` | merge byte-assembling loads into one wide load |
| `selftail` | turn self `return_call` into a loop |
| `gloop`, `gssa` | keep globals in registers across loops; promote a global to SSA where that does fewer memory operations on it, such as Go's stack pointer or a shadow stack pointer moved in a loop (`-fgssa-all` promotes every one, which costs more than it saves) |
| `loopclose` | loop-closed SSA for values used after a loop |
| `escape` | remove GC objects that never escape and forward their fields |
| `partial-escape` | allocate a GC object that escapes on some paths only where it escapes |
| `devirt`, `closed-world` | turn `call_ref` and `call_indirect` into direct calls, also through vtables in immutable globals; with a closed world, use vtable slots too |
| `reinline` | after the cleanup passes, inline again what devirtualisation made direct and the calls an allocation leaves by |
| `throw-jump` | a `throw` caught in its own function, without the exnref, becomes a jump to the catch |
| `gcpure` | treat immutable GC fields and `array.len` as pure; drop redundant null and bounds checks |
| `labcache`, `barelide` | keep the GC allocation buffer open, and hoist the load-barrier mask, in safepoint-free loops |
| `reach`, `prune`, `merge` | module-wide reachability; skip unreachable bodies; share byte-identical functions |
| `align`, `cpool`, `regalloc`, `guard-pages` | code alignment; constant pools; the real register allocator (off: every value lives in a stack slot); guard-page bounds checking |

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
| `-o FILE` | output path (default `a.out`, `a.exe` on Windows, `out.o` with `-c`) |
| `-c` | write a relocatable object and stop |
| `--target linux\|macos\|windows` | output format and ISA (default `linux`) |
| `--rt PATH` | runtime archive to link (default `$WARMC_RT`) |
| `--wasi[=PATH]` | link a WASI host and run `_start` (core module) or `wasi:cli/run` (component) |
| `-O0` `-O1` `-O2` | optimisation level (default `-O2`) |
| `-fPASS`, `-fno-PASS` | toggle one pass (see [Optimisation](#optimisation)) |
| `-j N` | maximum number of compile threads |
| `--profile=full\|deterministic` | the WebAssembly profile. `deterministic` makes relaxed-SIMD and NaN results platform-independent. |
| `--symbol-prefix=P` | name the module descriptor `P_module` |
| `--no-export-names` | drop the export-name table, keeping only `_start` and `main` |
| `--no-dead-strip` | keep unreferenced sections in the link |
| `--system-ld`, `--ld PATH` | link with an external linker instead (Linux and macOS only) |
| `--keep-obj` | keep the intermediate object |
| `-v` | print the external link command |
| `--validate-only`, `--emit-wasm`, `--dump-*`, `--print-ir`, `--stats`, `--profile` | see above |

### Environment variables

| Variable | Read by | Effect |
|---|---|---|
| `WARMC_RT`, `WARMC_WASI`, `WARMC_LD` | compiler | defaults for `--rt`, `--wasi=` and `--ld` |
| `WARMC_SYSTEM_LD` | compiler | same as `--system-ld` |
| `WARMC_MACOS_LIBDIR` | compiler | directory holding `libSystem.tbd` for `--system-ld` on macOS (otherwise found with `xcrun`) |
| `WARMC_JOBS` | compiler | same as `-j` |
| `WARMC_TIME` | compiler | print phase timings |
| `WARMC_GC_HEAP_KB` | program | GC heap reservation (default 1 GiB) |
| `WARMC_GC_MIN_KB`, `WARMC_GC_GROWTH` | program | collection trigger: minimum size, and growth as a percentage of live data |
| `WARMC_GC_CONC` | program | `0` disables the background collector thread |
| `WARMC_GC_STATS` | program | print a GC report at exit |
| `WARMC_GC_VERIFY`, `WARMC_GC_STRESS` | program | heap verification after each cycle; collect on every allocation |
| `WARMC_RANDOM_SEED` | program | make WASI preview 1 `random_get` deterministic |

Many more `WARMC_*` variables exist for debugging individual passes, the
register allocator and the linker. Search for `getenv` in `src/`.

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
| memory64 and table64 | ✅ (memory capped at 65536 pages) |
| Multi-memory | ✅ |
| Extended constant expressions | ✅ |
| Deterministic profile | ✅ `--profile=deterministic` |
| Threads, shared memory, atomics | ❌ rejected at parse time |
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
rematerialises constants, and it chooses between callee-saved registers and
caller-saved registers with save and restore around calls, whichever is
cheaper. Functions compile in parallel and are laid out deterministically.

**Linker** (`src/link.c`, `pe.c`, `elf.c`, `macho.c`, `coff.c`):
- Linux: a static, non-PIE ELF with no libc.
- macOS: a PIE that imports libSystem through a GOT and is signed ad hoc
  in-process.
- Windows: a PE32+ whose DLL imports come from a curated table, so no SDK is
  needed.
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

`test/baseline.json` records warmc against wasmtime 50.0.0 on the author's
Apple silicon machine, at `-O2`, with each corpus's producer version beside
it (TinyGo 0.43, Go 1.27.3, rustc nightly 1.101, Zig 0.17, AssemblyScript
0.28.20, Kotlin 2.4.21, wasi-sdk 34 with clang 23.1, GHC 9.14). Each figure is the
geometric mean of warmc time ÷ wasmtime time, so lower is better. Each
engine's empty-program time is subtracted first, so the ratios compare
steady-state generated code, not start-up or compile time.

| Corpus | Source language | Kernels | warmc / wasmtime | Range | Median peak RSS (warmc / wasmtime) |
|---|---|---:|---:|---|---|
| `gobench` | TinyGo | 26 | 0.61 | 0.07 – 1.06 | 1.9 / 8.9 MB |
| `stdgo` | Go | 34 | 0.65 | 0.06 – 1.05 | 7.1 / 27.2 MB |
| `vecbench` | TinyGo (vectorisable loops) | 30 | 0.15 | 0.04 – 0.94 | 1.9 / 8.9 MB |
| `asbench` | AssemblyScript | 25 | 0.64 | 0.19 – 0.99 | 1.9 / 8.7 MB |
| `rsbench` | Rust | 28 | 0.71 | 0.34 – 1.00 | 1.9 / 9.1 MB |
| `zigbench` | Zig | 9 | 0.70 | 0.41 – 1.00 | 1.9 / 8.7 MB |
| `ktbench` | Kotlin (Wasm GC) | 12 | 0.09 | 0.004 – 0.48 | 3.4 / 20.6 MB |
| `cbench` | C | 14 | 0.84 | 0.32 – 1.05 | 2.0 / 8.9 MB |
| `cppbench` | C++ | 14 | 0.69 | 0.23 – 1.01 | 2.5 / 9.7 MB |
| `hsbench` | Haskell | 15 | 0.65 | 0.40 – 0.89 | 10.2 / 32.3 MB |

Over all 207 kernels the geometric mean is **0.48**, and 0.45 without
`stdgo`. Eight kernels are slower than wasmtime, all within 6%, and five of
them within 1.5%. Of the other three, `stdgo/memcpy` spends its time in the
platform's memmove under both engines, `cbench/nbody`'s pair loop compiles to
nearly the same instructions in both, and `gobench/bitops` pays a register
move after each population count that wasmtime does without.

`stdgo` is Go built by Go's own compiler, which is not LLVM. It compiles
every function to a loop around a `br_table` over the function's resume
points, so that a goroutine switch can unwind the wasm stack and re-enter the
function where it left off, and it sends each jump inside the function -- a
loop's back edge among them -- through that dispatch with a constant index.
warmc threads those jumps to their targets (`thread`), gives the dispatch a
cold copy of the loops it can enter in the middle so that the originals have
one way in again (`nodesplit`), and keeps Go's stack pointer in a register
between calls (`gssa`). With the loops back, the vectoriser takes Go's
64-bit address arithmetic too: `stdgo/strsum` runs at 0.06× and `stdgo/sum`
at 0.15×, where they were 1.31× and 0.98×.

Re-run the comparison on your own machine before relying on these numbers:

```sh
python3 test/gobench.py --corpus rsbench --rounds 5     # one corpus
make bench-compare                                      # all corpora vs the recorded baseline
```

---

## Testing

```sh
make check          # the full differential test suite (rows run in parallel; logs in build/checklogs)
make check-encoder  # AArch64 and x64 encoders against llvm-mc
make check-spec     # the official WebAssembly testsuite, at three optimisation settings
make conformance    # write CONFORMANCE.md: spec results grouped by proposal
make fuzz           # random modules vs wasm-interp (FUZZ_SEED, FUZZ_COUNT)
make gc-jitter      # GC schedule sweep with randomised collector delays
make check-linux    # run the TinyGo WASI corpus and GC tests as real Linux binaries (podman/docker arm64)
```

Most of the suite is **differential**. warmc's output is compared with:
- wabt's `wasm-interp`, for the hand-written cases in `test/cases*` (at every
  `-O` level and with individual passes turned off)
- wasmtime, for WASI preview 1 and 0.2 programs (stdout, stderr, exit code and
  the resulting directory tree)
- wasmtime, for the benchmark corpora in Go, Rust, Zig, AssemblyScript,
  Kotlin, C, C++ and Haskell
- `wasm-tools`, for component, WIT and WAT handling
- the Component Model's reference `definitions.py`, for canonical-ABI layout

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
  parse.c validate.c wat.c opcodes.c      front end
  component.c canon.c clink.c wit.c witgen.c   Component Model and WIT
  ir.c ir_build.c opt.c inline.c          SSA IR and optimiser
  isel*.c isel_*.inc regalloc.inc         instruction selection and register allocation
  a64.c x64.c                             machine-code encoders
  codegen.c compile.c module_data.c       per-function emission, parallel driver, module descriptor
  elf.c macho.c coff.c pe.c link.c        object writers and linker
rt/             the runtime linked into every program
  rt.c            instantiation, traps, GC, exceptions, CLI harness
  start.S start_x64.S                     entry, invoke, throw and GC trampolines
  wasi.c wasifs.c wasi.wat                WASI preview 1 host
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
