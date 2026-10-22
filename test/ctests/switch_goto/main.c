// Control flow: dense switches that become jump tables (br_table in wasm),
// sparse switches that become compare trees, switches on negative and 64-bit
// values, fallthrough chains, a default in the middle, nested switches,
// Duff's device, goto for loop exits, retry and cleanup ladders, a state
// machine driven by goto, loops with break/continue at several nesting depths,
// do-while, the comma operator in loop headers, and short-circuit && and ||
// with side effects.
//
// A br_table has to clamp its index and send out-of-range values to the
// default; a compare tree has to get every boundary right; and the irreducible
// shapes goto can make have to be handled by whoever rebuilds the CFG.  Each
// case writes something distinct so a wrong target prints a wrong line.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *dense(int x) {
  switch (x) {
    case 0: return "zero";
    case 1: return "one";
    case 2: return "two";
    case 3: return "three";
    case 4: return "four";
    case 5: return "five";
    case 6: return "six";
    case 7: return "seven";
    case 8: return "eight";
    case 9: return "nine";
    case 10: return "ten";
    case 11: return "eleven";
    default: return "other";
  }
}

static int sparse(int x) {
  switch (x) {
    case -1000000: return 1;
    case -77: return 2;
    case -1: return 3;
    case 0: return 4;
    case 13: return 5;
    case 100: return 6;
    case 1000: return 7;
    case 4096: return 8;
    case 65535: return 9;
    case 1 << 20: return 10;
    case 2147483647: return 11;
    case -2147483647 - 1: return 12;
    default: return 0;
  }
}

static int wide(long long x) {
  switch (x) {
    case 0x100000000ll: return 1;
    case -0x100000000ll: return 2;
    case 0x7fffffffffffffffll: return 3;
    case 1: return 4;
    case 2: return 5;
    case 3: return 6;
    case 0x200000001ll: return 7;
    default: return (int)(x & 0xff);
  }
}

static int fallthrough(int x) {
  int r = 0;
  switch (x) {
    case 0: r += 1;  // fallthrough
    case 1: r += 10;  // fallthrough
    default: r += 100;  // fallthrough
    case 2: r += 1000;
      break;
    case 3: r += 10000;  // fallthrough
    case 4: r += 100000;
  }
  return r;
}

static int nested(int a, int b) {
  switch (a & 3) {
    case 0:
      switch (b & 3) {
        case 0: return 1;
        case 1: return 2;
        default: return 3;
      }
    case 1:
      switch (b % 5) {
        case -4: case 4: return 4;
        case -3: case 3: return 5;
        case 0: break;
        default: return 6;
      }
      return 7;
    case 2:
      if (b > 0) {
        case 3:  // a case label inside an if: valid C, jumps into the block
          return 8 + (b & 1);
      }
      return 10;
  }
  return -1;
}

static int char_class(char c) {
  switch (c) {
    case 'a': case 'e': case 'i': case 'o': case 'u': return 1;
    case 'A' ... 'Z': return 2;  // GNU case range
    case '0' ... '9': return 3;
    case ' ': case '\t': case '\n': return 4;
    default: return 0;
  }
}

static void duff(char *to, const char *from, int count) {
  int n = (count + 7) / 8;
  switch (count % 8) {
    case 0: do { *to++ = *from++;
    case 7:      *to++ = *from++;
    case 6:      *to++ = *from++;
    case 5:      *to++ = *from++;
    case 4:      *to++ = *from++;
    case 3:      *to++ = *from++;
    case 2:      *to++ = *from++;
    case 1:      *to++ = *from++;
            } while (--n > 0);
  }
}

static int find_pair(const int *a, int n, int target, int *pi, int *pj) {
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (a[i] + a[j] == target) {
        *pi = i;
        *pj = j;
        goto found;
      }
  return 0;
found:
  return 1;
}

static int cleanup_ladder(int fail_at, char *log) {
  int rc = -1;
  strcat(log, "A");
  if (fail_at == 1) goto out;
  strcat(log, "B");
  if (fail_at == 2) goto undo_a;
  strcat(log, "C");
  if (fail_at == 3) goto undo_b;
  strcat(log, "D");
  rc = 0;
  goto out;
undo_b:
  strcat(log, "b");
undo_a:
  strcat(log, "a");
out:
  return rc;
}

// Tokenizer as a goto state machine: counts words, numbers and punctuation.
static void scan(const char *s, int *words, int *nums, int *punct, long *numsum) {
  *words = *nums = *punct = 0;
  *numsum = 0;
  long cur = 0;
start:
  if (!*s) return;
  if (*s >= '0' && *s <= '9') { cur = 0; goto number; }
  if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) goto word;
  if (*s != ' ') (*punct)++;
  s++;
  goto start;
number:
  cur = cur * 10 + (*s - '0');
  s++;
  if (*s >= '0' && *s <= '9') goto number;
  (*nums)++;
  *numsum += cur;
  goto start;
