# warmc

**warmc** is an ahead-of-time compiler that turns WebAssembly into standalone
native executables, or into shared libraries that other languages call. It
needs no JIT, interpreter or VM process at run time.

You give it a `.wasm` or `.wat` file and it produces a Linux, macOS or Windows
binary. That binary is linked against a small freestanding runtime, which
provides traps, a concurrent garbage collector for Wasm GC and exception
handling. With `--wasi` it also links a sandboxed WASI preview 1 host, or a
WASI 0.2 one for a component (on Linux and macOS).

warmc is a single C11 program with no LLVM or other compiler-library
dependency. Everything between the WebAssembly bytes and the finished
executable is written from scratch: the text-format assembler, the validator,
an SSA optimiser, instruction selectors and register allocators for AArch64
and x86-64, ELF, Mach-O and COFF object writers, and a linker.

```console
$ build/warmc --target macos --rt build/warmrt-macos.a --wasi -o hello example/tinygo/main.wasm
$ ./hello --dir=.
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
  three, using the runtime archives that `make` builds for every target
  (WASI 0.2 programs only for Linux and macOS).
  With `--shared`, each target gets a shared library instead: a `.so`, a
  `.dylib` or a `.dll`.

- **Self-contained toolchain.** warmc links executables and shared libraries
  itself. It reads `.a` archives and ELF, Mach-O and COFF objects,
  dead-strips unused sections, and resolves Windows DLL imports from a
  built-in table. It needs no `ld`, SDK import libraries or `lld` for normal
  use.
- **WebAssembly 3.0.** This covers the 1.0 and 2.0 core (2.0 brings
  multi-value, bulk memory, reference types and **SIMD**) plus typed function
  references, **GC** (structs, arrays, i31, casts), **relaxed SIMD**, **tail
  calls**, **exception handling** (`try_table` / `exnref`), **memory64** and
  table64, **multi-memory**, and extended constant expressions. See
  [WebAssembly feature support](#webassembly-feature-support).
- **Passes the official spec testsuite.** The 258 core `.wast` files of the
  pinned upstream testsuite (its top level; its `proposals/`, `legacy/` and
  `custom/` directories are not run) pass with zero failures: 6,925
  validation and 60,730 execution checks, counting each module that must
  compile and each instantiation as well as the assertions, at `-O1`, at
  `-O2`, and at `-O2 --profile=deterministic`. The text format is checked
  through warmc's own text front end: the quoted modules, malformed ones
  among them, go to it directly, and the 5,118 modules the files write as
  text are assembled by it to the bytes wasm-tools makes of them, except
  for two where wasm-tools gives a function a type that is not final and
  warmc follows the spec.
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
  - rewrites of what particular producers emit: GHC's trampoline becomes tail
    calls, and Go's stack pointer travels in registers
  - at `-O3`, also fused multiply-adds and the removal of stores into C
    frames that nothing reads again, which wasm's semantics do not allow

  Functions compile in parallel and are laid out in a deterministic order.
- **Fast code.** Across 364 benchmark kernels compiled from Go, TinyGo, Rust,
  Zig, AssemblyScript, Kotlin, Java, C, C++ and Haskell, warmc's steady-state
  code takes 0.45× wasmtime's time (geometric mean), with much lower peak
  memory. Against the same programs compiled natively (at `-O2` or the
  nearest level each toolchain has), over the 300 kernels with a native
  ratio, it takes 1.02× the time. See [Performance](#performance).
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
- **Runs exports directly.** Built without `--wasi`, a program is a harness
  that calls any export by name from the command line, which is handy for
  testing and timing a module.
- **Embeddable.** Compile with `-c` to get a relocatable object that exposes
  one module descriptor. A small C API instantiates modules, wires them
  together, binds host functions and calls exports.
- **Shared libraries.** With `--shared`, each exported function whose
  parameters and results are `i32`, `i64`, `f32` or `f64` becomes a C
  function, declared in a generated header. C, C++, Python's `ctypes`, Java's
  foreign function API or any other C FFI can call it.
- **Custom sections.** The module's custom sections go into the output, in a
  section of their own that tools can read, and the runtime hands them out by
  name.

---

## Building

### Prerequisites

| Tool | Why |
|---|---|
| **clang** with the AArch64 and X86 backends (a recent LLVM; Homebrew's works) | builds `warmc` and cross-compiles the three runtimes |
| **llvm-ar** | archives the runtimes (Linux archives fall back to `ar`; the macOS and Windows ones need `llvm-ar`, which Xcode does not have) |
| **lld** | links the ThinLTO build, whose objects GNU ld cannot read (the Makefile asks for it on Linux; MSYS2's CLANG64 clang uses it by default) |
| GNU **make** | the build |

Homebrew's LLVM is keg-only: put `$(brew --prefix llvm)/bin` on `PATH`, or
set `CC` and `XCC` to its `clang`.

Supported build hosts, the three CI builds and tests on:
- macOS on Apple silicon
- Linux on AArch64
- Windows x86-64 under an MSYS2 shell (CI uses CLANG64)

The Makefile tunes the compiler for a particular CPU: `-mcpu=apple-m4` on
macOS and `-march=x86-64-v3` (AVX2, BMI2) on Windows. On an older Mac, or a
Windows machine without AVX2, build it with `make HOST_TUNE=`. On other
hosts, such as Linux x86-64, warmc builds and cross-compiles, but CI does
not cover them; the test suite then runs AArch64 output under
`qemu-aarch64`. An Intel Mac takes the Linux path of the Makefile and is not
supported.

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
| `build/warmwasip2.a`, `warmwasip2-macos.a` | WASI 0.2 hosts (there is none for Windows) |
| `build/warmgcth*.a` | the runtime plus GC thread self-tests, used by the test suite |
| `build/a64_test` | the AArch64 encoder's self-test (`make check-encoder` runs it with the x86-64 one) |

`BUILD=DIR` builds into another directory. `make clean` removes `build/`,
including the pinned dependencies cached there (the spec testsuite,
`wasm-tools`, wasi-sdk, TeaVM and the Kotlin stdlib), which the targets that
need them fetch again. `make asan` and `make tsan` build sanitiser versions of the
compiler, `build/warmc-asan` and `build/warmc-tsan`.

---

## Quick start

```sh
make -j

# Linking needs a runtime archive for the target: pass --rt, or set WARMC_RT once.
export WARMC_RT=$PWD/build/warmrt-macos.a

# A WASI program (here, a hand-written one from the test suite)
build/warmc --target macos --wasi -o hello test/wasi/hello.wat
./hello
```

On Linux, set `WARMC_RT=$PWD/build/warmrt.a` and leave out `--target macos`.

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

The runtime archive has to be the one for the target: warmc does not pick it
from `--target`, and linking another fails (`link: wasi.o is not a 64-bit
Mach-O object`). `WARMC_RT` sets it once.

`-o FILE` names the output. `-c` stops after writing a relocatable object
(`out.o` unless you pass `-o`). `--shared` builds a shared library, by default
`libNAME.so`, `libNAME.dylib` or `NAME.dll` for an input named `NAME.wasm` or
`NAME.wat` (see [Shared libraries](#shared-libraries)).

When you pass `--wasi`, warmc finds the matching host archive next to the
runtime by name: `warmrt-macos.a` becomes `warmwasi-macos.a` for a core module
and `warmwasip2-macos.a` for a component. A runtime whose name does not start
with `warmrt` gives nothing to derive it from. Use `--wasi=PATH`, or
`WARMC_WASI`, to choose a different host archive. There is no WASI 0.2 host
for Windows.

### Running exports directly: the CLI harness

If you compile a module **without** `--wasi`, the executable becomes a small
harness that calls any export by name. (A module that imports
`wasi_snapshot_preview1` needs `--wasi`, or `-c` to link a host of your own;
warmc refuses to build it as a harness.)

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
  `_start`, and prints its result; the exit status is 0 whatever it returns.
  If the module has neither, it lists the exports.
- **Integer arguments** are decimal or `0x` hex, with an optional sign.
  **Float arguments are raw IEEE-754 bit patterns**. For example, 3.0 as an
  `f64` is `0x4008000000000000`.
- **Results** print as `type:value`. Integers are signed decimal
  (`i32:-1`). Floats and `v128` values print as hex bit patterns
  (`f64:0x4014000000000000`), and references as `ref:` and 16 hex digits.
  Multiple results share one line; a call with no results prints an empty
  one.
- You can chain several calls with `--`. `--catch` reports a trap and carries
  on with the next call; the exit status is still 1 if any call trapped.
- `--bench N` times the export. It warms up first (at least 3 calls and
  100 ms), then takes up to N samples, at most 512, each a batch of calls
  lasting about 3 ms. It stops early once it has 5 samples and has spent
  40 ms. It prints `bench: NAME n=… min_ns=… med_ns=… max_ns=… mean_ns=…`,
  per call, to stderr. The result still goes to stdout. In a `--` chain, the
  later calls warm up for 2 ms only. `--catch` does not apply to timed calls:
  a trap under `--bench` ends the program.
- **Exit codes:**
  - `0` success
  - `1` trap, printed to stderr as `trap: <spec message>`, or one of the
    runtime's own fatal errors, printed as `warmc-rt: …`
  - `2` unknown export, too few arguments (extra ones are ignored), a `v128`
    parameter, or more than 60 parameters past the registers (3 integer
    and 4 float on x64, 7 and 8 on arm64)
  - `4` unresolved import (the harness provides no imports), or an instance
    the runtime refuses to make, such as one with a memory or table past its
    limits, printed as `warmc-rt: cannot instantiate: …`
  - `139` a fault outside linear memory

`--no-export-names` removes the export-name table from the binary, except for
`_start` and `main`. The harness still runs those with no arguments, but it
cannot call an export by name or list the exports.

### Several modules in one program

Given several core modules, warmc makes one program holding them all. Each
module is compiled exactly as it would be alone: the code is the same, the
symbols take a prefix of the module's own, and a function's name in the
symbol table is `MODULE.FUNCTION`. The program is asked for a module by its
file's name, without the directory and the last extension. The harness takes
that name as its first argument; with `--wasi`, the host takes
`--module NAME`, an option that exists only in a program of several modules.
Only the module named becomes an instance.

```console
$ build/warmc --target macos --rt build/warmrt-macos.a -o both fib.wat sum.wat
$ ./both                      # lists the modules
modules:
  fib
  sum
$ ./both fib fib 40
i64:102334155
$ build/warmc --target macos --rt build/warmrt-macos.a --wasi -o tools a.wasm b.wasm
$ ./tools --module b ARGS...
```

The test suites use this to link hundreds of modules into a few programs:
macOS checks a new program on its first run, which costs about 150 ms.

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
| Java | [TeaVM](https://teavm.org)'s `wasm-gc` target, then `test/teavm/wasi.py` (see [Java through TeaVM](#java-through-teavm)) |

Then compile it:

```sh
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
```

The resulting program reads its own sandbox flags anywhere before a `--`. Every
other argument, an unknown flag included, becomes the guest's `argv`:

| Flag | Effect |
|---|---|
| `--dir=HOST[::GUEST]` | preopen a host directory, under the guest path `GUEST`, or `HOST` as written |
| `--env NAME=VALUE` | pass one environment variable |
| `--inherit-env` | pass the whole host environment instead, ignoring every `--env` (nothing is inherited by default) |
| `--module NAME` | in a program of several modules, the one to run (see [Several modules in one program](#several-modules-in-one-program)); a program of one passes it to the guest |
| `--tcplisten=[ADDR:]PORT` | hand the guest a listening socket, at a dotted IPv4 address (default 127.0.0.1), non-blocking. Its descriptor comes after the preopened directories': 3 when there are none. Without it, the guest has no network access. Not on Windows. |

Each flag also takes its value after a space or an `=`: `--dir HOST`,
`--env=NAME=VALUE`, `--tcplisten PORT`.

```sh
./app --dir=./data::/data --env MODE=fast input.txt
```

Sandbox behaviour:
- Paths are resolved one component at a time.
- Absolute paths, and `..` that climbs above a preopen, are refused.
- Symlinks are followed by hand: at most 8, and never to an absolute target.
- A path that ends in a slash names a directory, as under wasmtime: making a
  directory or renaming by it acts on the entry itself, which must be one;
  creating a file by it is refused, with the host's errno (`EISDIR` on Linux
  and Windows, `EINVAL` on macOS); anything else acts on what the name
  leads into.
- A name is taken as it is spelled: on Windows too, `x.` and `x ` are names of
  their own and not `x`, and paths of any length up to 4096 bytes reach
  Win32 whole (by its `\\?\` prefix).
- On Windows the host gives Linux's answers, where wasmtime's Windows host has
  rules of its own for a path that ends in a slash (`EACCES` for a directory
  removed by one, a symlink to nothing refused).
- Rights are enforced per descriptor.
- The flags a guest sets on stdin, stdout and stderr (`fd_fdstat_set_flags`)
  are its own. The host carries out non-blocking and append itself rather
  than setting them on the files, which are the parent's too and keep their
  flags after the program: Go's runtime makes all three non-blocking as it
  starts, and a terminal would stay so. A sync flag is refused on them
  (`ENOTSUP`).

The exit status is the guest's `proc_exit` code. A code of 126 or more, which
the system would cut to 8 bits or a shell takes for its own, is a trap, as
under wasmtime. The host reserves a few codes of its own:

| Code | Meaning |
|---|---|
| 70 | the host's own import stub failed to instantiate |
| 71 | the module failed to link or to instantiate, a trap in its start function or segment initialisation included |
| 72 | no `_start` |
| 73 | a trap in `_start`, printed to stderr, a `proc_exit` code of 126 or more included |
| 74 | a `--dir` that cannot be opened, a `--tcplisten` that cannot listen, or no memory for the flags |

#### An HTTP server in Go

wasip1 has no call that opens a socket, so a server takes the one
`--tcplisten` hands it: the first descriptor from 3 up that is a stream
socket. Go's runtime waits for connections, and for each one's reads and
writes, in `poll_oneoff`, so one thread serves them all:

```go
package main

