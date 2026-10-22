// A lexer written the way hand-tuned C scanners are: one label per state and a
// goto for every transition, over generated source text with identifiers,
// keywords, decimal and hex numbers, strings with escapes, operators and both
// kinds of comment.
//
// Whitespace, line comments and block comments form one region: each can be
// followed by another without going back through the dispatcher, and the
// dispatcher can enter at any of the three, so the control flow is irreducible.
// wasm has only structured loops, and LLVM's fix-irreducible-control-flow pass
// rewrites the region into a loop headed by a br_table on a label variable that
// each entering edge sets.  Character classes are ctype.h: isspace, isalpha and
// isdigit are inline in musl's header and fold to compares, while isalnum,
// isxdigit, ispunct and tolower are calls into wasi-libc.  No other kernel
// here is irreducible by construction, in the program's own hot loop.
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint32_t idents, keywords, numbers, strings, puncts, comments, lines, errors;
  uint64_t hash;
} Counts;

static int keyword(const char *s, size_t n) {
  static const char *const kw[] = { "if", "else", "while", "for", "return", "int", "char", "static" };
  for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++)
    if (strlen(kw[i]) == n && !memcmp(kw[i], s, n)) return 1;
  return 0;
}

// One label per state and a goto for every transition.  Whitespace, line
// comments and block comments are one region: each can follow another without
// going back through `next`, and `next` can enter any of them.
__attribute__((noinline)) static void lex(const char *p, Counts *n) {
  const char *start;
  uint64_t v;
  int c;
next:
  c = (unsigned char)*p++;
  if (c == 0) return;
  if (isspace(c)) goto space;
  if (c == '/' && *p == '*') { p++; goto block; }
  if (c == '/' && *p == '/') { p++; goto line; }
  if (isalpha(c) || c == '_') { start = p - 1; goto ident; }
  if (isdigit(c)) {
    v = (uint64_t)(c - '0');
    if (c == '0' && (*p == 'x' || *p == 'X')) { p++; goto hex; }
    goto dec;
  }
  if (c == '"') { v = 0; goto str; }
  if (ispunct(c)) goto punct;
  n->errors++;
  goto next;

space:
  if (c == '\n') n->lines++;
more_space:
  c = (unsigned char)*p;
  if (isspace(c)) { p++; goto space; }
  if (c == '/' && p[1] == '*') { p += 2; goto block; }
  if (c == '/' && p[1] == '/') { p += 2; goto line; }
  goto next;

line:
  c = (unsigned char)*p++;
  if (c == 0) return;
  if (c != '\n') goto line;
  n->lines++;
  n->comments++;
  goto more_space;

block:
  c = (unsigned char)*p++;
  if (c == 0) { n->errors++; return; }
  if (c == '\n') n->lines++;
  if (c != '*') goto block;
star:
  c = (unsigned char)*p++;
  if (c == '*') goto star;
  if (c == '/') { n->comments++; goto more_space; }
  if (c == 0) { n->errors++; return; }
  if (c == '\n') n->lines++;
  goto block;

ident:
  c = (unsigned char)*p;
  if (isalnum(c) || c == '_') { p++; goto ident; }
  if (keyword(start, (size_t)(p - start))) n->keywords++;
  else n->idents++;
  v = 0;
  while (start < p) v = v * 131 + (unsigned char)*start++;
  n->hash = (n->hash ^ v) * 0x100000001b3ull;
  goto next;

dec:
  c = (unsigned char)*p;
  if (isdigit(c)) { p++; v = v * 10 + (uint64_t)(c - '0'); goto dec; }
  goto number;

hex:
  c = (unsigned char)*p;
  if (isxdigit(c)) { p++; v = v * 16 + (uint64_t)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10); goto hex; }
number:
  n->numbers++;
  n->hash += v;
  goto next;

str:
  c = (unsigned char)*p++;
  if (c == '"') { n->strings++; n->hash = (n->hash ^ v) * 31; goto next; }
  if (c == 0 || c == '\n') { n->errors++; goto next; }
  if (c != '\\') { v = v * 33 + (uint64_t)c; goto str; }
  c = (unsigned char)*p++;
  switch (c) {
    case 'n': v = v * 33 + '\n'; break;
    case 't': v = v * 33 + '\t'; break;
    case '0': v = v * 33; break;
    default: v = v * 33 + (uint64_t)c; break;
  }
  goto str;

punct:
  // Two-character operators are one token.
  if ((c == '=' || c == '!' || c == '<' || c == '>') && *p == '=') p++;
  else if ((c == '&' || c == '|' || c == '+' || c == '-') && *p == c) p++;
  n->puncts++;
  n->hash = n->hash * 7 + (uint64_t)c;
  goto next;
}

enum { SIZE = 1 << 18 };
static char text[SIZE + 64];

static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng % n;
}

static size_t put(size_t at, const char *s) {
  size_t k = strlen(s);
  memcpy(text + at, s, k);
  return at + k;
}

int main(void) {
  static const char *const words[] = {
    "if", "else", "while", "for", "return", "int", "char", "static",
    "count", "buffer_len", "x", "y2", "_tmp", "node", "next_ptr", "Value",
  };
  static const char *const ops[] = { "+", "-", "*", "/", "=", "==", "!=", "<=", "&&", "||", "++", "(", ")", "{", "}", ";", ",", "[", "]", "->" };
  size_t at = 0;
  char num[24];
  while (at < SIZE) {
    switch (rnd(10)) {
      case 0: case 1: case 2: at = put(at, words[rnd(16)]); break;
      case 3: snprintf(num, sizeof num, rnd(4) ? "%u" : "0x%X", rnd(1000000)); at = put(at, num); break;
      case 4: case 5: at = put(at, ops[rnd(20)]); break;
      case 6: at = put(at, rnd(3) ? " " : "\n  "); break;
      case 7: at = put(at, rnd(2) ? "\"str\\n\\\"q\\\" ok\"" : "\"plain text\""); break;
      case 8: at = put(at, rnd(2) ? "/* block\n * comment */" : "// to the end\n"); break;
      default: at = put(at, rnd(2) ? "\t" : " "); break;
    }
    text[at++] = ' ';
  }
  text[at] = 0;
  Counts n = { 0 };
  for (int r = 0; r < 140; r++) {
    lex(text, &n);
    text[(size_t)r * 7919 % at] = 'a';
  }
  printf("idents = %u keywords = %u numbers = %u strings = %u puncts = %u comments = %u lines = %u errors = %u hash = %016llx\n",
         n.idents, n.keywords, n.numbers, n.strings, n.puncts, n.comments, n.lines, n.errors,
         (unsigned long long)n.hash);
  return 0;
}
