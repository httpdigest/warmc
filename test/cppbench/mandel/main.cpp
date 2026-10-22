// The Mandelbrot set in std::complex<double>: z = z * z + c until |z|^2
// passes 4, over grids that straddle the set's boundary, with a smoothed
// escape count from std::abs and std::log for every point that escapes.
//
// std::complex is not the two-multiply, one-add loop it looks like.  libc++
// hands operator* to the compiler's own complex multiply, so z * z lowers as
// C's _Complex does under Annex G: the four products inline, then a test of
// both halves of the result for NaN that, if both are, calls compiler-rt's
// __muldc3 with an out-parameter in a 16-byte frame on the shadow stack -- a
// branch per iteration that is never taken here but has to be compiled
// around.  std::norm tests both halves for infinity with f64.abs before it
// squares, and each escape is a call to libm's hypot and two to log, all
// compiled into the module.  The row is f64 arithmetic carried in a pair of
// locals, with the library's IEEE caution as control flow around it.
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>

namespace {

__attribute__((noinline)) double escape(std::complex<double> c, int limit) {
  std::complex<double> z = 0;
  for (int i = 0; i < limit; i++) {
    z = z * z + c;
    if (std::norm(z) > 4.0) {
      // Smooth iteration count: fractional part from how far past 2 |z| got.
      return i + 1 - std::log(std::log(std::abs(z))) / std::log(2.0);
    }
  }
  return 0;
}

}  // namespace

int main() {
  constexpr int W = 320, H = 240, Limit = 400;
  double sum = 0;
  uint64_t inside = 0;
  for (int frame = 0; frame < 3; frame++) {
    std::complex<double> centre(-0.7453 + frame * 0.0005, 0.1127);
    double span = frame == 0 ? 2.8 : (frame == 1 ? 0.02 : 0.3);
    for (int py = 0; py < H; py++) {
      for (int px = 0; px < W; px++) {
        std::complex<double> c = centre + std::complex<double>((px - W / 2) * span / W,
                                                               (py - H / 2) * span / W);
        double e = escape(c, Limit);
        if (e == 0) inside++;
        sum += e;
      }
    }
  }
  std::printf("sum = %.10e inside = %llu\n", sum, static_cast<unsigned long long>(inside));
  return 0;
}