import (
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"syscall"
)

// listener is the socket --tcplisten hands the program
func listener() (net.Listener, error) {
	var st syscall.Stat_t
	for fd := 3; syscall.Fstat(fd, &st) == nil; fd++ {
		if st.Filetype == syscall.FILETYPE_SOCKET_STREAM {
			return net.FileListener(os.NewFile(uintptr(fd), "listener"))
		}
	}
	return nil, errors.New("no listening socket: run with --tcplisten")
}

func main() {
	l, err := listener()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "hello from %s\n", r.URL.Path)
	})
	http.Serve(l, nil)
}
```

```sh
GOOS=wasip1 GOARCH=wasm go build -o server.wasm .
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o server server.wasm
./server --tcplisten=8080 --dir=./static::/static
```

`net.Listen` and `http.ListenAndServe` build for wasip1 too, but Go
implements them there with a network inside the program, which nothing
outside it can connect to. Nor can the program connect out (`http.Get`).
preview1 has no call that tells the program its listener's address or a
connection's peer, so `Addr()` and `RemoteAddr` do not say them.
`test/gohttp/main.go` is a fuller server, and `test/run_gohttp.py` what is
checked of it.

#### Linux

On Linux a WASI program is a static executable with no libc, like every
other Linux output: `rt/linuxposix.c` answers the WASI hosts' POSIX calls
over raw system calls. It does so under names of its own (`warmc_lx_*`), so
an embedder can still link the `-c` object into a C program that uses glibc:
`gcc -nostartfiles -no-pie -u warmc_driver_hook app.o build/warmwasi.a
build/warmrt.a`, as `test/run_gowasi_linux.sh` does.

### Java through TeaVM

TeaVM 0.16 compiles JVM bytecode to Wasm GC, but has no WASI target: its
`wasm-gc` module is written for TeaVM's JavaScript loader, which supplies the
module's memory, `System.out` a UTF-16 code unit at a time, the clock,
`Math`'s transcendental functions, and the JS strings and objects its JSO
layer makes at start-up to export `main`. `test/teavm/wasi.py` defines every
one of those inside the module, over WASI, and adds a `_start` that runs
`main` with no arguments. TeaVM's own code stays as TeaVM wrote it: the
adapter replaces the module's import lines with functions of the same names
and reassembles it, so every index keeps referring to what it did. Of the
exports it keeps `_start`, `memory` and the program's own: TeaVM's exports
for its loader -- strings and exceptions handed to JavaScript, the `main`
function object -- take and return references, and with none crossing the
module's boundary warmc decides its casts inline (`-fsealed`).
`make teavm` fetches the pinned compiler and applies `test/teavm/patches`
(below), and `test/teavm/TeaVMBuild.java` drives it, as TeaVM publishes no
command-line jar for this release:

```sh
make teavm
T=build/tools/teavm-0.16.0
javac --release 17 -cp "$T/*" -d build/teavm-driver test/teavm/TeaVMBuild.java
# src/ holds Main.java; TeaVM writes out/main.wasm
java -cp "$T/patched:$T/*:build/teavm-driver" warmc.teavm.TeaVMBuild --teavm $T \
  --support test/teavm/FdLibm.java src=out
python3 test/teavm/wasi.py out/main.wasm app.wasm
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
```

- `Math.sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log` and
  `pow` call `test/teavm/FdLibm.java`, a port of fdlibm (what `StrictMath`
  computes) that `--support` has TeaVM compile into the module; `sqrt`,
  `floor`, `ceil` and `abs` are single instructions.
- Output is written a line at a time, as `console.log` writes it, in UTF-8,
  with a lone surrogate as U+FFFD.
- A direct `ByteBuffer` stays allocated: TeaVM frees one through
  JavaScript's `FinalizationRegistry` once it is garbage, and the registry
  the adapter gives the class library never calls back.
- An exception `main` lets out prints `Uncaught Java exception: MESSAGE` and
  exits 1. A host call with no meaning outside JavaScript (JSO, the DOM,
  timers) prints its name and exits 69.
- TeaVM's semantics are TeaVM's: without `--strict` (the driver's flag), a
  null dereference, an index out of bounds or an integer division by zero is
  a wasm trap rather than a Java exception; with it, the exception. A
  negative array size, an `ArrayStoreException` and a stack overflow are
  traps either way, and the class library has no `System.exit`.

`test/teavm/patches` fix bugs of TeaVM 0.16.0, each a unified diff of the
sources of the artifact it names. `test/teavm/patch.py` applies them, refusing
a hunk whose context is not exactly where it says, and compiles the patched
files into `$T/patched`, which goes ahead of the jars on the JVM's class path
and on the one TeaVM reads a program's class library from (the driver checks
both):
- `name-section-order`: each map of the name section in index order, which
  is not the order TeaVM keeps functions and globals in, as imported ones
  come first. Out of order, a map is not one as the name section's appendix
  has it: wasm-tools names nothing from its first entry out of order, and
  warmc ignores the section.
- `integer-division`: `MIN_VALUE / -1` is `MIN_VALUE`, where `div_s` traps,
  and with `--strict` a zero divisor, of `/`, `%`, `divideUnsigned` or
  `remainderUnsigned`, throws `ArithmeticException`, which TeaVM's
  dependency analysis now knows a division can throw.
- `math-rint-round-ulp`: `Math.rint` rounds half to even, `Math.round` is
  `floor(x + 1/2)` without rounding the sum, and `Math.ulp(double)` masks
  the exponent it means to; the fixes TeaVM made after 0.16.0.
- `array-simple-name`: `getSimpleName` and `getName` of an array class, which
  need the names of its item type.
- `console-bytes`: `System.out.write` and `flush`, which threw: a null action
  called, a decoder left at its end, and an offset never advanced.
- `byte-order`: `ByteOrder.nativeOrder()` is little-endian, as linear memory
  is, without asking JavaScript.
- `direct-buffer-zero`: `ByteBuffer.allocateDirect`'s bytes are zero, as Java
  has a new buffer's; they were what TeaVM's heap had left there.

### LWJGL 3 programs on macOS

`lwjgl/` builds LWJGL 3 programs into native macOS executables through TeaVM
and warmc, on LWJGL's own jars -- `lwjgl`, `lwjgl-glfw` and `lwjgl-opengl`
3.4.3 -- with GLFW and OpenGL called directly and no JNI:

```sh
make all teavm glfw lwjgl
python3 lwjgl/build.py --main HelloWorld lwjgl/demo/hello -o hello
./hello
```

`lwjgl/demo/hello/HelloWorld.java` is LWJGL's own HelloWorld, a window
cleared to blue. `lwjgl/build.py` compiles the program against LWJGL's jars
(`make lwjgl` fetches them from Maven Central), has TeaVM make a module of it,
LWJGL's classes and `lwjgl/platform`, the TeaVM adapter a WASI command of
that, and warmc a native executable, linked with GLFW (`make glfw` fetches the
3.5.1 release's static library), the system's libffi and the frameworks GLFW
and OpenGL need. `--c FILE.c` compiles and links C of the program's own.

LWJGL's classes are its jars' own but for the few that `lwjgl/platform`
replaces, and a TeaVM class transformer, `lwjgl/teavm/LwjglTransformer.java`,
changes them as TeaVM reads them:

- Each OpenGL function LWJGL declares native (`GL11C.glClear`,
  `GL15C.nglBufferData`) is an import of module `lwjgl` named after the
  function, which `build.py` passes to warmc as a `--native-module` (see
  [Calling C functions](#calling-c-functions)): a direct call into macOS's
  OpenGL framework, its arguments where C's convention puts them. So is each
  C library function (`LibCStdlib.nmalloc`), and each other native is a C
  function of `lwjgl/native/lwjgl.c`'s, `lwjgl_CLASS_METHOD`.
- LWJGL calls GLFW's functions, and OpenGL's overloads that take Java
  arrays, through `org.lwjgl.system.JNI` at an address it looked up by name,
  `JNI.invokeV(GLFW.Functions.PollEvents)`. `build.py` generates `JNI`, each
  of LWJGL's 1779 methods a call of the function at the address
  (`warmc_call`), and the transformer makes a call whose address is a
  `GLCapabilities` field, which is named after the function, or a `Functions`
  field, whose initializer names it, a direct call of the function:
  `bl _glfwPollEvents`. A `Functions` field's read goes with the address, and
  the `Functions` class, which looks up every function by name, is not
  initialized for it. A call through any other address is an indirect call
  of it: one a program makes through `JNI` itself, `GL.createCapabilities`'s
  start-up queries, `glfwFreeCallbacks`'s loop, and an optional function's,
  whose address LWJGL checks before the call.
- A Java array handed to C is copied onto the thread's `MemoryStack` and back
  after the call.
- `MemoryUtil`'s backend and allocator are `lwjgl/platform`'s. An address is a
  native one, as in LWJGL, and C takes it as it is. What Java allocates --
  `MemoryStack`, `memAlloc`, `BufferUtils` -- is in the module's linear
  memory, at `warmc_memory_base()` plus its offset there, and Java reads and
  writes it with wasm's own loads and stores. C's memory (a string GLFW hands
  back, a `GLFWVidMode`, a callback's arguments) is read and written through
  `memcpy`.
- A callback is a closure of the system's libffi whose handler calls the
  module's export `lwjgl_upcall`, which calls the Java callback. An exception
  the callback throws is kept and thrown when the `JNI` method whose call ran
  the callback returns, as JNI throws it.
- A library is the program itself: LWJGL's `SharedLibrary` finds a function
  with `dlsym` among the program's symbols, all of GLFW's among them.
- `GLCapabilities` looks up only the OpenGL functions macOS's framework
  exports, as the SDK's stub of it lists them, which are all `dlsym` can
  find there. The transformer folds a check of a version or extension the
  framework has none of to `false` (OpenGL 4.2 and later, most extensions:
  1037 of LWJGL's 2267 functions), and with it the check's arrays of names,
  its flags and, when nothing else is left of it, the check itself, which
  takes 0.7 MB off a program. The capabilities are the same, field for field;
  `--every-gl-function` looks each function up as LWJGL does.
- Of what TeaVM lacks, `MethodHandles.lookup()`, which a callback's
  descriptor keeps only for LWJGL's FFM backend, is null; `System::load` is
  not called; and `Runtime.addShutdownHook`'s hooks run when `main` returns.

What differs from LWJGL on the JVM:

- An OpenGL function the program can reach that macOS's framework lacks
  (OpenGL 4.2 and later) is a link error, not a capability it checks for.
- With `Configuration.DEBUG_FUNCTIONS` or `DEBUG`, LWJGL reports no OpenGL
  function the framework lacks as missing, as it never looks one up.
- GLFW is the 3.5.1 release, which has none of the IME functions LWJGL binds
  as optional: calling one throws LWJGL's `NullPointerException`, as on the
  JVM with that GLFW.
- A buffer over C's memory (`memByteBuffer` of an address C owns) is refused
  with `UnsupportedOperationException`; `memGetInt`, `memCopy` and the rest
  work on that memory.
- An exception a callback run from an OpenGL function throws, as a debug
  message callback's, comes out of the next `JNI` call rather than that
  function's.
- What `BufferUtils` allocates is never freed (see the direct buffers above).
- The bindings are LWJGL's core, GLFW and OpenGL's alone, on Apple silicon.

### WASI 0.2 command components

If the input is a component that exports `wasi:cli/run@0.2.x#run`, `--wasi`
links the WASI 0.2 host:

```sh
tinygo build -target=wasip2 -o app.wasm main.go     # or: cargo build --target wasm32-wasip2
build/warmc --target macos --rt build/warmrt-macos.a --wasi -o app app.wasm
./app --dir=. --env FOO=bar arg1 arg2
```

The host implements 58 functions of `wasi:cli` (environment, exit, stdio,
terminal), `wasi:clocks`, `wasi:filesystem` (types and preopens), `wasi:io`
(error, poll and streams) and `wasi:random` (`rt/wasip2.c` lists them). It
does not implement all of each interface: no `wasi:random/insecure`, no
clock `resolution`, and no descriptor `get-type`, `set-size`, `set-times`,
`advise` or `sync`, for example. It accepts any `@0.2.x` interface version.
The program takes `--dir`, `--env` and `--inherit-env` as a preview 1 one
does, but not `--tcplisten`, which goes to the guest's arguments. There is no
WASI 0.2 host for Windows. warmc refuses to build a command component
without `--wasi` (or `-c`).

