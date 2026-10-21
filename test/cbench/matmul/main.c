// Dense double-precision matrix multiply, i-k-j order so the inner loop is a
// contiguous multiply-add over a row: the loop LLVM vectorises when it may and
// the one every numerical C program is built around.
//
// gobench/matrix and rsbench/matrix are the same algorithm through other
// producers; C with restrict-free pointers is the version whose aliasing the
// compiler knows least about.
#include <stdio.h>
#include <stdint.h>

enum { N = 96 };
static double a[N][N], b[N][N], c[N][N];

int main(void) {
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      a[i][j] = (double)((i * 7 + j * 3) % 17) - 8.0;
      b[i][j] = (double)((i * 5 + j * 11) % 13) * 0.5;
    }
  double trace = 0;
  for (int r = 0; r < 240; r++) {
    for (int i = 0; i < N; i++)
      for (int j = 0; j < N; j++) c[i][j] = 0;
    for (int i = 0; i < N; i++)
      for (int k = 0; k < N; k++) {
        double aik = a[i][k];
        for (int j = 0; j < N; j++) c[i][j] += aik * b[k][j];
      }
    for (int i = 0; i < N; i++) trace += c[i][(i + r) % N];
    a[r % N][(r * 3) % N] += 1.0;
  }
  printf("trace = %.6f\n", trace);
  return 0;
}
