// A five-point heat-diffusion stencil on a 96x96 grid of StaticArray<f64>,
// ping-ponged between two arrays, with a moving heat source.  binaryen keeps
// every check: each read and the write is a compare of the index against the
// byte length stored in the array's object header (shifted down by 3) and a
// trap, ahead of the f64.load or f64.store.  The neighbours are i - 1, i + 1,
// i - W and i + W, so a consumer that wants to prove those checks away has to
// reason about a two-dimensional index rather than a single induction
// variable.
//
// dotf64 and matrix go through Float64Array; this is the StaticArray route,
// whose length lives in the header rather than in a separate view object.  It
// is also the f64 kernel here whose loads overlap from one iteration to the
// next, which is what a consumer can keep in registers across them.
import { print, bits } from "../wasi";

const W = 96;
const H = 96;

function step(dst: StaticArray<f64>, src: StaticArray<f64>, c: f64): void {
  for (let y = 1; y < H - 1; y++) {
    for (let x = 1; x < W - 1; x++) {
      let i = y * W + x;
      dst[i] = src[i] + c * (src[i - 1] + src[i + 1] + src[i - W] + src[i + W] - 4.0 * src[i]);
    }
  }
}

let a = new StaticArray<f64>(W * H);
let b = new StaticArray<f64>(W * H);
for (let i = 0; i < W * H; i++) a[i] = <f64>((i * 7919) % 1000) * 0.001;

let probe: f64 = 0;
for (let r = 0; r < 3150; r++) {
  // A heat source that moves: one cell pinned per round.
  a[(r * 131) % (W * H)] = 1.0;
  step(b, a, 0.2);
  step(a, b, 0.2);
  probe += a[(W >> 1) * W + (r % W)];
}
let s: f64 = 0;
for (let i = 0; i < W * H; i++) s += a[i];
print("s = " + bits(s) + " p = " + bits(probe));
