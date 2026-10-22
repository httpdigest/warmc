// memory.fill, through Uint8Array#fill and through the builtin directly.  A
// sieve of Eratosthenes over a 64 KiB byte array is reset with fill() before
// every run -- AssemblyScript lowers a one-byte fill to a single memory.fill
// after clamping start and end -- and a second memory.fill, at an odd offset
// and an odd length, writes about half of another buffer each round.  The
// lengths change from round to round, so neither is a constant-size fill a
// consumer could expand inline.
//
// Between the fills is the sieve itself: a strided byte store per composite,
// then a byte-load count.  Those loops reach the array through a module global,
// and as binaryen leaves them every element access re-reads the global, spills
// it to the shadow stack and reloads dataStart -- invariant work that only the
// consumer's loop-invariant code motion takes out.
import { print } from "../wasi";

const MAXN = 1 << 16;
let flags = new Uint8Array(MAXN);
let scratch = new Uint8Array(MAXN);

function sieve(n: i32): i32 {
  flags.fill(1, 0, n);
  unchecked(flags[0] = 0);
  unchecked(flags[1] = 0);
  for (let p = 2; p * p < n; p++) {
    if (unchecked(flags[p])) {
      // Clearing a range of multiples is a strided store, not a fill.
      for (let q = p * p; q < n; q += p) unchecked(flags[q] = 0);
    }
  }
  let c = 0;
  for (let i = 0; i < n; i++) c += unchecked(flags[i]);
  return c;
}

let total = 0;
for (let r = 0; r < 1750; r++) {
  let n = MAXN - (r * 37 & 4095);
  total += sieve(n);
  // The builtin directly, at an odd offset and an odd length.
  memory.fill(scratch.dataStart + <usize>(r & 15), <u8>r, <usize>(n >> 1) + 3);
  total += <i32>scratch[(r * 13) & 1023];
}
print("total = " + total.toString());
