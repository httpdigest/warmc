// Arbitrary-precision unsigned integers on 32-bit limbs: addition and
// subtraction with carry and borrow chains, schoolbook multiplication with
// 64-bit partial products, short division by a word, long division (Knuth's
// algorithm D, with its normalisation shift and the rare add-back step),
// shifts by whole and partial limbs, comparison, conversion to and from
// decimal strings, and the results checked against identities: (a*b)/b == a,
// a == q*b + r with r < b, gcd(a,b) dividing both, and modular exponentiation
// confirming Fermat's little theorem for a known prime.
//
// The carry and borrow handling in these loops is the classic place where an
// optimiser that narrows a 64-bit intermediate, or mis-schedules a compare
// against the value before an add, produces numbers that are wrong only in
// their high limbs.  Factorials, powers and Fibonacci numbers of a few hundred
// digits are printed in full.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXL 160
typedef struct { int n; uint32_t d[MAXL]; } Big;  // little-endian limbs, no leading zeros

static void norm(Big *a) {
  while (a->n > 0 && a->d[a->n - 1] == 0) a->n--;
}
static void set_u64(Big *a, uint64_t v) {
  a->n = 0;
  while (v) {
    a->d[a->n++] = (uint32_t)v;
    v >>= 32;
  }
}
static int cmp(const Big *a, const Big *b) {
  if (a->n != b->n) return a->n < b->n ? -1 : 1;
  for (int i = a->n - 1; i >= 0; i--)
    if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
  return 0;
}
static void add(Big *r, const Big *a, const Big *b) {
  int n = a->n > b->n ? a->n : b->n;
  uint64_t carry = 0;
  for (int i = 0; i < n; i++) {
    uint64_t s = carry + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
    r->d[i] = (uint32_t)s;
    carry = s >> 32;
  }
  r->n = n;
  if (carry) r->d[r->n++] = (uint32_t)carry;
}
static void sub(Big *r, const Big *a, const Big *b) {  // requires a >= b
  int64_t borrow = 0;
  for (int i = 0; i < a->n; i++) {
    int64_t s = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
    borrow = s < 0;
    r->d[i] = (uint32_t)(s + (borrow << 32));
  }
  r->n = a->n;
  norm(r);
}
static void mul(Big *r, const Big *a, const Big *b) {
  Big t;
  memset(t.d, 0, sizeof t.d);
  for (int i = 0; i < a->n; i++) {
    uint64_t carry = 0;
    for (int j = 0; j < b->n; j++) {
      uint64_t p = (uint64_t)a->d[i] * b->d[j] + t.d[i + j] + carry;
      t.d[i + j] = (uint32_t)p;
      carry = p >> 32;
    }
    t.d[i + b->n] = (uint32_t)carry;
  }
  t.n = a->n + b->n;
  norm(&t);
  *r = t;
}
static uint32_t divsmall(Big *q, const Big *a, uint32_t v) {
  uint64_t rem = 0;
  for (int i = a->n - 1; i >= 0; i--) {
    uint64_t cur = (rem << 32) | a->d[i];
    q->d[i] = (uint32_t)(cur / v);
    rem = cur % v;
  }
  q->n = a->n;
  norm(q);
  return (uint32_t)rem;
}
static void shl(Big *r, const Big *a, int bits) {
  int limbs = bits / 32, s = bits % 32;
  Big t;
  memset(t.d, 0, sizeof t.d);
  for (int i = 0; i < a->n; i++) {
    t.d[i + limbs] |= a->d[i] << s;
    if (s) t.d[i + limbs + 1] |= a->d[i] >> (32 - s);
  }
  t.n = a->n + limbs + 1;
  norm(&t);
  *r = t;
}
static void shr(Big *r, const Big *a, int bits) {
  int limbs = bits / 32, s = bits % 32;
  Big t;
  t.n = a->n - limbs;
  if (t.n <= 0) {
    r->n = 0;
    return;
  }
  for (int i = 0; i < t.n; i++) {
    uint32_t lo = a->d[i + limbs] >> s;
    uint32_t hi = (s && i + limbs + 1 < a->n) ? a->d[i + limbs + 1] << (32 - s) : 0;
    t.d[i] = lo | hi;
  }
  norm(&t);
  *r = t;
}
static int addbacks;
// Knuth algorithm D: q = a / b, r = a % b, b nonzero
static void divmod(Big *q, Big *r, const Big *a, const Big *b) {
  if (cmp(a, b) < 0) {
    q->n = 0;
    *r = *a;
    return;
  }
  if (b->n == 1) {
    uint32_t rem = divsmall(q, a, b->d[0]);
    set_u64(r, rem);
    return;
  }
  int s = __builtin_clz(b->d[b->n - 1]);
  Big u, v;
  shl(&v, b, s);
  shl(&u, a, s);
  if (u.n == a->n) u.d[u.n++] = 0;  // ensure an extra top limb
  int n = v.n, m = u.n - n;
  Big qq;
  memset(qq.d, 0, sizeof qq.d);
  for (int j = m - 1; j >= 0; j--) {
    uint64_t num = ((uint64_t)u.d[j + n] << 32) | u.d[j + n - 1];
    uint64_t qhat = num / v.d[n - 1], rhat = num % v.d[n - 1];
    while (qhat >= (1ull << 32) || qhat * v.d[n - 2] > ((rhat << 32) | u.d[j + n - 2])) {
      qhat--;
      rhat += v.d[n - 1];
      if (rhat >= (1ull << 32)) break;
    }
    int64_t borrow = 0;
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
      uint64_t p = qhat * v.d[i] + carry;
      carry = p >> 32;
      int64_t t = (int64_t)u.d[i + j] - (int64_t)(uint32_t)p - borrow;
      borrow = t < 0;
      u.d[i + j] = (uint32_t)(t + (borrow << 32));
    }
    int64_t t = (int64_t)u.d[j + n] - (int64_t)carry - borrow;
    u.d[j + n] = (uint32_t)t;
    if (t < 0) {  // add back
      addbacks++;
      qhat--;
      uint64_t c = 0;
      for (int i = 0; i < n; i++) {
        uint64_t s2 = (uint64_t)u.d[i + j] + v.d[i] + c;
        u.d[i + j] = (uint32_t)s2;
        c = s2 >> 32;
      }
      u.d[j + n] += (uint32_t)c;
    }
    qq.d[j] = (uint32_t)qhat;
  }
  qq.n = m;
  norm(&qq);
  *q = qq;
  u.n = n;
  norm(&u);
  shr(r, &u, s);
}
static void to_dec(const Big *a, char *out) {
  Big t = *a;
  char tmp[MAXL * 10 + 2];
  int k = 0;
  if (t.n == 0) tmp[k++] = '0';
  while (t.n > 0) {
    uint32_t rem = divsmall(&t, &t, 1000000000u);
    for (int i = 0; i < 9; i++) {
      tmp[k++] = (char)('0' + rem % 10);
      rem /= 10;
      if (t.n == 0 && rem == 0) break;
    }
  }
  int len = 0;
  while (k) out[len++] = tmp[--k];
  out[len] = 0;
}
static void from_dec(Big *a, const char *s) {
  a->n = 0;
  Big ten, digit;
  set_u64(&ten, 10);
  for (; *s; s++) {
    mul(a, a, &ten);
    set_u64(&digit, (uint64_t)(*s - '0'));
    add(a, a, &digit);
  }
}
static void print(const char *label, const Big *a) {
  static char buf[MAXL * 10 + 2];
  to_dec(a, buf);
  printf("%s (%zu digits) = %s\n", label, strlen(buf), buf);
}

