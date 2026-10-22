// math.h's transcendental functions in a numeric loop: the response of a
// damped, driven oscillator over a grid of frequencies and times, with exp,
// sin and cos per point, then pow, log, log10, atan2 and tanh per frequency.
//
// wasm has instructions for sqrt, floor and the like but none for these, so
// each is a call into wasi-libc's musl libm, compiled into the module:
// argument reduction (sin and cos through __rem_pio2), a polynomial, and for
// exp an indexed lookup in a table of 2^(i/N).  musl keeps the polynomial
// coefficients in a constant struct, so even they are f64.loads from linear
// memory rather than immediates.  The calls sit on the critical path of an f64
// accumulation, so the row measures call overhead and the code of a library
// the program did not write.  gobench/mathfn covers the f64 operations that
// are single instructions; this is the rest of math.h.
#include <math.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
  // A damped, driven oscillator's response evaluated over a grid of
  // frequencies and times, with a log-power spectrum and a phase per point.
  enum { F = 2400, T = 2000 };
  double power = 0, phase = 0, peak = 0;
  for (int f = 0; f < F; f++) {
    double w = 0.05 + f * 0.0017;
    double gamma = 0.02 + 0.3 * pow(w, 0.7);
    double re = 0, im = 0;
    for (int k = 0; k < T; k++) {
      double tk = k * 0.031;
      double env = exp(-gamma * tk);
      double drive = sin(w * tk) + 0.25 * cos(3.0 * w * tk + 0.5);
      double y = env * drive;
      re += y * cos(w * tk);
      im += y * sin(w * tk);
      if (y > peak) peak = y;
    }
    double mag = re * re + im * im;
    power += log(1.0 + mag) + log10(1.0 + w);
    phase += atan2(im, re) + tanh(re * 0.01);
  }
  printf("power = %.12f phase = %.12f peak = %.12f\n", power, phase, peak);
  return 0;
}
