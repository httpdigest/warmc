// Bitfields packed into a 32-bit word, inside a union that also reads the word
// whole: a table of game entities -- alive flag, kind, hit points, a 9-bit
// position, a signed 4-bit velocity -- updated one field at a time, the way C
// packs flags and small counters into device registers, protocol headers and
// saved state.
//
// clang reads an unsigned field as a shift and a mask, and a signed one as a
// shift left then an arithmetic shift right; a write is an and with the
// field's complement, an or and an i32.store of the containing word.  LLVM
// merges the updates it can prove nothing reads in between, but the neighbour
// read through another pointer might be the same word, so the entity's word
// still goes back to memory several times per step.  zigbench/bitpack is Zig's
// packed structs, which Zig lowers itself; these are C's, laid out by clang's
// ABI and left to the optimiser.
#include <stdio.h>
#include <stdint.h>

typedef union {
  uint32_t word;
  struct {
    unsigned alive : 1;
    unsigned kind : 3;
    unsigned hp : 10;
    unsigned x : 9;
    unsigned y : 9;
  } f;
} Entity;

typedef union {
  uint32_t word;
  struct {
    signed int vx : 4;
    signed int vy : 4;
    unsigned age : 12;
    unsigned team : 2;
    unsigned kills : 10;
  } m;
} Motion;

enum { N = 1 << 14, TICKS = 2000 };
static Entity ent[N];
static Motion mot[N];

static void tick(uint32_t t) {
  for (uint32_t i = 0; i < N; i++) {
    Entity *a = &ent[i];
    Motion *b = &mot[i];
    if (!a->f.alive) {
      if (((t + i) & 15) == 0) {
        a->f.alive = 1;
        a->f.hp = 1023 - (i & 255);
        b->m.age = 0;
      }
      continue;
    }
    // Unsigned fields wrap modulo their width; the signed velocity is sign
    // extended on every read.
    a->f.x += b->m.vx;
    a->f.y += b->m.vy;
    b->m.age++;
    // The neighbour's fields are read through the same union.
    const Entity *o = &ent[(i * 97 + t) & (N - 1)];
    unsigned dmg = ((a->f.x ^ o->f.y) & 7) + a->f.kind;
    if (o->f.alive && mot[(i * 97 + t) & (N - 1)].m.team == b->m.team) dmg = 0;
    if (a->f.hp <= dmg) {
      a->f.alive = 0;
      mot[(i * 97 + t) & (N - 1)].m.kills++;
    } else {
      a->f.hp -= dmg;
    }
    if ((b->m.age & 31) == 0) {
      int vx = b->m.vx, vy = b->m.vy;
      b->m.vx = vy;
      b->m.vy = -vx - (vx < 0);
      a->f.kind = (a->f.kind + 1) & 7;
    }
  }
}

int main(void) {
  uint32_t x = 88172645u;
  for (uint32_t i = 0; i < N; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    ent[i].word = x;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    mot[i].word = x & 0x003fffffu;
    if (mot[i].m.vx == 0) mot[i].m.vx = 1;
  }
  uint32_t h = 2166136261u;
  for (uint32_t t = 0; t < TICKS; t++) {
    tick(t);
    if ((t & 63) == 63)
      for (uint32_t i = 0; i < N; i += 7) h = (h ^ ent[i].word ^ (mot[i].word << 1)) * 16777619u;
  }
  uint32_t alive = 0, hp = 0, kills = 0;
  for (uint32_t i = 0; i < N; i++) {
    alive += ent[i].f.alive;
    hp += ent[i].f.hp;
    kills += mot[i].m.kills;
    h = (h ^ ent[i].word) * 16777619u;
  }
  printf("alive = %u hp = %u kills = %u h = %08x\n", alive, hp, kills, h);
  return 0;
}
