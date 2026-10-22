// Structs passed and returned by value: a 3-vector, a 3x3 matrix and a
// particle record handed to small functions and handed back, the way C
// geometry and physics code reads when it prefers values to out-pointers.
//
// The wasm C ABI passes any struct of more than one scalar through memory.
// The caller copies each argument into its own shadow-stack frame -- the f64
// fields as i64.load/i64.store pairs, the 64- and 72-byte structs as
// memory.copy -- and passes the copy's address; a returned struct is written
// through a hidden pointer into a slot the caller reserved, then read back out
// of it.  `step` alone keeps a 608-byte frame for its temporaries.  The
// functions are noinline and external, as they would be in another translation
// unit, so LLVM cannot rewrite the convention away.  gobench/bigstruct is one
// large struct and a memory.copy per call; this is many small ones, and the row
// measures whether stores into a frame are forwarded to the loads that read
// them back.
#include <stdio.h>
#include <stdint.h>

typedef struct { double x, y, z; } V3;
typedef struct { V3 r0, r1, r2; } M3;
typedef struct { V3 pos, vel; double mass; uint32_t id, hits; } Particle;
typedef struct { uint32_t lo, hi; } Pair;

#define CALL __attribute__((noinline))

CALL V3 v3add(V3 a, V3 b) { return (V3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
CALL V3 v3scale(V3 a, double s) { return (V3){ a.x * s, a.y * s, a.z * s }; }
CALL double v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CALL V3 v3cross(V3 a, V3 b) {
  return (V3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
CALL V3 m3apply(M3 m, V3 v) { return (V3){ v3dot(m.r0, v), v3dot(m.r1, v), v3dot(m.r2, v) }; }
CALL M3 m3transpose(M3 m) {
  return (M3){ { m.r0.x, m.r1.x, m.r2.x }, { m.r0.y, m.r1.y, m.r2.y }, { m.r0.z, m.r1.z, m.r2.z } };
}
CALL M3 m3mul(M3 a, M3 b) {
  M3 t = m3transpose(b);
  return (M3){ m3apply(t, a.r0), m3apply(t, a.r1), m3apply(t, a.r2) };
}

// A spring towards the origin, a force across the velocity, a rotation of the
// velocity, and a bounce off a sphere.  Every intermediate is a struct value.
CALL Particle step(Particle p, M3 rot, V3 axis, double dt) {
  V3 f = v3add(v3scale(p.pos, -0.5), v3cross(axis, p.vel));
  p.vel = m3apply(rot, v3add(p.vel, v3scale(f, dt / p.mass)));
  p.pos = v3add(p.pos, v3scale(p.vel, dt));
  if (v3dot(p.pos, p.pos) > 4.0) {
    p.vel = v3scale(p.vel, -0.98);
    p.hits++;
  }
  return p;
}

CALL Pair tally(Pair acc, Particle p) {
  acc.lo = acc.lo * 31u + p.hits + p.id;
  acc.hi ^= (uint32_t)(int32_t)(p.pos.x * 1000.0) + (acc.lo >> 7);
  return acc;
}

enum { N = 256, ROUNDS = 6000 };
static Particle ps[N];

int main(void) {
  uint32_t x = 2463534242u;
  for (int i = 0; i < N; i++) {
    double c[6];
    for (int k = 0; k < 6; k++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      c[k] = (double)(x % 2001) / 1000.0 - 1.0;
    }
    ps[i] = (Particle){ { c[0], c[1], c[2] }, { c[3], c[4], c[5] }, 1.0 + (i & 7) * 0.25, (uint32_t)i, 0 };
  }
  // A rotation by 0.01 radians about z, and a tilt about x applied to it now and then.
  const double co = 0.9999500004166653, si = 0.009999833334166664;
  M3 rot = { { co, -si, 0 }, { si, co, 0 }, { 0, 0, 1 } };
  const M3 tilt = { { 1, 0, 0 }, { 0, co, -si }, { 0, si, co } };
  V3 axis = { 0.1, 0.2, 0.3 };
  Pair acc = { 1, 0 };
  for (int r = 0; r < ROUNDS; r++) {
    for (int i = 0; i < N; i++) ps[i] = step(ps[i], rot, axis, 0.01);
    if ((r & 31) == 31) {
      rot = m3mul(rot, tilt);
      axis = v3cross(axis, (V3){ 0.0, 0.0, 1.0 });
      axis = v3add(axis, (V3){ 0.1, 0.2, 0.3 });
      for (int i = 0; i < N; i += 16) acc = tally(acc, ps[i]);
    }
  }
  double e = 0;
  for (int i = 0; i < N; i++) {
    acc = tally(acc, ps[i]);
    e += v3dot(ps[i].vel, ps[i].vel) * ps[i].mass + v3dot(ps[i].pos, ps[i].pos);
  }
  printf("e = %.9f acc = %08x%08x\n", e, acc.hi, acc.lo);
  return 0;
}