word:
  s++;
  if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_') goto word;
  (*words)++;
  goto start;
}

static void loops(void) {
  printf("-- loops with break and continue\n");
  int found = 0;
  for (int i = 2; i < 200 && found < 25; i++) {
    int d;
    for (d = 2; d * d <= i; d++)
      if (i % d == 0) break;
    if (d * d <= i) continue;
    printf("%d ", i);
    found++;
  }
  printf("\n");
  int i = 0, j = 0, k = 0;
  do {
    i++;
    if (i % 3 == 0) continue;
    j += i;
  } while (i < 20);
  for (i = 0, k = 100; i < k; i += 7, k -= 3) j ^= i * k;
  printf("i=%d j=%d k=%d\n", i, j, k);
  // labelled-style break out of three loops via a flag and via goto
  int hits = 0;
  for (int a = 0; a < 10; a++) {
    for (int b = 0; b < 10; b++) {
      for (int c = 0; c < 10; c++) {
        if (a * 100 + b * 10 + c == 537) goto done;
        if ((a + b + c) % 7 == 0) hits++;
      }
    }
  }
done:
  printf("hits before 537: %d\n", hits);
  // while with complex condition and side effects in the condition
  int n = 27, steps = 0;
  while (n != 1 && ++steps < 1000) n = n & 1 ? 3 * n + 1 : n / 2;
  printf("collatz(27) steps=%d\n", steps);
  // short-circuit evaluation counting the calls made
  int calls = 0;
#define T(v) (calls++, (v))
  int r1 = T(0) && T(1);
  int r2 = T(1) || T(0);
  int r3 = (T(1) && T(0)) || T(1);
  int r4 = T(0) || T(0) || (T(1) && T(1));
  int r5 = !T(0) && (T(1) ? T(2) : T(3));
  printf("short-circuit: %d %d %d %d %d calls=%d\n", r1, r2, r3, r4, r5, calls);
  // conditional chains
  for (int x = -3; x <= 3; x++) {
    const char *s = x < -1 ? "very negative" : x < 0 ? "negative" : x == 0 ? "zero" : x < 2 ? "small" : "large";
    printf("%d:%s ", x, s);
  }
  printf("\n");
}

int main(void) {
  printf("-- dense\n");
  for (int x = -2; x <= 13; x++) printf("%d=%s ", x, dense(x));
  printf("\n-- sparse\n");
  int sp[] = {-1000000, -1000001, -77, -76, -1, 0, 1, 12, 13, 14, 99, 100, 1000,
              4095, 4096, 65535, 65536, 1 << 20, 2147483647, -2147483647 - 1, 555};
  for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) printf("%d:%d ", sp[i], sparse(sp[i]));
  printf("\n-- 64-bit\n");
  long long wv[] = {0x100000000ll, -0x100000000ll, 0x7fffffffffffffffll, 1, 2, 3,
                    0x200000001ll, 0x1ll << 40 | 0x37, 4, 0, -1};
  for (unsigned i = 0; i < sizeof wv / sizeof wv[0]; i++) printf("%d ", wide(wv[i]));
  printf("\n-- fallthrough\n");
  for (int x = -1; x <= 6; x++) printf("%d:%d ", x, fallthrough(x));
  printf("\n-- nested\n");
  for (int a = 0; a < 8; a++) {
    for (int b = -6; b <= 6; b += 2) printf("%d", nested(a, b));
    printf(" ");
  }
  printf("\n-- character classes\n");
  const char *text = "Hello World 2024\tis Over!";
  for (const char *p = text; *p; p++) printf("%d", char_class(*p));
  printf("\n-- duff\n");
  const char *src = "the quick brown fox jumps over the lazy dog";
  for (int cnt = 1; cnt <= 43; cnt += 6) {
    char dst[64];
    memset(dst, 0, sizeof dst);
    duff(dst, src, cnt);
    printf("%2d [%s]\n", cnt, dst);
  }
  printf("-- goto\n");
  int arr[] = {8, 3, 11, -4, 7, 20, 1, 15};
  for (int t = 0; t < 30; t += 4) {
    int pi = -1, pj = -1;
    int ok = find_pair(arr, 8, t, &pi, &pj);
    printf("target %d: %d (%d,%d)\n", t, ok, pi, pj);
  }
  for (int f = 0; f <= 3; f++) {
    char log[16] = "";
    int rc = cleanup_ladder(f, log);
    printf("fail_at=%d rc=%d log=%s\n", f, rc, log);
  }
  const char *inputs[] = {"abc 123 de, 45!", "x1y22z333", "  ...  ", "id_name = 42 + 58;", ""};
  for (int i = 0; i < 5; i++) {
    int w, nn, pu;
    long sum;
    scan(inputs[i], &w, &nn, &pu, &sum);
    printf("'%s': words=%d nums=%d punct=%d sum=%ld\n", inputs[i], w, nn, pu, sum);
  }
  loops();
  return 0;
}