int main(void) {
  Big a, b, c, q, r, t;
  printf("-- factorials\n");
  set_u64(&a, 1);
  for (int i = 1; i <= 120; i++) {
    Big f;
    set_u64(&f, (uint64_t)i);
    mul(&a, &a, &f);
    if (i % 20 == 0) {
      char label[16];
      snprintf(label, sizeof label, "%d!", i);
      print(label, &a);
    }
  }
  printf("-- powers and fibonacci\n");
  set_u64(&b, 1);
  set_u64(&t, 3);
  for (int i = 1; i <= 400; i++) {
    mul(&b, &b, &t);
    if (i % 100 == 0) {
      char label[16];
      snprintf(label, sizeof label, "3^%d", i);
      print(label, &b);
    }
  }
  Big f0, f1;
  set_u64(&f0, 0);
  set_u64(&f1, 1);
  for (int i = 1; i <= 500; i++) {
    add(&c, &f0, &f1);
    f0 = f1;
    f1 = c;
  }
  print("fib(501)", &f1);
  printf("-- division identities\n");
  for (int k = 0; k < 8; k++) {
    // dividend: a mix of the factorial and powers; divisor of varying length
    Big num, den;
    shr(&num, &a, k * 37);
    add(&num, &num, &b);
    set_u64(&den, 0x9e3779b97f4a7c15ull >> (k * 7));
    for (int m = 0; m < k; m++) mul(&den, &den, &den);
    divmod(&q, &r, &num, &den);
    Big back;
    mul(&back, &q, &den);
    add(&back, &back, &r);
    char qs[MAXL * 10 + 2], rs[MAXL * 10 + 2];
    to_dec(&q, qs);
    to_dec(&r, rs);
    printf("k=%d limbs %d/%d: q has %zu digits, q ends ...%s, r ends ...%s, identity=%d r<den=%d\n", k, num.n,
           den.n, strlen(qs), qs + (strlen(qs) > 12 ? strlen(qs) - 12 : 0), rs + (strlen(rs) > 12 ? strlen(rs) - 12 : 0),
           cmp(&back, &num) == 0, cmp(&r, &den) < 0);
  }
  // (x*y)/y == x and (x*y) % y == 0
  for (int k = 1; k <= 6; k++) {
    Big x, y, p;
    shr(&x, &b, k * 50);
    shr(&y, &a, 300 + k * 20);
    mul(&p, &x, &y);
    divmod(&q, &r, &p, &y);
    printf("roundtrip %d: x limbs %d y limbs %d -> q==x %d r==0 %d\n", k, x.n, y.n, cmp(&q, &x) == 0, r.n == 0);
  }
  printf("addbacks so far: %d\n", addbacks);
  printf("-- decimal round trip and gcd\n");
  const char *decs[] = {"0", "1", "4294967296", "18446744073709551616",
                        "340282366920938463463374607431768211457",
                        "123456789012345678901234567890123456789012345678901234567890"};
  for (int i = 0; i < 6; i++) {
    from_dec(&t, decs[i]);
    char out[200];
    to_dec(&t, out);
    printf("%s -> %d limbs -> %s %s\n", decs[i], t.n, out, strcmp(out, decs[i]) ? "MISMATCH" : "ok");
  }
  Big g1, g2;
  shr(&g1, &a, 100);  // 120! / 2^100 shares many factors with 3^400
  g2 = b;
  int steps = 0;
  while (g2.n) {
    divmod(&q, &r, &g1, &g2);
    g1 = g2;
    g2 = r;
    steps++;
  }
  print("gcd(120!>>100, 3^400)", &g1);
  printf("euclid steps=%d\n", steps);
  printf("-- modular exponentiation\n");
  Big p, base, e, acc;
  from_dec(&p, "170141183460469231731687303715884105727");  // 2^127 - 1, prime
  for (int k = 2; k <= 7; k++) {
    set_u64(&base, (uint64_t)k * 1000003);
    Big one;
    set_u64(&one, 1);
    sub(&e, &p, &one);
    set_u64(&acc, 1);
    Big bb = base;
    while (e.n) {
      if (e.d[0] & 1) {
        mul(&acc, &acc, &bb);
        divmod(&q, &acc, &acc, &p);
      }
      mul(&bb, &bb, &bb);
      divmod(&q, &bb, &bb, &p);
      shr(&e, &e, 1);
    }
    char out[64];
    to_dec(&acc, out);
    printf("base %d: a^(p-1) mod p = %s\n", k * 1000003, out);
  }
  printf("addbacks total: %d\n", addbacks);
  return 0;
}
