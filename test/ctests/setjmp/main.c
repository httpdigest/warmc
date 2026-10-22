// setjmp and longjmp: a jump from several frames down back to the caller,
// the value passed through (and longjmp(env, 0) arriving as 1), nested
// handlers where an inner jmp_buf is installed and removed around a call and
// an outer one catches what the inner does not, re-entering the same setjmp
// many times in a loop, volatile locals keeping values modified between
// setjmp and longjmp, jumps out of recursion at varying depths, a try/catch
// macro layer with rethrow, and cleanup bookkeeping across the unwound frames.
//
// wasi-sdk lowers these onto wasm exception handling: setjmp sets up a
// try_table around the rest of the function, longjmp throws a tag carrying
// the jmp_buf and value, and every function between them unwinds.  The
// counts printed after each jump show which frames ran their code before the
// throw and which did not.
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf top;
static int depth_reached;

static void dive(int depth, int bail) {
  depth_reached = depth;
  if (depth == bail) longjmp(top, depth * 10 + 1);
  if (depth < 50) dive(depth + 1, bail);
  depth_reached = -depth;  // only reached when no jump happens
}

static void basics(void) {
  printf("-- basic jumps\n");
  for (int bail = 0; bail <= 60; bail += 7) {
    int r = setjmp(top);
    if (r == 0) {
      dive(0, bail);
      printf("bail=%d no jump, depth_reached=%d\n", bail, depth_reached);
    } else {
      printf("bail=%d jumped r=%d depth_reached=%d\n", bail, r, depth_reached);
    }
  }
  // longjmp with 0 delivers 1
  volatile int seen = 0;
  int r = setjmp(top);
  if (r == 0) {
    seen = 1;
    longjmp(top, 0);
  }
  printf("longjmp(0) -> r=%d seen=%d\n", r, seen);
}

static void volatiles(void) {
  printf("-- volatile locals across the jump\n");
  jmp_buf env;
  volatile int counter = 0;
  volatile double acc = 1.0;
  volatile long long big = 1;
  int r = setjmp(env);
  printf("r=%d counter=%d acc=%g big=%lld\n", r, counter, acc, big);
  if (counter < 8) {
    counter++;
    acc *= 1.5;
    big *= -3;
    longjmp(env, counter * 2);
  }
}

static jmp_buf *handler;  // innermost installed handler
static int cleanups;

static void thrower(int code) {
  if (code) longjmp(*handler, code);
}

static int inner(int code, int catch_only) {
  jmp_buf here;
  jmp_buf *saved = handler;
  handler = &here;
  int r = setjmp(here);
  if (r == 0) {
    thrower(code);
    handler = saved;
    return 0;
  }
  handler = saved;
  cleanups++;
  if (r != catch_only) longjmp(*handler, r + 1000);  // rethrow, decorated
  return r;
}

static void nested(void) {
  printf("-- nested handlers\n");
  for (int code = 0; code <= 5; code++) {
    jmp_buf outer;
    handler = &outer;
    int r = setjmp(outer);
    if (r == 0) {
      int got = inner(code, 3);
      printf("code=%d inner returned %d cleanups=%d\n", code, got, cleanups);
    } else {
      printf("code=%d outer caught %d cleanups=%d\n", code, r, cleanups);
    }
  }
}

// TRY/CATCH macros on a handler stack.
#define MAXH 16
static jmp_buf hstack[MAXH];
static int hdepth;
static const char *err_msg;
#define TRY if (setjmp(hstack[hdepth++]) == 0) {
#define CATCH(e) hdepth--; } else { int e = err_code; hdepth--;
#define END }
static int err_code;
static void raise_err(int code, const char *msg) {
  err_code = code;
  err_msg = msg;
  longjmp(hstack[hdepth - 1], 1);
}

