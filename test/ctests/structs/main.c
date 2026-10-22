// Structs as values: assignment and copy of small and large structs, passing
// and returning them by value (wasm32's C ABI passes a struct through memory
// and returns one through a hidden pointer the caller provides, except a
// single scalar member which travels as the scalar), nested structs, arrays
// inside structs copied as part of the whole, structs of mixed-width members
// with padding, sizeof and offsetof, arrays of structs sorted and searched by
// a field, and a struct-heavy little geometry and matrix library.
//
// clang lowers a struct copy to memcpy or to a run of loads and stores
// depending on its size, and SROA splits small structs into scalars; both
// paths are hit here, with the results printed member by member.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { int32_t x, y; } Vec2;
typedef struct { double x, y, z; } Vec3;
typedef struct { float m[4][4]; } Mat4;
typedef struct { char c; double d; short s; int i; char tail[3]; } Padded;
typedef struct { uint8_t a; uint64_t b; uint8_t c; } Gappy;
typedef struct { int32_t only; } Wrapped;
typedef struct { float f; } WrappedF;
typedef struct { char name[16]; int age; double score; Vec2 pos; } Person;
typedef struct Node { int v; struct Node *next; } Node;
typedef struct { Vec2 min, max; } Rect;
typedef struct { int len; int data[32]; } Bag;

__attribute__((noinline)) static Vec2 v2add(Vec2 a, Vec2 b) {
  return (Vec2){a.x + b.x, a.y + b.y};
}
__attribute__((noinline)) static Vec2 v2rot(Vec2 a) { return (Vec2){-a.y, a.x}; }
__attribute__((noinline)) static Vec3 cross(Vec3 a, Vec3 b) {
  Vec3 r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  return r;
}
__attribute__((noinline)) static double dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
__attribute__((noinline)) static Mat4 matmul(Mat4 a, Mat4 b) {
  Mat4 r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
      r.m[i][j] = s;
    }
  return r;
}
__attribute__((noinline)) static Wrapped wrap(int v) { return (Wrapped){v * 3}; }
__attribute__((noinline)) static WrappedF wrapf(float v) { return (WrappedF){v / 4}; }
__attribute__((noinline)) static int unwrap(Wrapped w, WrappedF f) {
  return w.only + (int)f.f;
}
__attribute__((noinline)) static Bag bag_push(Bag b, int v) {
  if (b.len < 32) b.data[b.len++] = v;
  return b;
}
__attribute__((noinline)) static Rect rect_union(Rect a, Rect b) {
  Rect r;
  r.min.x = a.min.x < b.min.x ? a.min.x : b.min.x;
  r.min.y = a.min.y < b.min.y ? a.min.y : b.min.y;
  r.max.x = a.max.x > b.max.x ? a.max.x : b.max.x;
  r.max.y = a.max.y > b.max.y ? a.max.y : b.max.y;
  return r;
}
__attribute__((noinline)) static void modify_copy(Person p) {
  p.age = -1;
  strcpy(p.name, "changed");
}
__attribute__((noinline)) static void modify_ptr(Person *p) {
  p->age += 10;
  p->pos = v2rot(p->pos);
}

static void layout(void) {
  printf("-- sizes and offsets\n");
  printf("Vec2 %zu Vec3 %zu Mat4 %zu Padded %zu Gappy %zu Wrapped %zu Person %zu"
         " Node %zu Rect %zu Bag %zu\n",
         sizeof(Vec2), sizeof(Vec3), sizeof(Mat4), sizeof(Padded),
         sizeof(Gappy), sizeof(Wrapped), sizeof(Person), sizeof(Node),
         sizeof(Rect), sizeof(Bag));
  printf("Padded: c=%zu d=%zu s=%zu i=%zu tail=%zu align=%zu\n",
         offsetof(Padded, c), offsetof(Padded, d), offsetof(Padded, s),
         offsetof(Padded, i), offsetof(Padded, tail), _Alignof(Padded));
  printf("Gappy: a=%zu b=%zu c=%zu align=%zu\n", offsetof(Gappy, a),
         offsetof(Gappy, b), offsetof(Gappy, c), _Alignof(Gappy));
  printf("Person: name=%zu age=%zu score=%zu pos=%zu pos.y=%zu\n",
         offsetof(Person, name), offsetof(Person, age), offsetof(Person, score),
         offsetof(Person, pos), offsetof(Person, pos.y));
  printf("Rect: max.x=%zu Bag: data[5]=%zu Mat4: m[2][3]=%zu\n",
         offsetof(Rect, max.x), offsetof(Bag, data[5]), offsetof(Mat4, m[2][3]));
}

