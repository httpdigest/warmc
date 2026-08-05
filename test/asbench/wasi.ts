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

/** An f64 printed as its bits.
 *
 * Nothing in this corpus prints a float as digits.  `f64.toString` is Grisu
 * and would be the largest thing in some of these modules, and rounding to a
 * decimal string is exactly the step that would hide a one-ulp disagreement
 * between warmc and wasmtime -- which is the disagreement worth catching.
 */
export function bits(x: f64): string {
  return reinterpret<i64>(x).toString();
}