The exit status is `0` or `1`, from `run`'s result, unless the component
calls `wasi:cli/exit`'s `exit-with-code` with another (Rust's
`std::process::exit` does). As for preview 1, the host exits with 71 when a
core instance fails to link or to instantiate (a trap in its start function
included), 72 when there is no `run` export, 73 on a trap in `run`, and 74
for a `--dir` that cannot be opened or no memory for the flags. If the
component calls a
`wasi:*` function this host does not implement, the program traps when the
call happens and names the function. It is not refused at link time.

### Embedding compiled modules in C

For a core module, `-c` produces an object that exports a single symbol, the
module descriptor `warmc_module`. With `--symbol-prefix=P`, the descriptor is
named `P_module`, so several modules can live in one program. Given several
core modules, `-c --symbol-prefix=P` makes one object of them, the i-th
(counting from 1) as it would be alone with `--symbol-prefix=Pi`, with no
table of modules. A component's
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

Here `guest.wat` imports one function, which `binds` gives `print_i64`, and
exports `fib`. The value `drive` returns is the program's exit status.

Build it on macOS like this. Host code is compiled freestanding, like the
runtime, and uses `warmc_rt_write` and friends for I/O.

```sh
build/warmc --target macos -c --symbol-prefix=guest -o guest.o guest.wat
clang --target=arm64-apple-macos11 -DWARMC_DARWIN=1 -O2 -ffreestanding \
      -fno-stack-protector -fno-builtin -nostdlib -I rt -c -o host.o host.c
ld -arch arm64 -platform_version macos 11.0 11.0 -o app host.o guest.o \
   build/warmrt-macos.a -lSystem -L"$(xcrun --show-sdk-path)/usr/lib"
```

On Linux, build the guest with `--target linux`, compile the host with
`--target=aarch64-linux-gnu` and link with
`ld.lld -static -e _start -o app host.o guest.o build/warmrt.a`. The Windows
recipe is in `test/hostbuild.py`.

The rest of the API, in [`rt/warm_rt.h`](rt/warm_rt.h):

