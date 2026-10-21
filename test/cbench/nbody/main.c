// The n-body simulation from the Benchmarks Game: an array of structs of
// doubles, a pairwise loop with a square root and a division per pair, and the
// velocities written back through the same pointers that were read.
//
// Floating-point latency bound, with the struct fields reached at constant
// offsets from a pointer that changes every iteration -- the shape the loads
// and stores of every physics or geometry kernel in C have.
#include <stdio.h>
#include <math.h>

typedef struct { double x, y, z, vx, vy, vz, m; } Body;

#define PI 3.141592653589793
#define SOLAR (4 * PI * PI)
#define DPY 365.24

static Body bodies[5] = {
  { 0, 0, 0, 0, 0, 0, SOLAR },
  { 4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
    1.66007664274403694e-03 * DPY, 7.69901118419740425e-03 * DPY, -6.90460016972063023e-05 * DPY,
    9.54791938424326609e-04 * SOLAR },
  { 8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
    -2.76742510726862411e-03 * DPY, 4.99852801234917238e-03 * DPY, 2.30417297573763929e-05 * DPY,
    2.85885980666130812e-04 * SOLAR },
  { 1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
    2.96460137564761618e-03 * DPY, 2.37847173959480950e-03 * DPY, -2.96589568540237556e-05 * DPY,
    4.36624404335156298e-05 * SOLAR },
  { 1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
    2.68067772490389322e-03 * DPY, 1.62824170038242295e-03 * DPY, -9.51592254519715870e-05 * DPY,
    5.15138902046611451e-05 * SOLAR },
};

static void advance(int n, double dt) {
  for (int i = 0; i < n; i++) {
    Body *a = &bodies[i];
    for (int j = i + 1; j < n; j++) {
      Body *b = &bodies[j];
      double dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
      double d2 = dx * dx + dy * dy + dz * dz;
      double mag = dt / (d2 * sqrt(d2));
      a->vx -= dx * b->m * mag; a->vy -= dy * b->m * mag; a->vz -= dz * b->m * mag;
      b->vx += dx * a->m * mag; b->vy += dy * a->m * mag; b->vz += dz * a->m * mag;
    }
  }
  for (int i = 0; i < n; i++) {
    bodies[i].x += dt * bodies[i].vx;
    bodies[i].y += dt * bodies[i].vy;
    bodies[i].z += dt * bodies[i].vz;
  }
}

static double energy(int n) {
  double e = 0;
  for (int i = 0; i < n; i++) {
    Body *a = &bodies[i];
    e += 0.5 * a->m * (a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
    for (int j = i + 1; j < n; j++) {
      Body *b = &bodies[j];
      double dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
      e -= a->m * b->m / sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

int main(void) {
  double px = 0, py = 0, pz = 0;
  for (int i = 0; i < 5; i++) {
    px += bodies[i].vx * bodies[i].m;
    py += bodies[i].vy * bodies[i].m;
    pz += bodies[i].vz * bodies[i].m;
  }
  bodies[0].vx = -px / SOLAR; bodies[0].vy = -py / SOLAR; bodies[0].vz = -pz / SOLAR;
  for (int i = 0; i < 1500000; i++) advance(5, 0.01);
  printf("e = %.9f\n", energy(5));
  return 0;
}
