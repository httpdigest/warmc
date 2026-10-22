// Shared by every program in test/astests (copied from test/asbench/wasi.ts,
// with the print helpers at the end added, and `bits` changed to print any
// NaN as "nan").  Unlike asbench, a few programs here do print floats as
// digits as well as bits, because the dtoa that does it is itself code under
// test there.
//
// Enough of WASI preview1 to print a line, and nothing else.
//
// AssemblyScript has no standalone-command target of its own: `asc` emits a
// module meant for a JavaScript host, so `console.log` is an import the host
// fills in.  There is an official `@assemblyscript/wasi-shim` package that
// redirects it to `fd_write`, but taking a node_modules dependency for two
// imports is a worse trade than declaring them, and declaring them keeps the
// import list in the compiled module down to exactly what these benchmarks
// use -- which is what makes `warmc --wasi` and `wasmtime` agree about the
// module without either of them supplying an `env` namespace.
//
// The other half of standing alone is `abort`.  Left alone, `asc` imports
// `env.abort`; the corpus is built with `--use abort=` instead, which turns a
// failed bounds check or assertion into a plain trap.  That is the better
// behaviour for a differential harness: a trap is something warmc and wasmtime
// can be compared on, and a message printed through an import that only one of
// them has is not.

@external("wasi_snapshot_preview1", "fd_write")
declare function fd_write(fd: i32, iovs: usize, iovsLen: i32, nwritten: usize): i32;

/** Write `s` and a newline to stdout. */
export function print(s: string): void {
  // AssemblyScript strings are UTF-16, so this encode is a real conversion and
  // a real allocation -- not a cast.  It is outside every kernel's timed loop.
  let buf = String.UTF8.encode(s + "\n");
  // One static iovec, reused: 8 bytes of {ptr, len} and 8 more for the
  // nwritten fd_write insists on being given somewhere to put.
  let iov = memory.data(16);
  store<u32>(iov, <u32>changetype<usize>(buf));
  store<u32>(iov + 4, <u32>buf.byteLength);
  fd_write(1, iov, 1, iov + 8);
}

/** An f64 printed as its bits -- or as "nan" if it is any NaN.
 *
 * Bits rather than digits because rounding to a decimal string is exactly the
 * step that would hide a one-ulp disagreement between warmc and wasmtime.
 *
 * Unlike asbench's copy, a NaN is printed as the word.  The wasm spec leaves
 * the sign and payload of a NaN that comes out of an arithmetic instruction
 * (add, sqrt, min, nearest, demote, ... and every library function built from
 * them) nondeterministic: x64 hardware makes negative NaNs where arm64 makes
 * positive ones, and an engine may propagate or canonicalise a payload.  So a
 * NaN's bits are only printed through `rawbits`, at the few places where the
 * spec does fix them.
 */
export function bits(x: f64): string {
  if (isNaN(x)) return "nan";
  return reinterpret<i64>(x).toString();
}

// What follows is added for test/astests, the correctness corpus, which prints
// many intermediate values rather than one checksum.

/** An f32 printed as its bits, or "nan", for the same reasons as `bits`. */
export function bits32(x: f32): string {
  if (isNaN(x)) return "nan";
  return reinterpret<i32>(x).toString();
}

/** An f64's exact bits, NaN sign and payload included.  Only for values whose
 * bits the spec fixes: constants, loads, reinterpret, select, lane moves, and
 * the sign-only operations neg, abs and copysign applied to those. */
export function rawbits(x: f64): string {
  return reinterpret<i64>(x).toString();
}

/** An f32's exact bits; the f32 counterpart of `rawbits`. */
export function rawbits32(x: f32): string {
  return reinterpret<i32>(x).toString();
}

/** An f64's bits for folding into a checksum, with every NaN mapped to the
 * one canonical pattern, so the checksum is host-independent. */
export function canon64(x: f64): u64 {
  return isNaN(x) ? 0x7ff8000000000000 : reinterpret<u64>(x);
}

/** An f32's bits for a checksum, every NaN mapped to 0x7fc00000. */
export function canon32(x: f32): u32 {
  return isNaN(x) ? 0x7fc00000 : reinterpret<u32>(x);
}

/** A u32 as eight hex digits, zero-padded. */
export function hex32(x: u32): string {
  return x.toString(16).padStart(8, "0");
}

/** A u64 as sixteen hex digits, zero-padded. */
export function hex64(x: u64): string {
  return x.toString(16).padStart(16, "0");
}