| Call | Purpose |
|---|---|
| `warmc_instantiate(md)`, `warmc_instantiate_catching(md, &err)` | instantiate a module, its imports resolved from the registered instances, and run its start function; instead of exiting, the `_catching` form returns NULL and stores the trap code in `err` |
| `warmc_instantiate_bound(md, binds)`, `warmc_instantiate_bound_catching(md, binds, &err)` | the same with one `WarmBinding { inst, name, code }` per import, of every kind, in import order: a non-NULL `code` binds a host function, which receives `inst` first; a NULL one imports export `name` of instance `inst`. The registry is not consulted. |
| `warmc_register(name, inst)`, `warmc_register_n(name, len, inst)` | make an instance's exports importable under module `name`, so modules can import one another. Imported memories, tables, globals and tags all work. |
| `warmc_find_export(inst, name)`, `warmc_find_export_n(inst, name, len)`, `warmc_export_func(inst, e)` | look an export up, and get the function an export names |
| `warmc_bind_import(inst, module, name, fn, self)` | rebind an already-linked function import to a host function, which then receives `self` first (`test/tinygo/driver.c` passes the guest, to reach its memory); returns how many imports it rebound |
| `warmc_invoke(...)`, `warmc_invoke_catching(...)` | call an export. Integer and reference args go in `iargs`: up to 7 on arm64 and 3 on x64; an `i32` is its slot's low half, the high half whatever it holds, so an `int32_t` may go in sign-extended. Floats go in `fargs[2*i]`: up to 8 on arm64 and 4 on x64, a `v128` in `fargs[2*i]` and `fargs[2*i+1]`. Results come back in `ret[WARMC_RET_X0]` (integer), `ret[WARMC_RET_V0]` (float; a `v128` in it and the next word) or `ret[WARMC_RET_MULTI(i)]` (multi-value). |
| `warmc_invoke_s`, `warmc_invoke_catching_s` | for functions with more parameters than the registers above carry, which take the rest from an image of their stack slots, in parameter order: on x64 8 bytes per scalar and an aligned 16 per `v128`, at most `WARMC_X64_STACK_BYTES`; on arm64 16 bytes per parameter (`WARMC_A64_STACK_SLOT`), a scalar in the low 8, any number of them. |
| `warmc_global_cell(inst, idx)`, `inst->mem_base` / `mem_size` | access globals and linear memory 0 |
| `warmc_gc_collect()`, `warmc_gc_hold()` / `warmc_gc_release()` | drive the collector; register C-side roots |
| `warmc_custom_section(md, name, index, &data, &size)` | the module's custom sections named `name`: how many there are, and the `index`-th one's payload (see [Custom sections](#custom-sections)) |

After a trap that `warmc_invoke_catching` returns, the instance can be called
again. What its memory, tables and globals then hold is the module's own
only with `-fexact-trap` (see
[What a trap leaves behind](#what-a-trap-leaves-behind)).

An `externref` the host hands in is any 64-bit value with its low two bits
clear -- a pointer to a C object is one -- or 0 for null. A module that
turns it into an `anyref` (`any.convert_extern`) sees it as no object and no
`i31` of its own, and gets it back unchanged from `extern.convert_any`.

Limits: at most 4096 instances per process, and instances are never freed.
Every 32-bit memory that a module compiled with guard pages (`-O2` and `-O3`)
defines takes one of 1024 guarded address-space reservations, as does the
process's first 32-bit memory whatever the level, so at most 1024 such
memories can be made. A 64-bit memory takes none.

`test/link/driver.c` (multi-module linking) and `test/tinygo/driver.c`
(hand-written WASI imports) are complete examples.

### Calling C functions

`--native-module NAME` makes every function import of module `NAME` a call
of the C function of the import's name: no host binding, no thunk, the
arguments in the registers and stack slots the platform's C convention has
them in (Apple's packed stack arguments on macOS), and the C function found
by the system linker, which `--link` gives what it needs:

```sh
clang -O2 -c -o gl.o mygl.c
build/warmc --target macos --rt build/warmrt-macos.a --wasi \
  --native-module c --link "gl.o -framework OpenGL" -o app app.wasm
```

- A native import takes and returns `i32`, `i64`, `f32` and `f64` alone, at
  most one result, and can only be called: one exported, in a table, taken
  by `ref.func` or used as the start function is refused. A tail call of one
  is a call and a return.
- A pointer is an `i64` native address. `warmc_memory_base`, of type
  `() -> i64` in a native module, is not a C function but memory 0's base,
  the native address of offset 0, read from the register warmc keeps it in;
  a 32-bit memory's base does not move, as warmc reserves all it can grow to.
- `warmc_call`, or `warmc_call.` followed by anything, so that a module can
  import it at several types, is not a C function either: it calls the C
  function at the address its last argument, an `i64`, holds, the others
  that function's arguments, with a `blr` of the register the address is in.
- C calls back into the program through `warmc_wasi_instance`, the instance
  `--wasi`'s host made of it, with `warmc_find_export` and `warmc_invoke`. A
  collection waits while such a call runs, as it does in every nested call.
- Native imports leave the wasm sandbox: C reads and writes what it is
  handed, wherever it points. They are AArch64's alone, on Linux and macOS,
  link with the system linker, which they imply, and not into a `--shared`
  library.

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

`calc.h` declares the exports, a type for each import, and five functions of
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
size_t calc_custom_section(const char *name, size_t index, const uint8_t **data, size_t *size);
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
  failure, or an internal error above`). A trap while the instance is made,
  in the module's start function say, likewise fails every call with that
  trap. `calc_bind_import` returns 0, 1 for an import the module does not
  have, or 2 once the first call has been made.
- **Threads.** Any thread may call; the calls take turns on the one instance.
  An import may call back into the library.
- **Traps** do not unwind into the caller. The call returns 0, results
  returned through pointers are left as they were, and `calc_last_trap()`
  gives the calling thread's trap code (0 after a call that returned).
  `calc_trap_name` names it. A wasm exception that leaves the export is trap
  13, `uncaught exception`. The instance stays usable.
- **Several results** come back through pointers after the parameters.
- **Memory.** `calc_memory(&size)` returns linear memory 0 and its size, or
  NULL and 0 when the module has none or the instance could not be made. It
  makes the instance if no call has, so it counts as the first call. A call
  that grows the memory may move it.
- **Stack.** Recursion is limited by the calling thread's own stack. Running
  out traps as `call stack exhausted`.
- **Faults.** The first call installs the runtime's fault handlers (SIGSEGV
  and SIGBUS, or a vectored exception handler on Windows). Faults outside
  wasm code go on to the handler that was there before, so a JVM's handler, or
  the program's own, keeps working.
- **Options.** The first call reads the runtime's `WARMC_GC_*` variables from
  the environment, as a program does when it starts.
- **Custom sections.** `calc_custom_section(name, index, &data, &size)`
  returns how many of the module's custom sections are named `name`, as
  `WebAssembly.Module.customSections` does, and gives the `index`-th one's
  payload, or NULL and 0 when there is none. It needs no instance, and the
  payload lives as long as the library. Which sections there are is
  `--custom-sections`'s (see [Custom sections](#custom-sections)).

Names:
- `--symbol-prefix=P` sets the prefix of the library's own functions.
  Without it, the prefix is the output's name without a leading `lib` and
  without everything from its first `.`, made a C identifier: `my-calc.v2.wat`
  gives `my_calc`. `--header PATH` writes the header somewhere other than
  `PREFIX.h` beside the library.
- An export whose name is not a C identifier gets one, with `_` for each
  other character and before a leading digit: `add-one` becomes `add_one`,
  `1x` becomes `_1x`.
- A name that C or the runtime already uses (a C keyword, `main`, `_start`,
  or one starting `warmc_` or `__warmc`) gets the prefix: `int` becomes
  `calc_int`. The header gives the original name of every renamed export.
- Two exports that become the same C name, or an export named like one of
  the library's own functions, stop the build.
- An export that C cannot call gets no symbol, and warmc warns about it. That
  covers `v128` and reference types, more than 16 results, and more than 60
  parameters past the registers.

warmc refuses to build a library that imports a memory, table, global or tag,
or a function that C cannot stand in for: one returning more than one
result, or with a parameter or result C cannot call with, as for exports
above. On Windows an imported function may take at most three parameters.
`--shared` cannot be combined with `--wasi`, `--no-export-names`,
`--system-ld` or a component. `--shared -c` writes the object and the header
(and the `.def` on Windows) without linking.

| Target | Default output | Linking a program against it |
|---|---|---|
| macOS | `libNAME.dylib`, install name `@rpath/` and the output's file name | `-L DIR -lNAME -Wl,-rpath,DIR` |
| Linux | `libNAME.so`, with no dependencies (the runtime needs no libc), and the output's file name as its SONAME | `-L DIR -lNAME -Wl,-rpath,DIR` |
| Windows | `NAME.dll`, plus `PREFIX.def` beside it | an import library made from the `.def`: `llvm-dlltool -m i386:x86-64 -d PREFIX.def -l libNAME.dll.a`, or MSVC's `lib /def:PREFIX.def /machine:x64` |

`NAME` is the input's name without everything from its first `.`, and
`PREFIX` the library's prefix (above). A Windows DLL needs an
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

`test/shared/` has a fuller C program and the Python and Java tests;
`test/run_shared.sh` runs them all, and builds a C++ use of the header too.

### Custom sections

What warmc compiles carries the module's custom sections, in a read-only
section of their own: `.wasm.custom` in ELF, `__TEXT,__wasm_custom` in
Mach-O, `.wcustom` in COFF and PE. A tool finds them there
(`otool -s __TEXT __wasm_custom`, `llvm-objcopy --dump-section`), and the
runtime hands them out by name: `warmc_custom_section` for an embedder, and
`PREFIX_custom_section` from a library. By default every one is kept but the
debug information: the name section, whose function names the symbols
carry already, and DWARF's `.debug_*` sections. `--custom-sections=all`
keeps those too, and `--custom-sections=none` keeps none, so the output then
has no such section. For a component, the sections carried are its core
modules'; the component's own custom sections are not.

The section holds a table per module (a component has several), each
8-aligned and little-endian:

| Bytes | |
|---|---|
| 8 | `wcustom\0` |
| 4 | the table's size, these 16 bytes included |
| 4 | how many custom sections it lists, `n` |
| 16 × `n` | for each, in the module's order: the offset and length of its name, then of its payload, from the table's start |
| | the names and the payloads |

The module descriptor's `custom` field points at the module's table, or is
NULL when it carries none.

### Components and WIT

warmc parses almost any Component Model binary. It compiles a component to
native code when the whole instance graph can be resolved at compile time. In
practice that means one of two cases:

- **WASI 0.2 command components.** Every import is a `wasi:*@0.2.*` instance,
  and the component exports `wasi:cli/run@0.2.x#run`.
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
...
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
| `--validate-only` | parse and validate, then stop. Exit codes: `0` valid, `2` malformed, `3` invalid. A `.wit` file, or a directory as the input with or without it, is read and checked as WIT. Given several inputs, each one gets a verdict, a line on stdout apiece: the path, a tab, then `0 ok` or the exit code and message it would have given alone. The exit code is then `0`. |
| `--emit-wasm` | assemble a `.wat` to binary (`-o`, default `out.wasm`) and stop. Given several inputs, each is written next to itself, its last extension replaced by `.wasm`, and gets a verdict line as with `--validate-only`; `-o` is refused then |
| `--print-ir` | dump each function's IR, before and after optimisation, to stderr |
| `--stats` | print optimisation counters: folds, inlined calls, spills, `ldp`/`stp` pairs, vectorised loops, and so on |
| `--profile` | instrument the code to count executed instructions; prints `icount: N` at exit |
| `WARMC_TIME=1` | print per-phase compile and link timings, the peak memory reached by each compile phase, and the slowest functions to compile |

---

## Optimisation

| Level | What it does |
|---|---|
| `-O3` | everything `-O2` does, and the passes that change what a program can see: `fma` and `dead-frames` (below). Every optimisation warmc has, for code that does not depend on wasm's exact float rounding or on reading stack memory it never wrote |
| `-O2` (default) | the full pipeline: what `-O1` does, plus inlining, devirtualisation, unrolling, `gcpure`, and guard-page memory (which also enables the vectoriser, native-pointer caching and load combining) |
| `-O1` | the optimising pipeline without guard pages, inlining, devirtualisation, unrolling or `gcpure` |
| `-O0` | skips the IR optimiser. Register allocation, module reachability (`reach`), `prune`, `merge`, the `call_indirect` code tables (`ictab`) and constant pools (`cpool`) still run. |

You can switch individual passes with `-fPASS` / `-fno-PASS`. Flags apply in
command-line order, so put them **after** `-O`; each `-O` sets everything a
level decides, so the last one given is the level. Some passes need others
to run: `edge-tests` needs `vectorize`, `guard-pages` and `branch`;
`nodesplit` needs `thread`; `reinline` needs `inline`; `vectorize`, `memptr`
and `ldcomb` need `guard-pages`; `small-loop-align` needs `align`; and
`mulwide`, like the constant multiplies and divisions of `strength`, lives in
`fold`. The instruction names below are arm64's.

| Pass | What it does |
|---|---|
| `fold` | constant folding and algebraic simplification, including a constant reinterpreted between integer and float and two xor-shifts that cancel, a test of an `Ordering` -- Rust's `(a > b) - (a < b)`, alone or masked to its byte -- as the compare of `a` and `b` it stands for, a branch on the high half of a call's i64 result, Go's unwind flag once `go-sp` packs it, as a `tst` of the result, and an i64 put back together from its halves, `x & 0xffffffff00000000` or'd with `x`'s low half extended, as `x`; `x*c + y*c` as `(x + y)*c`, and `x*c - y*c` as `(x - y)*c`, an integer's or lane by lane, where each product is read there alone; the sign extension of an i64 that already is the sign extension of its low 32 bits -- a signed load, an extension, a constant in range, an and, or, xor or select of such, a block parameter every edge hands one, a loop's too -- as that i64, as Go's int32 kept in an i64 and extended again at each use is; the value a loop hands its parameter `x` on a back edge, `x op c1 op c2 ...` with each `c` a constant and `op` an add, multiply, and, or or xor, as `x op c`, the constants folded into one, also where the links are read elsewhere, which keep them, so that the next iteration waits on one instruction, not on each link -- a loop unrolled four times, each copy its counter plus one -- though not where nothing else reads `x`, which would then be live to the chain's end; a reinterpret undone, a store of reinterpreted bits, and an integer load read only as float bits, as the C ABI copies a struct's doubles, move the bits in the float registers |
| `dce` | dead-code elimination |
| `cse` | common-subexpression elimination over dominators |
| `licm` | loop-invariant code motion, including hoisting invariant stores and trap guards; a value is taken out of the innermost loop that reads it and no further, and a native address stays by the access it serves, which folds it |
| `branch` | branch folding: a `br_if` or `br_table` on a constant, a `br_if` whose two edges go to one place, and edges through empty blocks that only jump on |
| `edge-tests` | in loops, fold a test or branch that a dominating branch on the same value has already decided, such as an inlined bounds check behind a loop's own exit test |
| `fma` | off below `-O3`: a float multiply whose one reader is an add or a subtract becomes one fused multiply-add (`fmadd`, `fmla`; `vfmadd` on x86-64), rounded once. Not where the add is what a loop carries round and the multiply is not, as in a dot product, which runs a third slower fused, nor for a vector `a*b - c`. wasm rounds the multiply and the add each, so a result can differ in its last bit; `-ffma` is for code that does not depend on that, as native C compilers assume by default |
| `dead-frames` | off below `-O3`: assumes C's stack, where nothing reads a function's frame once it returns, and while it runs only the function and the callees it passes pointers into it; a store into the frame that nothing can read before the function returns, or before the frame an inlined callee took is popped, is removed, such as the copies the C ABI makes of struct arguments and results once inlining has forwarded them (`cbench/structval` takes 0.54× its `-O2` time at `-O3`). wasm keeps those stores, so a program that reads stack memory it never wrote can see other bytes. A module whose stack is not C's -- Go's, an asyncify one, or one where a function reads above the stack pointer it was entered with -- is left as it is, and a call that may reach a function scanning its callers' frames, as a collector does, reads the whole frame |
| `mulwide` | a 64-by-64 multiply that wasm spells in 32-bit halves -- `__multi3` from Rust, C and Zig, Go's `bits.Mul64` -- becomes one `mul` for the low half and `umulh`, or `smulh` for sign-extended operands, for the high |
| `ifconv` | if-conversion: a diamond or triangle whose arms are cheap and cannot trap becomes selects (`csel`), as do the leaves of the compare tree a small `br_table` becomes. (That tree, its default dropped where the index is provably in range, is made from `-O1` whatever the flags.) |
| `thread` | jump threading through a block that only switches on a parameter, such as the dispatch Go's compiler puts around every function, or only compares one with a constant, when an edge passes that parameter a constant; and through a block that only tests a value against constants -- a branch on it, on its compare with one, or on the value plus or minus one, or a `br_table` on it -- when the branches an edge took on its way there, up through blocks each entered from one edge, have decided the test: equal or not, unsigned and signed ranges, a `br_table`'s case or default, also for a parameter the edge passes a compare the way decides, and through a block that only passes its parameters on. Not into a loop's header. A parameter every edge passes one compare -- itself, or a constant its way shows it to be -- is that compare, made again. TinyGo's switch on strings (`gobench/strswitch`) tests in every case a length an earlier case's test has already fixed, and keeps those tests in the locals LLVM's wasm back end adds when it lays control flow out as nested blocks |
| `nodesplit` | copy the part of a loop that a switch outside it enters in the middle, so that the loop has one way in again and the loop passes can take it |
| `mem` | store-to-load forwarding, also of bytes stored as one type and loaded as another, and dead-store removal, also of a store of the value the slot already holds and of a `global.set` a later one in the block overwrites, using per-function effect summaries |
| `bce` | bounds-check elimination by value range and dominating checks, of memory accesses and GC array accesses |
| `strength` | strength reduction: induction variables, shifts, magic-number division; unsigned division by a loop-invariant divisor (32-bit, or 64-bit of operands that fit in 32 bits) through a reciprocal made before the loop; the remainder of a division just made as a multiply-subtract; a post-decrement loop test on the new count |
| `inline` | call-site-scored inlining (on from `-O2`) |
| `reassoc` | rebalance associative integer chains for latency, also through a wrap to 32 bits, and add the value a loop carries round last in its chain, so that an iteration waits on one op of it, in v128 lanes too; the products such a chain then adds among themselves, by one factor, are one product of their sum (`fold`'s rule): Zig's unrolled sum of four vector products by a splat, one multiply |
| `rotate` | loop rotation, of call-free loops that touch memory, whose latch ends in an unconditional jump and whose header is at most 8 pure instructions |
| `unroll` | unrolling by up to 4: a constant trip count by its largest divisor up to that, a run-time one behind a guard, the original loop kept for the rest; complete unrolling of short loops, and of a nest, at a depth of 2 or more and at most 1200 instructions, whose outer loop runs at most 8 times; a loop that runs once loses its back edge |
| `vectorize` | auto-vectoriser: contiguous accesses, loads interleaved two, three or four ways (`ld3` and `ld4` on arm64) and stores two, if-converted bodies, bytes and halfwords computed in lanes twice as wide where their values need it (widening multiply-adds, a rounding shift as an add-high-narrow), integer, widening and float min/max reductions, argmin and argmax (an index kept beside a min or max), search loops, saturating arithmetic, high-half multiplies, byte reversal and int/float conversions; several copies of the body per iteration, each with its own accumulator; a loop a producer unrolled by three, unrolled again until it spans whole vectors; with runtime alias checks and a scalar fallback |
| `addroff`, `memptr` | fold address arithmetic into displacements; compute a native pointer once per base, and for constant addresses too large for a displacement one per block, to the 4 KiB page below them where that reaches them (one add to the memory base), also for an access alone in its block |
| `ldcomb` | merge byte-assembling loads into one wide load |
| `selftail` | turn self `return_call` into a loop |
| `gloop`, `gssa` | keep globals in registers across loops; promote a global to SSA where that does fewer memory operations on it, such as a shadow stack pointer moved in a loop, or Go's where `go-sp` does not pass it (`-fgssa-all` promotes every one, which costs more than it saves) |
| `loopclose` | loop-closed SSA for values used after a loop |
| `escape` | remove GC objects that never escape and forward their fields |
| `partial-escape` | allocate a GC object that escapes on some paths only where it escapes |
| `devirt`, `closed-world` | turn `call_ref` and `call_indirect` into direct calls, also through vtables in immutable globals; with a closed world, use vtable slots too, and let `reach` drop what only a struct field of a global's initialiser names that no `struct.get` reads (on at every level, off for a component) |
| `trampoline` | a module whose functions each return the table index of the next to run, to one loop that calls through the table until one returns 0, as GHC's code returns to its runtime's `StgRun`, has them tail-call the next themselves, directly when the index is a constant, with up to three globals they alone use, the most used, passed as arguments and written back when the loop ends, and stored before and loaded after a call that can run the loop again -- the loop's function, what calls it, an import other than WASI's when an export can run it -- or stored before every call in a module that catches exceptions; made only where nothing else can tell: the module has one table, the loop's is the module's one call of that type and alone in a loop that calls again with what it returns until that is 0, as `StgRun`'s is, nothing calls or takes a reference to those functions any other way, and a call through another type that reached one still traps |
| `go-sp` | a module Go's compiler made (a `go:buildid` section) has each function of Go's one type take the stack pointer as an argument and return it beside its result, in place of reading and writing global 0 at every call; arguments, results and frames stay in Go's stack memory for its collector and stack growth, the global is stored before a call into a function of another type that can reach it -- the write barrier, the resume loop, an import the host could call back through, by an export or a reference it could be handed -- and loaded after, and a goroutine's switch to another stack goes through the resume loop, which hands each function the pointer it resumes on; made only where nothing else can tell: no function of Go's type exported or taken by `ref.func`, none imported into a table or taken by `ref.func`, no table or stack pointer imported or exported, and no tail call, `call_ref`, `table.get`, `array.new_elem`, `array.init_elem`, function type with a supertype, or exception handling in the module |
| `go-resume` | in a module of Go's whose table 0 nothing writes, a function's resume dispatch -- the `br_table` on its first parameter that Go's compiler starts it with -- keeps only the cases something can resume it at: 0 and 1, the blocks after its calls, named by the return addresses it stores, and the jumps back it makes; every other case traps, and 0 and 1, a call's way in, are tested ahead of the rest, which only a resume reaches and which is laid out after the function's code: a call falls through to the entry, and the return of each call made to the code after it. A function that calls `runtime.deferreturn`, whose recovered panics resume at an offset Go's function data holds, is left alone |
| `go-noret` | in a module of Go's, a function of Go's type whose returns can only be reached past calls of functions that never return normally -- `runtime.sigpanic`, the bounds checks' panics, `morestack`, and what only returns through them, the greatest such set -- never returns normally either, and the test after a call of one is taken as unwinding: the way on past a nil or bounds check no longer joins the hot path, nor the stack pointer it returned the one the code goes on with |
| `wrap-params` | on arm64, an i32 block parameter every reader of which takes its low half only, and that an edge passes the wrap of an i64 read on after the wrap, takes the i64s, wrapped once at the block's head: Go's stack pointer after a call, the low half of the call's result, stays in the result's register on both ways on, to the code after the call and to the unwinding return |
| `ictab` | a `call_indirect` through table 0, when nothing writes it, it holds no imported function and it is not table64, loads its target from a code table per type, whose slots that fail the call's checks hold trap stubs, so the call needs no signature, null or instance check |
| `reinline` | after the cleanup passes, inline again what devirtualisation made direct and the calls an allocation leaves by |
| `throw-jump` | a `throw` caught in its own function, without the exnref, becomes a jump to the catch |
| `gcpure` | treat immutable GC fields and `array.len` as pure; drop redundant null and bounds checks, and casts and type tests that a dominating `ref.test` or cast already decides |
| `sealed` | in a module no reference crosses the boundary of, either way, every object the code meets is the module's own, so a cast or type test of one of its types whose passing types are one run of its numbering -- a type and the subtypes numbered after it -- compares the object's type slot inline and never asks the runtime; a cast that lets null through, whose object an access then checks for null, traps on null itself, so the object is checked once. Another module's objects carry type ids outside the module's range, so a module wrongly taken for sealed could only fail a cast that should pass, never pass one that should fail (on at every level) |
| `once` | TeaVM initializes a class lazily: a global holds the class's initializer until it runs, which first stores an empty function there, and every static access calls whatever the global holds. A call through such a global goes where the global holds the empty function on every way there -- after another call through it, a call of the initializer or a store of the empty function -- and wherever the initializer only disarms globals of the pattern nothing reads but to call them; a loop that makes such a call is copied, and the copy, without the call, entered once the global holds the empty function, also from the end of a round of the original; the call's effects are the initializer's; and the inliner takes a callee in a loop whose only call is one of these |
| `labcache`, `barelide` | keep the GC allocation buffer open in a one-block loop of small allocations and calls that cannot let anything escape; hoist the load-barrier mask out of a safepoint-free loop, and drop a repeated barrier on the same slot between safepoints |
| `reach`, `prune`, `merge` | module-wide reachability, whose unreachable bodies are not compiled; a body the optimised code no longer calls becomes a stub that traps; share byte-identical functions |
| `split` | live-range splitting at loop boundaries in loops that make no call, so a value the loop carries keeps a register inside it although it is spilled outside |
| `align`, `small-loop-align` | code alignment; a loop small enough for one 64-byte fetch block is placed inside one (arm64) |
| `cpool`, `regalloc`, `guard-pages` | constant pools; the real register allocator (off: every value lives in a stack slot); guard-page bounds checking |
| `compress-data` | write the active data segments of a memory the module defines, when every one has a constant offset in bounds, as one compressed image of what they make of it, which instantiation decodes into the memory (see [Runtime](#how-it-works)); off, each segment's bytes as they are. On at every level |

`-fno-loadbar` stops emitting GC load barriers. That is unsound for a module
with GC references, because relocation heals them through the barrier; it
exists so that `test/run_nobar.sh` can show that a module without GC
references compiles to the same bytes either way.

`-j N` (or `WARMC_JOBS`) caps the number of compile threads. By default warmc
picks a count from the module size and the number of cores. Functions are
placed in index order whatever the scheduling, so the output does not depend
on the count (`test/run_jobs.sh` checks it); `-j1` also makes the count
exact, as a module with fewer functions than threads uses fewer.

### How memory is protected

With guard pages (`-O2` and `-O3`, or `-fguard-pages`), each 32-bit linear
memory reserves 4 GiB plus a 32 MiB guard region of address space. An access
whose static offset is within the guard needs no explicit check: a fault
there becomes the `out of bounds memory access` trap.
Without guard pages, and always for memory64, accesses are checked explicitly,
and the `bce` pass removes the checks it can prove redundant. A 64-bit memory
has no guard region and reserves what it may grow to: its declared maximum, up
to 4 TiB, or 64 GiB when it declares none, and less, halving, where the
address space runs short. Its pages cost nothing until written, except on
Windows, which commits them when the memory is made or grows. Stack overflow
is detected by a prologue check in every function that makes calls. It traps
as `call stack exhausted` rather than crashing.

### What a trap leaves behind

A trap is the same at every level and with every flag. What memory, tables
and globals hold afterwards is unspecified unless the module is compiled with
`-fexact-trap`, which no level turns on. That matters only to an embedder that
calls an instance again after a trap, or reads its memory. Without the flag:
- `mem` removes a store that a later one overwrites, even where a trap can
  come between the two;
- `gloop` and `gssa` keep a global in a register until a loop or the function
  ends;
- `trampoline` and `go-sp` keep globals in arguments;
- `dead-frames` removes stores into a frame;
- `licm` moves a load that can trap above a store that stays in its loop;
- the vectoriser leaves a range that runs past the end of memory to the
  guard page, which faults a whole vector before the scalar iterations ahead
  of it have run;
- on AArch64, a store that crosses the end of memory into the guard page may
  have written its bytes before the page when it faults. Apple's cores write
  none. The same holds for a short constant-length `memory.fill` or
  `memory.copy`, and for two stores made one `stp`.

With `-fexact-trap`, everything written before the trapping instruction is
there, and nothing after it:
- `mem` keeps a store when a trap can come before the one that overwrites it;
- `gssa` stores a global it keeps in a register before whatever can trap,
  calls included;
- `gloop` leaves alone a loop that can trap, and writes the global back at
  every way out;
- `licm` moves no load above a store or bulk write that stays in the loop;
- the vectoriser checks every range against the memory's size, and runs the
  scalar loop for one past it;
- on AArch64, a store wider than a byte and a constant-length bulk operation
  are bounds-checked before they are made;
- `trampoline`, `go-sp` and `dead-frames` are off, whatever their own flags
  say and in whatever order.

Where the stack runs out, and where an allocation fails, can still move:
inlining checks a callee's frame at its caller's entry, and partial escape
allocates an object only where it escapes. wasm leaves both to the
implementation. `test/run_trapstate.py` checks each of these rules (see
[Testing](#testing)).

The cost, as the geometric mean of `-O2 -fexact-trap`'s speed over `-O2`'s
on each corpus (Apple M-series):
- 0.94 to 0.99 for C, C++, Rust, Zig, Kotlin, TinyGo and the vector kernels;
- 0.82 for Go, whose stack pointer `go-sp` then leaves in memory;
- 0.79 for AssemblyScript, whose shadow-stack stores must stay ahead of every
  bounds check that can trap;
- 0.65 for Haskell, without the trampoline.

---

## Command-line reference

| Option | Meaning |
|---|---|
| `-o FILE` | output path (default `a.out`, `a.exe` on Windows, `out.o` with `-c`, `out.wasm` with `--emit-wasm`, `libNAME.so`/`libNAME.dylib`/`NAME.dll` with `--shared`) |
| `-c` | write a relocatable object and stop |
| `--shared` | build a shared library (see [Shared libraries](#shared-libraries)) |
| `--header PATH` | where `--shared` writes the header (default `PREFIX.h` beside the library) |
| `--target linux\|macos\|windows` | output format and ISA (default `linux`; `darwin` means `macos`) |
| `--rt PATH` | runtime archive to link (default `$WARMC_RT`) |
| `--wasi[=PATH]` | link a WASI host and run `_start` (core module) or `wasi:cli/run` (component) |
| `-O0` `-O1` `-O2` `-O3` | optimisation level (default `-O2`) |
| `-fPASS`, `-fno-PASS` | toggle one pass (see [Optimisation](#optimisation)) |
| `-fexact-trap`, `-fno-exact-trap` | leave memory, tables and globals after a trap as wasm defines them (see [What a trap leaves behind](#what-a-trap-leaves-behind)); off at every level |
| `-j N`, `-jN` | maximum number of compile threads |
| `--profile=full\|deterministic` | the WebAssembly profile. `deterministic` makes relaxed-SIMD and NaN results platform-independent. |
| `--symbol-prefix=P` | name the module descriptor `P_module`; with `-c` and several modules, the i-th is named `Pi_module`; with `--shared`, the prefix of the library's own functions instead; ignored for a component |
| `--custom-sections=default\|all\|none` | which of the module's custom sections the output carries (see [Custom sections](#custom-sections)); `default` leaves out the name section and `.debug_*` |
| `--no-export-names` | drop the export-name table, keeping only `_start` and `main`; not for a component or with `--shared` |
| `--no-function-names` | name every function's symbol `wasm_func_N`, by its index, rather than as the name section does |
| `--no-dead-strip` | keep unreferenced sections in the link |
| `-s`, `--strip` | leave the names of the module's and the runtime's functions out of the executable's symbol table (Linux and macOS), which only debuggers and profilers read |
| `--system-ld`, `--ld PATH` | link with an external linker instead (Linux and macOS only, not with `--shared`) |
| `--native-module NAME` | call the C function of each function import of module `NAME` (see [Calling C functions](#calling-c-functions)); implies `--system-ld` |
| `--link ARGS` | more for the system linker: objects, archives, `-framework` and `-l` options, in its shell syntax; repeatable |
| `--keep-obj` | keep the intermediate object |
| `-v` | print the external link command |
| `--validate-only`, `--emit-wasm`, `--dump-*`, `--print-ir`, `--stats`, `--profile`, `-fno-loadbar` | see above |
| `--magic=D[:64]` | print the multiply, add and shift that divide by `D` (unsigned; 32-bit, or 64-bit with `:64`) and stop; several divisors, comma-separated, get a line each |
| `-h`, `--help` | print the option summary |

`--target`, `--rt`, `--ld` and `--header` also take their value after an `=`,
as in `--target=macos`.

### Environment variables

| Variable | Read by | Effect |
|---|---|---|
| `WARMC_RT`, `WARMC_WASI` | compiler | defaults for `--rt` and `--wasi=` |
| `WARMC_LD` | compiler | the external linker `--system-ld` runs (unlike `--ld`, it does not turn `--system-ld` on) |
| `WARMC_SYSTEM_LD` | compiler | the same as `--system-ld`, whatever its value |
| `WARMC_MACOS_LIBDIR` | compiler | directory holding `libSystem.tbd` for `--system-ld` on macOS (otherwise found with `xcrun`) |
| `WARMC_JOBS` | compiler | same as `-j` |
| `WARMC_CACHE` | compiler | a directory that keeps what each compile did, under a digest of warmc itself, every argument (the output by its file name only), every file an argument names, every archive next to the runtime, and every `WARMC_` setting except the ones only a program reads: its exit status, what it printed, and its output. A compile whose digest is already there makes its output a hard link to the kept copy, prints what it printed then and exits as it did -- a refusal and the verdicts of `--validate-only` and `--dump-*` included. Not used with `--shared`, `--header`, `--keep-obj`, `--emit-wasm`, `--print-ir` or an external linker, nor for a compile that crashed. `make check` and the test scripts set it to `build/warmc-cache` |
| `WARMC_TIME` | compiler | print phase timings, peak memory and the slowest functions |
| `WARMC_GC_HEAP_KB` | program | GC heap reservation (default 1 GiB) |
| `WARMC_GC_MIN_KB`, `WARMC_GC_GROWTH` | program | collection trigger: minimum size (default 16384, or a quarter of the heap if that is less), and growth as a percentage of live data (default 200) |
| `WARMC_GC_CONC` | program | `0` disables the background collector thread |
| `WARMC_GC_STATS` | program | print a GC report at exit |
| `WARMC_GC_VERIFY`, `WARMC_GC_STRESS` | program | heap verification after each cycle; collect on every allocation |
| `WARMC_GC_RELEASE` | program | return a freed run of at least this many bytes to the system (`madvise`); 0, the default, keeps them |
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
| Compact import section (a module's imports grouped under one module name) | ✅ (the `0x7f` form; the `0x7e` one is refused) |
| SIMD (`v128`) and relaxed SIMD | ✅ (NEON on AArch64, AVX2 on x86-64) |
| Tail calls | ✅ |
| Typed function references | ✅ |
| GC: struct, array, i31, recursive types and subtyping, casts | ✅ |
| Exception handling: `try_table`, `throw`, `throw_ref`, `exnref` | ✅ |
| Exception handling: legacy `try` / `catch` / `delegate` | ❌ |
| memory64 and table64 | ✅ (a 64-bit memory up to 2^26 pages, 4 TiB, or 2^20, 64 GiB, when it declares no maximum; a 32-bit memory up to 65535 pages) |
| Multi-memory | ✅ |
| Extended constant expressions | ✅ |
| Deterministic profile | ✅ `--profile=deterministic` |
| Custom annotations: `(@custom ...)` | ✅ (and wasm-tools' `(@producers ...)` and `(@dylink.0 ...)`) |
| Name section and name annotations | ✅ functions' names become their symbols' (see [How it works](#how-it-works)); the text assembler writes them from `$id`s and `(@name ...)` on functions. Module, local, type and field names are not used. |
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
These decode, assemble and validate core modules, components and WIT. A core
module is decoded whole, every body too, before a verdict of invalid is given,
so one that is invalid in one place and malformed in a later one is malformed,
as the spec has it. The text assembler handles both flat and folded syntax,
names in every index space, inline imports, exports and segments, GC type
definitions, and custom sections: `(@custom ...)` where it says,
`(@producers ...)` last and `(@dylink.0 ...)` first. It writes a name section
of the functions' names: each one's `(@name "...")` annotation, or else its
`$id`. It reads the 3.0 text grammar as written, down to its tokens: a run like
`data"a"` is one reserved token and refused, and so is source that is not
UTF-8.

**Middle end** (`src/ir*.c`, `opt.c`, `inline.c`, `tramp.c`, `gosp.c`). Functions become SSA with
block parameters instead of phi nodes. SSA is built directly from wasm locals,
and exception edges lead to landing-pad blocks. The optimiser runs the pass
pipeline described above, using module-wide effect summaries, reachability and
devirtualisation.

**Back end** (`src/isel*.c`, `regalloc.inc`, `a64.c`, `x64.c`, `codegen.c`).
Instruction selection fuses compares into branches and selects, folds shifts,
bitfield extracts, multiply-adds and addressing modes, and pairs loads and
stores into `ldp`/`stp`. A vector product added into a value, or taken from
one, is an `mla` or `mls` into it, in lanes of 8, 16 or 32 bits, but not
into a value a loop carries round, where the multiply would join the chain
each iteration waits on. From `-O1` a `br_table` of at most eight distinct
edges becomes a tree of compares, and any other a jump table, whose entries
on arm64 are halfwords, each its target's distance in instructions; a
function too large for that reach, 128 KB, is made again with its tables of
words. On arm64, a
function whose entry block tests only what it is passed and what it loads
from memory, and may return at once -- a destructor called on a null
pointer -- makes that test, and that return with the block's stores, ahead
of its prologue; a function with a loop small enough for one 64-byte fetch
block starts at the offset in its block that lets such loops fit with the
least padding run on their entry, the rest of the block left ahead of it,
where nothing runs; a wrap whose readers take the low half only -- an indirect
call's table index among them -- is no instruction, they read the i64's
register; and an i32 extended to i64 where a w-register write made it, its
high half zero already, is no instruction either, as in the return of every
Go function. On arm64 the instance and memory 0's base stay in `x19` and
`x20`: a function is entered with them there and leaves them there, so it
neither loads nor saves them, and a call of the module's own code passes
nothing for them. In an object (`-c`) or a component, where another instance
may call in, a function it can reach -- exported, or named by a segment or an
initialiser, which is how a reference to one is made -- starts with a compare
of the caller's instance against `x19`, and a call from another instance goes
on to a runtime trampoline (`warmc_xenter`) that sets the two for the callee,
copies its stack arguments, and puts the caller's back after; an executable's
or a shared library's module is the only instance, which the host enters
with the two set, and has no such compare. On arm64 a `memory.copy` or
`memory.fill` of memory 0 is no call to the register allocator either, so
the values live across one stay in the registers they have: a constant
length of up to 96 bytes is made inline in `v26`-`v31`, its bounds tested,
where it tests them, against a branch to the trap, so a function with no
other call needs no frame; a length up to 16 bytes is made inline as well;
and any other is a call of `warmc_memmove_r` or `warmc_memset_r`
(`rt/start.S`), which check both ranges and move the bytes in registers the
allocator never hands out, and on macOS hand a long one to libSystem with
every register it may touch saved. The register allocator is a linear scan over SSA webs with
lifetime holes. It rematerialises constants -- on arm64 a zero takes no
register at all, its readers reading the zero register, two stores of zeros
one `stp xzr, xzr` -- recomputes a spilled frame address or other cheap
operation from registers instead of reloading it, holds a spilled value that
a loop reads in a register that is free across the loop, and chooses between
callee-saved registers and caller-saved registers with save and restore
around calls, whichever is cheaper. A value live across a call in a loop
takes the callee-saved registers from the other end than the rest do, so
that a callee's save and restore of the few it uses rarely puts a loop's
counter through memory on every call.
Functions compile in parallel and are laid out deterministically.

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
  included, so `perf`, Instruments, `sample` and debuggers can name the code
  they land in, and demangle a C++ or Rust name: each compiled wasm function
  is named as the module's name section names it, next to the runtime's own
  functions, and `wasm_func_N`, its index, where the section gives it no name
  a symbol can carry, or under `--no-function-names`. Nothing refers to a
  function by that name, which is in the symbol table alone, so it may be
  anything -- a runtime function's, or another function's too. A name
  section that is not as the spec's appendix has it gives no names at all,
  and the module is valid all the same. `--print-ir` and `WARMC_TIME` give
  the names too. `-s` leaves them out. Windows output has no symbol table; a
  COFF object (`-c`) carries the names.

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
  - Evacuation has room kept for it: the program's allocations leave a
    thirty-second of the heap free, at least 1 MB and at most an eighth of
    it. An allocation that finds no room waits for up to two cycles, then
    takes that room, before it fails.
  - The program stops only for short handshake rounds.
  - Roots come from precise per-call-site stack maps.
  - A load barrier in compiled code heals stale references. On AArch64 one
    that needs only the current colour, and while a mark is on its object
    shaded, is handled in a few instructions with six registers saved; a
    reference to an object that may have moved, or one being born, goes to
    the runtime. A cycle that relocates nothing leaves the mark's colour
    current, so the program's next loads take no barrier at all.
  - Objects, and arrays of a constant length, are bumped inline from the
    program's allocation buffer, zeroed when their page was opened; on
    AArch64 so are arrays of a computed length.
- **Exceptions.** They are unwound by walking warmc's own compact unwind
  tables, with no DWARF and no libunwind. The collector walks the same
  tables for its roots, and nothing else does: a program's only module with
  no tag to throw and no type of the collector's has none.
- **Data segments.** A memory the module defines, whose every active segment
  has a constant offset that fits its initial size, gets the image those
  segments make of it, compressed at compile time (`src/datapack.c`, LZ77 in
  LZ4's tokens, a run of zeros skipped) and decoded into the new memory as
  it is instantiated (`rt/lzunpack.inc`). Any other memory's active segments
  are each its own bytes, applied in order; only the passive segments the
  code reads are kept by index, for `memory.init` and `array.new_data`. A Go
  `net/http` server's 90688 segments, 2.5 MB of bytes and 1.45 MB of their
  descriptors, are a 1.1 MB image, which takes 0.3 ms more to start.
- **Function references.** Each function's is eight bytes in the module's
  descriptor -- where its code is in the module's text, and which of the
  types its functions have it is -- made into the instance's at
  instantiation; an element segment's item that is a plain `ref.func` is the
  function's index in its entry.

---

## Performance

`test/baseline.json` records warmc against two references on the author's
Apple silicon machine: wasmtime 50.0.0 running the same wasm, and the same
source compiled natively. warmc runs at `-O2`. Each figure is the geometric
mean of warmc's time divided by the reference's, so below 1 means warmc is
faster. Each program's empty-program time is subtracted first, so the ratios
compare steady-state code rather than start-up or compile time. All three
are timed interleaved, round by round, each kernel's figure the best of five
rounds (`make bench-record`); `make bench-record-warmc` re-times warmc's
column alone, best of seven, against the recorded wasmtime and native times,
as the `stdgo` row is. The `-O3` columns are each kernel's recorded ratio times its `-O3`
time over its `-O2` time, the two timed interleaved, warmc alone, each the
best of five rounds (`make bench-record-o3`), so that they differ from the
`-O2` columns by what `-O3` changes and not by how either was timed.

The wasm comes from each language's usual producer: TinyGo 0.43 at
`-opt=2`, Go 1.27.3, rustc nightly 1.101 at `opt-level=3` with LTO, Zig 0.17
`ReleaseFast`, AssemblyScript 0.28.20 at `-O3`, Kotlin 2.4.21, TeaVM 0.16.0
at its `FULL` level, wasi-sdk 34 (clang 23.1) at `-O3`, and GHC 9.14 at
`-O2`. Only the Zig and AssemblyScript
corpora are built with wasm SIMD; the Haskell modules carry some too, in the
runtime system, libc and gmp that GHC's wasm toolchain ships prebuilt. The
native builds use the same language's own
compiler at `-O2`, or its nearest equivalent: clang 23.1 `-O2`, rustc
`opt-level=2`, Zig `ReleaseFast`, TinyGo `-opt=2`, `go build`, GHC `-O2`
(with `-with-rtsopts=-V0`, see below), and for Java the same classes run by
Temurin 25.0.5's JVM (see below).
AssemblyScript and Kotlin/Wasm have no native compiler to compare with, and
the native columns cover 42 of `vecbench`'s kernels and 27 of `hsbench`'s
(see below).

| Corpus | Source language | Kernels | warmc / wasmtime | Range | `-O3` / wasmtime | warmc / native | Range | `-O3` / native | Median peak RSS, MiB (warmc / wasmtime / native) |
|---|---|---:|---:|---|---:|---:|---|---:|---|
| `gobench` | TinyGo | 39 | 0.59 | 0.06 – 1.01 | 0.59 | 0.88 | 0.13 – 3.09 | 0.87 | 2.0 / 8.9 / 1.9 |
| `stdgo` | Go | 49 | 0.53 | 0.03 – 1.12 | 0.53 | 1.44 | 0.08 – 9.45 | 1.44 | 7.3 / 27.4 / 4.7 |
| `vecbench` | TinyGo (vectorisable loops) | 43 | 0.18 | 0.03 – 0.96 | 0.18 | 0.72 | 0.11 – 1.19 | 0.71 | 2.0 / 8.9 / 1.9 |
| `asbench` | AssemblyScript | 37 | 0.59 | 0.16 – 0.98 | 0.58 | – | – | – | 1.9 / 8.7 / – |
| `rsbench` | Rust | 39 | 0.63 | 0.13 – 1.00 | 0.61 | 1.04 | 0.63 – 2.64 | 1.02 | 2.0 / 9.1 / 1.9 |
| `zigbench` | Zig | 22 | 0.72 | 0.17 – 1.00 | 0.72 | 0.97 | 0.21 – 1.77 | 0.97 | 2.0 / 8.8 / 1.9 |
| `ktbench` | Kotlin (Wasm GC) | 25 | 0.10 | 0.004 – 0.72 | 0.11 | – | – | – | 3.5 / 21.0 / – |
| `cbench` | C | 27 | 0.75 | 0.26 – 1.01 | 0.73 | 1.03 | 0.008 – 4.29 | 1.00 | 2.0 / 9.0 / 1.8 |
| `cppbench` | C++ | 26 | 0.69 | 0.22 – 1.04 | 0.69 | 0.90 | 0.03 – 1.58 | 0.89 | 2.4 / 9.5 / 2.1 |
| `hsbench` | Haskell | 28 | 0.44 | 0.20 – 0.97 | 0.44 | 1.32 | 0.61 – 6.42 | 1.32 | 10.1 / 32.4 / 12.8 |
| `javabench` | Java (TeaVM, Wasm GC) | 29 | 0.42 | 0.02 – 1.01 | 0.42 | 1.01 | 0.26 – 9.40 | 1.01 | 8.3 / 10.2 / 58.8 |

### Against wasmtime

Over all 364 kernels, warmc's code takes **0.45×** wasmtime's time, and
0.44× without `stdgo`. Seven kernels are slower than wasmtime. Five of them
are within 1.5%, inside the spread between runs: `gobench/strswitch` (1.01),
`cbench/malloc` (1.01), `javabench/alloc` (1.01), `cppbench/bitset` (1.004)
and `zigbench/optional` (1.001). The other two:

| Kernel | warmc / wasmtime | What is known |
|---|---:|---|
| `stdgo/memcpy` | 1.12 | the platform's memmove under both engines, whose ratio spreads by more than 10% between runs |
| `cppbench/algorithms` | 1.04 | code placement: the same instructions laid out with no loop header aligned run 6% faster, below wasmtime |

TinyGo's `-opt=2` unrolls loops that its size-first default leaves alone,
which hides them from some of warmc's loop passes. LLVM's unrolled byte search
is vectorised as the rolled one is (`vecbench/search`, 0.40×), and a
callee whose loop LLVM unrolled into several calls through a closure is
inlined where the closure is a constant, so the calls inline too
(`gobench/closures`, 0.15×). A loop LLVM unrolled by three, whose stride
neither divides a vector nor is a whole number of them, is unrolled again
until it spans three vectors and vectorised: `vecbench/neighbour` runs at
0.21×, and `cbench/matmul`, whose inner loop is the same shape in `f64`, at
0.42×.

`stdgo` is Go built by Go's own compiler, which is not LLVM. It compiles
every function to a loop around a `br_table` over the function's resume
points, so that a goroutine switch can unwind the wasm stack and re-enter the
function where it left off, and it sends each jump inside the function -- a
loop's back edge among them -- through that dispatch with a constant index.
warmc threads those jumps to their targets (`thread`), gives the dispatch a
cold copy of the loops it can enter in the middle so that the originals have
one way in again (`nodesplit`), and passes Go's stack pointer to each
function and back in registers instead of through its global, a store and a
load on the path of every call (`go-sp`), which takes 11% off `stdgo`'s
cycles. With the loops back, the vectoriser takes Go's
64-bit address arithmetic too: `stdgo/strsum` runs at 0.03× and `stdgo/sum`
at 0.04×.

### Against native code

Over the 300 kernels with a native ratio, warmc's code takes **1.02×** the
native time (median 1.05), and 0.99× without Haskell. wasmtime's code takes
2.08× the native time on the same kernels. 119 kernels run faster under warmc
than natively, and another 50 are within 10% of the native build.

The native builds start ahead of the wasm in a few ways that have nothing to
do with warmc's compiler:
- **SIMD.** LLVM's native auto-vectoriser uses NEON, while the TinyGo, C, C++
  and Rust corpora are built without wasm SIMD. The vector code they get is
  what warmc's own vectoriser makes of scalar loops. It takes saturating
  arithmetic, high-half multiplies, byte reversals and int/float conversions,
  and runs several copies of a loop's body per iteration with an accumulator
  each: `satadd`, `mulhi16` and `bswap` run at 0.66×, 0.75× and 0.92×
  native, and `vecbench/neighbour`, a loop LLVM unrolled by three, at 0.98×.
  `vecbench/rgbgrey` reads pixels three bytes apart, loads them with one
  `ld3` and computes in 16-bit lanes as the native build does, at 1.00×.
- **Hand-written assembly.** `stdgo/sha256` is 9× because native Go computes
  SHA-256 with the ARMv8 SHA-256 instructions, where the wasm build runs the
  portable Go code.
- **The producer's wasm back end.** Go's wasm port keeps nearly everything
  64-bit and dispatches through resume points (see above). GHC's wasm backend
  returns every function to a trampoline in the runtime and keeps its virtual
  registers in globals, where its native code generator jumps directly with
  them in machine registers. warmc turns those returns into tail calls and
  passes the three registers the functions use most as arguments
  (`trampoline`), which takes 29% off `hsbench`'s cycles.
- **Java's class library.** The Java native is the JDK's: the same classes
  run by the JVM against its own library, where the wasm carries TeaVM's.
  `javabench/list` runs at 9.4× the JVM's time because TeaVM's `ArrayList`
  has no `removeIf` of its own and takes `Collection`'s, whose
  `iterator.remove()` shifts the rest of the array for each element it
  removes, where the JDK's compacts the array in one pass.

The native build is slower where the platform's own mechanism costs more
than wasm's:
- `cbench/setjmp` runs at 0.008× native: macOS's `setjmp` and `longjmp` save
  and restore the signal mask, a system call each, where wasi-sdk's clang
  lowers them onto wasm exceptions, which warmc unwinds through its own
  tables.
- `cppbench/exceptions` runs at 0.03×: a native C++ throw goes through the
  platform's two-phase unwinder, and warmc unwinds through its own compact
  tables.
- `stdgo/atomics` runs at 0.08×: native Go's atomics are real atomic
  instructions with barriers, while Go on wasm is single-threaded and its
  atomics are plain loads and stores.

45 kernels print a different result natively, though they do the same work.
TinyGo's `int`, Rust's `usize`, C's `long` and Haskell's `Int` are 32 bits on
wasm32 and 64 natively. Native floating-point code is also not always rounded
as wasm's is: clang fuses multiply-adds natively, for one, which warmc does
only with `-ffma` or at `-O3`. Without those 45 kernels the geometric mean
is 1.03×. The Haskell natives are built with `-with-rtsopts=-V0`: without it
the runtime's exit would wait out its 10 ms ticker, which would make the
empty program's time mostly that wait and leave every kernel's native time
10 ms short. The Java natives are Temurin 25.0.5's JVM as it comes, running
the classes javac compiled, and the floor subtracted from them is the JVM
running an empty `main`. What remains of a kernel's time includes the
interpreter and the JIT compiling it, which at the tens of milliseconds a
kernel runs for is part of what the JVM costs. The three kernels where
warmc's code is fastest against it are the three whose JVM peaks at
520–650 MiB resident: `javabench/exn` at 0.26×, `complex` at 0.33× and
`alloc` at 0.35×. 302 kernels have a native build, but two native times are
too short to time reliably, under 5 ms: `vecbench/rowsum` and
`hsbench/fib`, which have no native ratio (their memory figures still
count).

### At `-O3`

At `-O3` warmc's code takes **0.44×** wasmtime's time (0.443) and **1.01×**
the native time, 0.991 of its time at `-O2`, over the same 364 and 300
kernels. The gains are where the two passes `-O3` adds have something to do:
- `dead-frames` removes the stores into C frames that nothing reads again:
  `cbench/structval` takes 0.54× its `-O2` time.
- `fma` fuses float multiplies into the adds that read them:
  `vecbench/horner` takes 0.63×, `rsbench/f32ops` 0.66×, `asbench/mathfn`
  0.77×, `asbench/stencil` 0.78×, `javabench/alloc` 0.80× and `cbench/nbody`
  0.84×.

167 of the 364 kernels compile to the same code at both levels, most of
`gobench`, `vecbench`, `rsbench`, `zigbench`, `ktbench` and `javabench`.
Their `-O3` figures differ from `-O2`'s only by the spread between runs:
between 0.93 and 1.11 for single kernels, 1.004 on average. That spread is
all that moves `ktbench` from 0.10 to 0.11 (0.104 and 0.105 unrounded). The
197 kernels whose code changes take 0.981 of their `-O2` time. Three of them came out
more than 3% slower than at `-O2`: timed again, `stdgo/recurse` ran at 0.98
of its `-O2` time and `asbench/utf8enc` at 1.00, and `hsbench/fib` is too
short to time.

Re-run the comparison on your own machine before relying on these numbers:

```sh
python3 test/gobench.py --corpus rsbench --rounds 5     # one corpus, against wasmtime
python3 test/gobench.py --corpus rsbench --native       # and against the native build
make bench-compare                                      # all corpora vs the recorded baseline
make bench-record                                       # re-record it, native column included
make bench-record-warmc                                 # re-time warmc's column alone against it
make bench-record-o3                                    # warmc at -O3 beside -O2, for the -O3 columns
```

---

## Testing

```sh
make check          # the full test suite: rows run in parallel, logs in build/checklogs
make smoke          # a few of its rows that reach most of warmc in under 30 CPU-seconds; CI runs it on pushes to main and on pull requests
make check-encoder  # the AArch64 encoder against GNU as, llvm-mc or clang; the x64 one against llvm-mc
make check-spec     # the official WebAssembly testsuite, at -O1, -O2 and -O2 --profile=deterministic
make conformance    # write CONFORMANCE.md: spec results grouped by proposal
make fuzz           # random modules vs wasm-interp (FUZZ_SEED, FUZZ_COUNT)
make check-combos   # the cases, traps and GC tests with every triple of passes turned off and on in all combinations, the language corpora with every pair
make gc-jitter      # GC schedule sweep with randomised collector delays (gc-jitter-full: every mode under every seed)
make check-linux    # run the GC tests, the WASI host's sockets and stdio flags (test/run_wasi_linux.sh), and on a macOS host with tinygo the TinyGo WASI corpus, as real Linux binaries (podman/docker arm64)
make check-tools    # fetch the pinned tools make check uses, alone
```

Before its rows, `make check` runs `check-encoder` and, except on Windows,
builds the sanitiser compilers; on Windows it checks that the Win32 offsets
the runtime hard-codes match `windows.h` instead. CI runs the whole of it
nightly. `CHECK_BENCH=0` leaves out the benchmark corpora, as CI does, and
`CHECK_TIMING=1` lists each row's wall and CPU time at the end.

`make check` keeps what it can, each thing under exactly what it depends
on. warmc's compiles go to `WARMC_CACHE=build/warmc-cache`.
`test/testcache.py` keeps the suites' own results in `build/testcache`:
- wasmtime's answers, under wasmtime's version, its arguments, the bytes of
  every file they name, and its stdin (`test/shims/wasmtime`, first on
  `make check`'s `PATH`; a run with a preopened directory, a network, or a
  stdin that may not end goes to wasmtime every time);
- a program's output, under the program's bytes, its arguments and its
  settings;
- the corpora's modules, under their sources, the flags and the toolchain's
  version; TeaVM's builds and the JVM's answers, under the Java sources,
  TeaVM's patches, TeaVM itself and the JDK; and `tinygo build`, `go build`
  and `rustc` with an `-o`, under the tool's version, the arguments and the
  sources (`test/shims/buildcache.py`);
- the host objects `test/hostbuild.py` compiles with clang, under clang's
  version, the command, the source, the names in each directory it includes
  from, and the contents of every file the compile read the time before
  (clang's `-MD`);
- what wasm-tools and wabt make of a file for `test/run_wat.py`, under the
  tool's version, its command and the file's bytes;
- a suite's whole result, where none of its programs, its scripts or its
  settings changed since a run that passed;
- and each row's whole answer (`test/row.py`), under warmc and its sanitiser
  builds, the runtimes, the pinned tools, the spec suite's corpus as checked
  out, every file of the tree outside `src/`, the toolchains on `PATH` the
  suites call, and the row's own command and settings: a `make check` with
  nothing changed answers from it, and one after a change to warmc runs
  every row, through the caches above.

After the rows, each cache that is over its size limit is cut to 90% of
it, least recently used first: 20 GB for warmc's (`WARMC_CACHE_MB`) and
2 GB for the suites' (`WARMC_TESTCACHE_MB`). This runs on its own once
`make check` has ended, so no run waits for the deleting. A later run that
reads an entry meanwhile holds all of its files open before using any of
them, so it reads the entry whole or not at all. An entry never changes once
it is in place, so a ledger (`.sizes`) keeps each directory's sizes. A
directory is listed again only when its own time says an entry came or went.

The shims run through wrappers that `make check` writes to `build/shims`,
one for each of these tools that is installed. Each wrapper names python3,
the shim and the tool by absolute path, so a suite that runs wasmtime under
`env -i` still reaches it. `make check` stops at once if wasmtime answers
differently there.

Each suite links its modules into a few programs of several modules, not a
program per test. The suites of many small modules (spec's corpus, the text
front end's fixtures, the mutated components) hand warmc many inputs per
process for `--validate-only`, `--emit-wasm` and `-c`. `WARMC_NO_TESTCACHE=1`
runs everything.

Most of the suite is **differential**. warmc's output is compared with:
- wabt's `wasm-interp`, for the hand-written cases in `test/cases`: at `-O0`,
  `-O1` and `-O2`; with the register allocator and SSA verified; with
  `-fgssa-all`, with `-fdead-frames`, with `-fexact-trap`, and with far
  branches forced; and at nine settings that turn every pair of passes off
  and on in all four combinations, so that each pass is off beside every
  other both on and off (`test/run_combos.py`, which runs the trap and GC
  tests and the C workload at them too). wasmtime runs the cases as a second
  engine.
- wabt's `wasm-interp`, for C, SIMD, control-flow and addressing workloads
  at each level (`test/run_workload.sh`), whose addressing variants must also
  compute what the 32-bit one does
- wasmtime, for WASI preview 1 and 0.2 programs (stdout, stderr, exit code and
  the resulting directory tree), and for linked components (`test/run_clink.py`)
- wasmtime, for the benchmark corpora in Go, Rust, Zig, AssemblyScript,
  Kotlin, Java, C, C++ and Haskell; four of them also with `-fdead-frames`,
  and four with `-fexact-trap`
- wasmtime, at `-O0`, `-O1`, `-O2` and `-O2 -fdead-frames`, for 332 test
  programs that are not benchmarks: `test/ctests` (C, 36), `cpptests` (C++,
  40), `rstests` (Rust, 34), `gotests` (TinyGo, 36), `stdgotests` (Go, 44),
  `astests` (AssemblyScript, 40), `kttests` (Kotlin, 22), `javatests` (Java,
  22, built with TeaVM's `--strict`), `zigtests` (Zig, 35) and `hstests`
  (Haskell, 23). Each one covers a single language area:
  integer and float edge cases, strings, collections, closures, exceptions,
  generics, the GC and the runtime library. Each prints only results that
  wasm fixes exactly; for example, it prints no arithmetic NaN bits.
- `wasm-tools`, for component, WIT and WAT handling
- wasmtime again, for the TeaVM adapter (`test/run_teavm.py`): modules shaped
  as TeaVM writes them, each host call the adapter defines called and printed,
  the string builtins' traps, the calls it stops at and an exception `main`
  lets out, against what each must print and its exit status; the JVM's
  `StrictMath`, which `test/teavm/FdLibm.java` must match bit for bit; and
  the JVM again for TeaVM's patches, a program for each in `test/teavm/fixes`
  built with the patched TeaVM, with and without `--strict`, each run's
  output the JVM's for the same classes
- clang, for `--native-module` (`test/run_native.py`): C functions
  (`test/native/natives.c`) that answer with where each argument arrived,
  called at every level with up to twenty arguments of each kind, across
  calls that write every caller-saved register, from a tail call, back into
  the module from C, on linear memory at `warmc_memory_base`'s address, and
  through `warmc_call` at an address, of each result type; the arguments
  loaded into C's registers rather than moved there, and the address called
  in the register it is in; and each use of a native import warmc refuses
- the JVM with LWJGL's own natives, GLFW and macOS's OpenGL, for `lwjgl/`
  (`test/run_lwjgl.py`, run by hand -- `make teavm glfw lwjgl` and a display
  first -- and not by `make check`): LWJGL's HelloWorld built; LWJGL's off-heap memory,
  in the program's and in C's, printing what the JVM does; calls through
  `JNI` of C of the program's own, with results narrower than a register,
  arrays of each type, 23 arguments, floating point and callbacks, an
  exception one throws coming out of the call that ran it; `GLCapabilities`
  field for field the same, for a legacy and a 4.1 core context, whether it
  looks up only what macOS's framework exports or everything, and checks of
  each shape folded or left as they must be; and a program that makes a
  window, reports a GLFW error through the error callback, has
  a callback's exception come out of the GLFW call that ran it, and reads
  back pixels cleared to blue, its module calling each GLFW and OpenGL
  function by an import of its own
- the Component Model's reference `definitions.py`, for canonical-ABI layout
  and for what the generated adapter code computes (`test/run_adapter.py`)
- wabt's `wasm-interp` again, for random expressions of the optimiser's folds
  and their near misses (`test/gen_folds.py`): ladders of field swaps, whole,
  rotated, cut short or with a mask, a shift or a step changed; float
  computations of converted integers truncated, saturating or compared with
  a constant; and TinyGo's select tree round a truncation, a bound or its
  NaN test now and then changed
- wabt's `wasm-interp` again, for random data segments (`test/gen_datapack.py`)
  in one or two memories, 32- or 64-bit, laid out as Go's linker lays them
  or at random, overlapping, empty, at the memory's end, across a block of
  the compressed image, some with an offset that is an expression and some
  passive, compiled with the image's blocks 64 bytes long and with no
  compression too; and what the binary keeps of the passive ones
- wabt's `wasm-interp` again, for random modules in Go's shape whose stack
  pointer `go-sp` passes as an argument (`test/gen_gosp.py`), and for small
  ones with one thing each that rules the rewrite out (`test/run_gosp.py`;
  those with imports or subtypes are only compiled, for the rewrite's
  absence)
- `-O0`, which makes no `trampoline` rewrite, and results worked out by
  hand, for small modules in GHC's shape with one thing each that rules the
  rewrite out or needs more of it -- a trampoline run inside another, an
  exception caught after it went through the loop (`test/run_tramp.py`); a
  library whose import runs one inside another (`test/shared/tramp.wat`)
  and a trampoline beside a global imported from another module
  (`test/link/tramp_b.wat`) check the host's part
- results worked out by hand, at `-O0`, `-O1`, `-O2` and `-O3`, for small
  modules each of which some pass once compiled wrong or is to transform
  (`test/run_optcases.py`, `test/cases_opt`); a module also says what
  `--stats` counts for it at each level, and how many of a function's AArch64
  instructions at `-O2` match a pattern, in the whole function or after its
  `ret`, where the blocks emitted after the rest go, for what changes the code
  and not what it computes; `test/cases_opt/trunc_exact.wat` is made by
  `test/gen_fexact.py`
- exact arithmetic, for every division's magic numbers (`test/magic.py`)
- the module itself, for the custom sections each linked image carries, read
  back from Mach-O, ELF and PE (`test/check_macho.py`, `check_elf.py`,
  `check_pe.py`) and asked for by name from a library (`test/run_customs.py`),
  and for the names its functions' symbols take in Mach-O and ELF

The other rows check what has no second engine to compare with:
- the data image's compressor and decoder on their own
  (`test/datapack_test.c`): every block decoding to what was compressed
  into memory that starts zero, nothing written outside it, streams that
  ask for more than the block or a match from before it trapping; and data
  segments into an imported memory that is not zero (`test/link/dpmem_*`)
- `br_table`'s jump tables past the reach of arm64's halfword entries, of
  70000 entries, against answers worked out by hand (`test/run_bigtable.py`)
- a Go `net/http` server (`test/gohttp`, `test/run_gohttp.py`), built by
  Go's own compiler and run at `-O0`, `-O1` and `-O2` with a listener and a
  directory preopened: many requests on one connection and many connections
  at once, large bodies both ways, clients that go away, a socket's reader
  and stdout's that are slow, while stdin is waited on throughout and the
  pipes' own flags stay as they were. wasmtime has no listener for preview 1
  to compare with.
- the trap tests (`test/cases_traps`), against the trap each export must
  raise, listed in `test/run_traps.sh`, at each level, at `-O3`, with
  `-fexact-trap`, without guard pages, without the register allocator and
  with far branches forced
- what a trap leaves in memory and globals with `-fexact-trap`, read by the
  calls after it on one instance (`test/run_trapstate.py`,
  `test/cases_trapstate`, with `test/link/statedriver.c`), at each level,
  against results worked out by hand. For what Apple's cores cannot show, a
  store left half-made by a fault, it asks `--stats` whether the checks were
  made.
- the GC suite (`test/cases_gc`), whose exports check their own answers, at
  each level, at `-O3`, with `-fexact-trap`, without the register
  allocator, without each of several passes, in every collector mode (`run_gc_modes.sh`) and with the
  collector thread under load (`run_gcthread.sh`); relaxed SIMD and `-ffma`
  (`test/cases_relaxed`, `cases_fma`) check their own answers too
- that every spelling of an `-O` level compiles to the same bytes
  (`test/run_levels.sh`) and the thread count never reaches the output
  (`test/run_jobs.sh`)
- linking, shared libraries, register pressure, rejected modules, binary
  cases, calls between two instances with memories of their own by each
  route one takes into the other -- an import, a table, a reference, each
  also as a tail call -- with stack arguments, more results than registers
  and exceptions caught across them (`test/link/pin_a.wat`, `pin_b.wat`),
  another module's object cast in a module it reached each way a
  reference can cross a boundary (`test/gen_link_seal.py`), which `sealed`
  must not take the module for, a global of the class-initializer pattern
  the embedder sets (`test/link/once_x.wat`), which `once` must not take
  for one, function symbols named from the name section (`test/run_names.py`),
  the WAT assembler against other assemblers (`test/run_wat.py`), and
  a module without GC references compiling to the same bytes with and
  without load barriers (`test/run_nobar.sh`)
- the compiler under AddressSanitizer and ThreadSanitizer (`run_asan.sh`,
  `run_tsan.sh`)

Five phases run last, each alone, because they need a quiet machine or would
load it for the others: the collector's pause check; the Go- and GHC-shaped
rewrites' modules and random programs on a 64-bit memory past 4 GiB
(`test/gen_mem64.py`, against `-O0` and wasmtime), with the Mach-O row; the
corpora with `-fexact-trap`; the official testsuite at the levels
`make check-spec` runs; and the pass combinations.

`test/run_shared.sh` builds a shared library and calls it from C, C++,
Python's `ctypes` and Java's foreign function API. The Java part needs Java 22
or newer, and skips itself otherwise.

Tools outside `make check`: `test/smith.py` (wasm-smith's modules against
wasmtime), `test/gen_gosp.py --seed N --count N` (more Go-shaped modules),
`test/refbytes.py` (objects byte for byte against a recorded manifest),
`test/bench.py` (warmc across levels and passes), `test/engines.py` (warmc
against other wasm engines), `test/sizes.py` (output sizes) and
`test/scale.py` (whether compile time grows linearly).

Requirements:
- `make check` needs `python3`, `perl` and wabt (`wat2wasm`, `wasm-interp`).
  It also needs clang's `wasm32` target with `wasm-ld`.
- Four dependencies are pinned in the Makefile and cached under `build/`:
  `wasm-tools` (built with `cargo` by `make wasm-tools`), the spec testsuite
  (`make testsuite`), wasi-sdk, the clang and sysroot the C and C++ corpora
  are built with (`make wasi-sdk`), and TeaVM, the compiler and class library
  the Java corpora are built with (`make teavm`, which needs a JDK, and
  applies `test/teavm/patches`). A fifth,
  the Kotlin/Wasm stdlib (`make kotlin-stdlib`), is cached there too, in the
  version your `kotlinc` reports. Each cached copy is stamped with its
  version, so bumping a pin or upgrading `kotlinc` fetches it again.
  `make check` brings all five up to date by itself, and `make check-spec`
  the testsuite. A sixth, Temurin, the JDK the Java corpus's native column
  runs (`make temurin`), only `make bench-record` fetches.
- Rows that need wasmtime, TinyGo, Go, Rust (`wasm32-wasip1` / `wasm32-wasip2`),
  Zig, `asc`, `kotlinc-wasm`, `wasm32-wasi-ghc` or a JDK (`javac` 21 or later,
  for the Java corpora) skip themselves when the tool is missing. GHC's wasm backend is not fetched by the Makefile: install it
  with ghc-wasm-meta's `bootstrap.sh` (the `hsbench` corpus is recorded with
  `FLAVOUR=9.14`), which puts it in `~/.ghc-wasm`, where `test/gobench.py`
  finds it without `~/.ghc-wasm/env` being sourced.

---

## Repository layout

```
src/            the compiler
  main.c          command line
  util.c          allocation, diagnostics, byte buffers, object symbol tables, the worker pool and the environment
  *.h             warmc.h (the module and object model) and each part's interface
  parse.c validate.c wat.c opcodes.c      front end
  component.c canon.c clink.c wit.c witgen.c   Component Model and WIT
  ir.c ir_build.c opt.c inline.c          SSA IR and optimiser
  tramp.c                                 a GHC-style trampoline rewritten as tail calls
  gosp.c                                  Go's stack pointer passed as an argument
  isel*.c isel_*.inc regalloc.inc         instruction selection and register allocation
  a64.c x64.c                             machine-code encoders
  codegen.c compile.c module_data.c       per-function emission, parallel driver, module descriptor
  elf.c macho.c coff.c pe.c link.c        object writers and linker
  shlib.c                                 --shared: the C entry points, header and .def
rt/             the runtime linked into every program and shared library
  rt.c            instantiation, traps, GC, exceptions, CLI harness
  start.S start_x64.S                     entry, invoke, throw and GC trampolines
  lib.c                                   a shared library's face: the C calls, its lock and its traps
  hook.c gcthook.c                        defaults that a driver, or the GC thread tests, replace
  gcthtest.c                              the GC thread tests' replacement
  modules.c component.c                   the descriptor tables of a program that is not a component
  wasi.c wasi.wat                         WASI preview 1 host
  wasifs.c wasifs.h                       the filesystem of both WASI hosts
  wasip2.c                                WASI 0.2 host
  linuxposix.c                            the WASI hosts' POSIX calls over Linux syscalls
  win.inc winposix.c                      Windows platform layer
  warm_rt.h                               the embedding API
  const_ops.inc                           the constant-expression opcodes, shared with the compiler
test/           test drivers (run_all.sh is make check), cases, benchmark and test corpora, the spec harness (spec/), TeaVM's driver and WASI adapter (teavm/), the suites' cache (testcache.py, row.py) and the caching wasmtime, tinygo, go and rustc make check runs (shims/)
lwjgl/          LWJGL 3 programs through TeaVM and warmc: the platform layer (platform/), the TeaVM transformer (teavm/), the C glue (native/), the build driver and a demo
example/        TinyGo and Kotlin sample programs (source, .wasm and a macOS build)
Makefile        the build, the pinned dependencies and the test targets
.github/        CI: make smoke on pushes to main and on pull requests, make check nightly
```

---

## Limitations

- **x86-64 is supported for Windows only.** Linux and macOS output is AArch64.
- **Native imports are AArch64's alone,** and `lwjgl/` is macOS's alone and
  has LWJGL's core, GLFW and OpenGL bindings alone.
- **No wasm threads.** Shared memories and atomics are rejected. (The
  runtime itself has a collector thread, and a library takes calls from
  several threads.)
- **No WASI 0.2 on Windows.** On Windows, the preview 1 host has no
  sockets, so `--tcplisten` fails there.
- **WASI 0.2** has no `wasi:sockets` or `wasi:http`, and implements only part
  of the interfaces it has (see [WASI 0.2 command
  components](#wasi-02-command-components)); any other `wasi:*` call traps.
- **Components** compile only when every import is WASI 0.2 and every
  non-`run` export uses only 32-bit and 64-bit integers. The canonical ABI is
  32-bit only. The async, threading and error-context built-ins are refused,
  as is an adapter between two sides with different string encodings. A
  component cannot be built `--shared`, and component text must be assembled
  with `wasm-tools`.
- **Tables** hold at most 2^20 entries, without a maximum as with one.
- **Memories:** a 32-bit one holds at most 65535 pages, one short of 4 GiB, a
  64-bit one 2^26 pages (4 TiB), or 2^20 (64 GiB) when it declares no maximum.
- **A function's frame** is at most 1 MiB on AArch64 and 128 MiB on x86-64.
- **Tail calls:** a module that makes one is refused if any of its function
  types takes more than 512 bytes of stack arguments -- more than 32
  parameters past the registers on AArch64, or 64 on x86-64 -- as a function
  the host calls has room for no more.
- **Guarded memories:** at most 1024 per process (see [Embedding compiled
  modules in C](#embedding-compiled-modules-in-c)).
- **`--system-ld`** does not link Windows output.
- **Windows executables** carry no symbol table, so there the name section's
  names reach only a COFF object (`-c`).
- **Exceptions** do not propagate through host (C) frames.
- **The collector** does not run while wasm is re-entered from a host call.
- **Instances** are never freed, and a process holds at most 4096 of them.
- **A shared library** holds one instance. Calls from several threads take
  turns on it, and its imports cannot be rebound once it has been made. It
  imports only functions, each of which C must be able to stand in for; on
  Windows an imported function takes at most three parameters.