static int parse_digit(char c) {
  if (c < '0' || c > '9') raise_err(c, "not a digit");
  return c - '0';
}
static int parse_number(const char *s) {
  int v = 0;
  for (; *s; s++) {
    v = v * 10 + parse_digit(*s);
    if (v > 100000) raise_err(v, "too big");
  }
  return v;
}

static void try_catch(void) {
  printf("-- try/catch macros\n");
  const char *inputs[] = {"123", "45x6", "999999", "", "0", "7a", "31337"};
  for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
    TRY
      int v = parse_number(inputs[i]);
      printf("'%s' -> %d (depth %d)\n", inputs[i], v, hdepth);
    CATCH(e)
      printf("'%s' -> error %d: %s (depth %d)\n", inputs[i], e, err_msg, hdepth);
    END
  }
  // nested TRY: the inner catches, rethrows a different error to the outer
  TRY
    TRY
      parse_number("12z");
    CATCH(e)
      printf("inner caught %d, rethrowing\n", e);
      raise_err(e + 1, "rethrown");
    END
    printf("not reached\n");
  CATCH(e)
    printf("outer caught %d: %s depth=%d\n", e, err_msg, hdepth);
  END
}

// Re-entering the same setjmp in a loop, with the loop state in statics.
static jmp_buf loop_env;
static int iterations;
static unsigned long long trail;
static void step(int i) {
  trail = trail * 31 + (unsigned)i;
  if (i % 3 == 0) longjmp(loop_env, i + 1);
}

static void reentry(void) {
  printf("-- re-entry\n");
  iterations = 0;
  trail = 7;
  int jumps = 0;
  for (int i = 0; i < 30; i++) {
    int r = setjmp(loop_env);
    if (r) {
      jumps++;
      continue;
    }
    step(i);
    iterations++;
  }
  printf("iterations=%d jumps=%d trail=%llu\n", iterations, jumps, trail);
}

// Jump out of a recursion that has built up state in its frames.
static jmp_buf found_env;
static int path[32];
static int path_len;
static void search(const int *tree, int n, int node, int target, int depth) {
  if (node >= n) return;
  path[depth] = node;
  if (tree[node] == target) {
    path_len = depth + 1;
    longjmp(found_env, 1);
  }
  search(tree, n, 2 * node + 1, target, depth + 1);
  search(tree, n, 2 * node + 2, target, depth + 1);
}

static void tree_search(void) {
  printf("-- escape from a tree search\n");
  int tree[63];
  for (int i = 0; i < 63; i++) tree[i] = (i * 37 + 11) % 101;
  int targets[] = {11, 48, 0, 100, 7, 85, 1000};
  for (unsigned t = 0; t < sizeof targets / sizeof targets[0]; t++) {
    path_len = 0;
    if (setjmp(found_env) == 0) {
      search(tree, 63, 0, targets[t], 0);
      printf("target %d not found\n", targets[t]);
    } else {
      printf("target %d path:", targets[t]);
      for (int i = 0; i < path_len; i++) printf(" %d", path[i]);
      printf("\n");
    }
  }
}

// A string buffer that is partially written before the jump.
static jmp_buf buf_env;
static void writer(char *out, int limit) {
  for (int i = 0; i < 26; i++) {
    if (i == limit) longjmp(buf_env, i);
    out[i] = (char)('a' + i);
    out[i + 1] = 0;
  }
}

static void partial(void) {
  printf("-- partial writes survive\n");
  for (int limit = 0; limit <= 30; limit += 6) {
    // static: an automatic array changed between setjmp and longjmp would
    // be indeterminate afterwards
    static char out[32];
    memset(out, 0, sizeof out);
    int r = setjmp(buf_env);
    if (r == 0) {
      writer(out, limit);
      printf("limit=%d complete: %s\n", limit, out);
    } else {
      printf("limit=%d stopped at %d: %s\n", limit, r, out);
    }
  }
}

int main(void) {
  basics();
  volatiles();
  nested();
  try_catch();
  reentry();
  tree_search();
  partial();
  return 0;
}
