# Embedding the JIT

Hosts of libwarmc, the JIT, in ten languages. Each one compiles the module
[`guest.wat`](guest.wat) in its own process, through the standard
WebAssembly C API (`include/wasm.h`) and warmc's additions
(`include/warm.h`). It gives the module a host function written in the host's
language, `host.log`, which reads a string out of the guest's memory. It then
calls the guest's exports: `greet`, which calls `host.log`; `fib`; `div`,
which traps when it divides by zero; and `peek`, which traps when it loads
past the end of the guest's one page of memory. That last trap is a fault on
the guard pages beyond the memory, which libwarmc's signal handler turns into
a wasm trap beside whatever the host language's runtime does with signals.
Every host prints the same lines, [`expected.out`](expected.out):

```
log: Hello from wasm!
fib(90) = 2880067194370816120
div(7, 2) = 3
div(1, 0) trapped: integer divide by zero
peek(16) = 72
peek(65536) trapped: out of bounds memory access
```

Build the library first, from the top of the tree: `make jit` makes
`build/libwarmc.dylib` on macOS, `build/libwarmc.so` on Linux, and
`build/libwarmc.dll` with its import library `build/libwarmc.dll.a` on
Windows. The commands below are macOS's, run from the top of the tree unless
they say otherwise; on Linux use `libwarmc.so`, and on Windows link
`build/libwarmc.dll.a` and put `build/` on the `PATH`.

| Language | Host | How it calls C | Needs |
|---|---|---|---|
| C | [`c/host.c`](c/host.c) | `wasm.h` itself | a C compiler |
| C++ | [`cpp/host.cpp`](cpp/host.cpp) | `wasm.h`, each object owned by a `std::unique_ptr`, the host function a lambda | C++17 |
| Java | [`java/Host.java`](java/Host.java) | the foreign function API (`java.lang.foreign`), the host function an upcall stub | Java 22 or later |
| Kotlin | [`kotlin/Host.kt`](kotlin/Host.kt) | the JVM's foreign function API, as Java's | Kotlin for the JVM, Java 22 or later |
| Haskell | [`haskell/Host.hs`](haskell/Host.hs) | GHC's FFI, the host function a `wrapper`, `wasm_func_call` a `safe` call so that wasm can call back into Haskell | GHC |
| Go | [`go/`](go/) | cgo; the host function is a Go function `//export`ed to a C callback | Go |
| Node.js | [`node/host.mjs`](node/host.mjs) | `node:ffi` (experimental), the host function a registered callback | Node 26 or later |
| Bun | [`bun/host.js`](bun/host.js) | `bun:ffi`, the host function a `JSCallback` | Bun |
| Zig | [`zig/`](zig/) | `wasm.h` translated to Zig by `build.zig` (`addTranslateC`) | Zig 0.16 or later |
| Rust | [`rust/`](rust/) | `extern "C"` declarations and `#[repr(C)]` structs, `build.rs` linking the library | Rust (edition 2024) |

```sh
# C and C++
clang -Iinclude -o host example/jit/c/host.c -Lbuild -lwarmc -Wl,-rpath,build
./host example/jit/guest.wat
clang++ -std=c++17 -Iinclude -o host example/jit/cpp/host.cpp -Lbuild -lwarmc -Wl,-rpath,build
./host example/jit/guest.wat

# Java, as a single-file program, and Kotlin
java --enable-native-access=ALL-UNNAMED example/jit/java/Host.java build/libwarmc.dylib example/jit/guest.wat
kotlinc example/jit/kotlin/Host.kt -include-runtime -d host.jar
java --enable-native-access=ALL-UNNAMED -jar host.jar build/libwarmc.dylib example/jit/guest.wat

# Haskell
ghc -O -outputdir build/hs -o host example/jit/haskell/Host.hs -Lbuild -lwarmc -optl-Wl,-rpath,build
./host example/jit/guest.wat

# Go
(cd example/jit/go && go build -o host . && ./host ../guest.wat)

# Node.js and Bun
node --no-warnings example/jit/node/host.mjs build/libwarmc.dylib example/jit/guest.wat
bun example/jit/bun/host.js build/libwarmc.dylib example/jit/guest.wat

# Zig and Rust
(cd example/jit/zig && zig build -Doptimize=ReleaseSafe && zig-out/bin/host ../guest.wat)
(cd example/jit/rust && cargo run --release -- ../guest.wat)
```

`test/run_jit_examples.py`, a row of `make check`, builds and runs each host
whose toolchain is installed, and checks what it prints against
`expected.out`, on macOS and Linux; on Windows it runs the C and C++ hosts
alone, the only ones tried there.