static void values(void) {
  printf("-- pass and return by value\n");
  Vec2 a = {3, -4}, b = {10, 20};
  for (int i = 0; i < 6; i++) {
    a = v2add(a, b);
    b = v2rot(b);
    printf("a=(%d,%d) b=(%d,%d)\n", a.x, a.y, b.x, b.y);
  }
  Vec3 u = {1, 2, 3}, v = {-2, 0.5, 4};
  for (int i = 0; i < 5; i++) {
    Vec3 w = cross(u, v);
    printf("u x v = (%g,%g,%g) dot=%g\n", w.x, w.y, w.z, dot(u, v));
    u = v;
    v = w;
    v.x += 1;
  }
  Mat4 m, id;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      m.m[i][j] = (float)(i * 4 + j) / 8 - 1;
      id.m[i][j] = i == j;
    }
  Mat4 p = matmul(m, id);
  Mat4 q = matmul(m, m);
  Mat4 r = matmul(q, m);
  for (int i = 0; i < 4; i++)
    printf("row %d: %g %g %g %g | %g %g %g %g | %g %g %g %g\n", i, p.m[i][0],
           p.m[i][1], p.m[i][2], p.m[i][3], q.m[i][0], q.m[i][1], q.m[i][2],
           q.m[i][3], r.m[i][0], r.m[i][1], r.m[i][2], r.m[i][3]);
  for (int i = -2; i < 3; i++) printf("wrap %d -> %d\n", i, unwrap(wrap(i), wrapf((float)i * 10)));
  Bag bag = {0};
  for (int i = 0; i < 40; i += 3) bag = bag_push(bag, i * i);
  printf("bag len=%d:", bag.len);
  for (int i = 0; i < bag.len; i++) printf(" %d", bag.data[i]);
  printf("\n");
  Rect r1 = {{0, 0}, {5, 5}}, r2 = {{-3, 2}, {4, 9}};
  Rect ru = rect_union(r1, r2);
  printf("union (%d,%d)-(%d,%d)\n", ru.min.x, ru.min.y, ru.max.x, ru.max.y);
}

static void copies(void) {
  printf("-- copies are independent\n");
  Person p = {"alice", 30, 91.5, {1, 2}};
  Person q = p;
  modify_copy(p);
  modify_ptr(&q);
  printf("p=%s %d %.1f (%d,%d)\n", p.name, p.age, p.score, p.pos.x, p.pos.y);
  printf("q=%s %d %.1f (%d,%d)\n", q.name, q.age, q.score, q.pos.x, q.pos.y);
  Person arr[3] = {p, q, {"carol", 41, 77.25, {-5, 6}}};
  arr[0].name[0] = 'A';
  arr[1] = arr[2];
  arr[2].age = 99;
  for (int i = 0; i < 3; i++)
    printf("arr[%d]=%s %d %.2f (%d,%d)\n", i, arr[i].name, arr[i].age,
           arr[i].score, arr[i].pos.x, arr[i].pos.y);
  // struct swap through a temporary, inside an array
  for (int i = 0; i < 2; i++) {
    Person t = arr[i];
    arr[i] = arr[2 - i];
    arr[2 - i] = t;
  }
  for (int i = 0; i < 3; i++) printf("swapped[%d]=%s %d\n", i, arr[i].name, arr[i].age);
  Padded pa = {'x', 2.5, -7, 123456, {'a', 'b', 'c'}};
  Padded pb;
  memset(&pb, 0, sizeof pb);
  pb = pa;
  pb.s++;
  printf("pa %c %.2f %d %d %c%c%c | pb %c %.2f %d %d\n", pa.c, pa.d, pa.s, pa.i,
         pa.tail[0], pa.tail[1], pa.tail[2], pb.c, pb.d, pb.s, pb.i);
  Gappy g[4];
  for (int i = 0; i < 4; i++) g[i] = (Gappy){(uint8_t)(i + 1), (uint64_t)i << 40, (uint8_t)(200 + i)};
  for (int i = 0; i < 4; i++) printf("g[%d] %u %llu %u\n", i, g[i].a, (unsigned long long)g[i].b, g[i].c);
}

static void linked(void) {
  printf("-- linked nodes on the stack\n");
  Node nodes[10];
  for (int i = 0; i < 10; i++) {
    nodes[i].v = i * i - 20;
    nodes[i].next = i + 1 < 10 ? &nodes[i + 1] : NULL;
  }
  // reverse the list in place
  Node *prev = NULL, *cur = &nodes[0];
  while (cur) {
    Node *n = cur->next;
    cur->next = prev;
    prev = cur;
    cur = n;
  }
  for (Node *p = prev; p; p = p->next) printf("%d ", p->v);
  printf("\n");
  int idx = 0;
  for (Node *p = prev; p; p = p->next) idx = idx * 3 + (int)(p - nodes);
  printf("index hash %d\n", idx);
}

int main(void) {
  layout();
  values();
  copies();
  linked();
  return 0;
}
