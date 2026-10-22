// string.h's string functions: strlen, strcpy, strncpy (with its zero padding
// and its missing terminator when the source is long), strcat, strncat,
// strcmp, strncmp, strchr, strrchr, strstr, strpbrk, strspn, strcspn, strtok
// and strtok_r, strdup and strndup, strnlen, strcasecmp/strncasecmp, stpcpy,
// and string reversal, splitting and joining written on top of them.
//
// musl's versions of these read a word at a time once the pointer is aligned
// (the has-zero-byte trick on 32-bit words) and fall back to bytes at the
// ends, so strings of every length from 0 to beyond a word, at every offset
// from an aligned base, are run through each function here.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int sgn(int v) { return (v > 0) - (v < 0); }

static void lengths(void) {
  printf("-- strlen and strnlen at every alignment\n");
  char buf[80];
  for (int off = 0; off < 8; off++) {
    printf("off %d:", off);
    for (int len = 0; len < 20; len += 3) {
      memset(buf, 'x', sizeof buf);
      buf[off + len] = 0;
      printf(" %zu/%zu", strlen(buf + off), strnlen(buf + off, 7));
    }
    printf("\n");
  }
}

static void copies(void) {
  printf("-- strcpy strncpy strcat strncat stpcpy\n");
  char dst[40];
  const char *srcs[] = {"", "a", "hello", "exactly8", "a longer string here"};
  for (int i = 0; i < 5; i++) {
    memset(dst, '#', sizeof dst);
    strcpy(dst, srcs[i]);
    printf("strcpy [%s] ", dst);
    memset(dst, '#', sizeof dst);
    strncpy(dst, srcs[i], 8);
    printf("strncpy8 [");
    for (int k = 0; k < 10; k++) putchar(dst[k] ? dst[k] : '0');
    printf("] ");
    memset(dst, 0, sizeof dst);
    strcpy(dst, "pre-");
    strcat(dst, srcs[i]);
    strncat(dst, "|tail-end", 5);
    printf("cat [%s] len=%zu\n", dst, strlen(dst));
  }
  char *p = dst;
  p = stpcpy(p, "one");
  p = stpcpy(p, ",two");
  p = stpcpy(p, ",three");
  printf("stpcpy [%s] end offset %td\n", dst, p - dst);
}

static void compares(void) {
  printf("-- strcmp strncmp strcasecmp\n");
  const char *s[] = {"", "a", "ab", "abc", "abd", "ABC", "b", "abc\xff", "abc\x01", "Abc"};
  int n = sizeof s / sizeof s[0];
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++)
      printf("%c", "<=>"[sgn(strcmp(s[i], s[j])) + 1]);
    printf(" ");
    for (int j = 0; j < n; j++)
      printf("%c", "<=>"[sgn(strncmp(s[i], s[j], 2)) + 1]);
    printf(" ");
    for (int j = 0; j < n; j++)
      printf("%c", "<=>"[sgn(strcasecmp(s[i], s[j])) + 1]);
    printf(" ");
    for (int j = 0; j < n; j++)
      printf("%c", "<=>"[sgn(strncasecmp(s[i], s[j], 3)) + 1]);
    printf("\n");
  }
}

static void searches(void) {
  printf("-- strchr strrchr strstr strpbrk strspn strcspn\n");
  const char *hay = "the cat sat on the mat; the end, finally.";
  for (const char *c = "tae;.zh"; *c; c++) {
    const char *f = strchr(hay, *c), *l = strrchr(hay, *c);
    printf("'%c': first=%td last=%td\n", *c, f ? f - hay : -1, l ? l - hay : -1);
  }
  printf("strchr nul: %td\n", strchr(hay, 0) - hay);
  const char *needles[] = {"the", "at", "mat;", "end,", "finally.", "", "cats", "e e", "y."};
  for (int i = 0; i < 9; i++) {
    const char *f = strstr(hay, needles[i]);
    printf("strstr '%s' -> %td\n", needles[i], f ? f - hay : -1);
  }
  const char *sets[] = {";,", "xyz", "aeiou", " ", "."};
  for (int i = 0; i < 5; i++) {
    const char *f = strpbrk(hay, sets[i]);
    printf("pbrk '%s' -> %td spn=%zu cspn=%zu\n", sets[i], f ? f - hay : -1,
           strspn(hay, "the cas"), strcspn(hay, sets[i]));
  }
  // strstr on a long periodic haystack exercises musl's two-way search
  char big[600];
  for (int i = 0; i < 599; i++) big[i] = "abaabaaab"[i % 9];
  big[599] = 0;
  const char *pats[] = {"aab", "baaaba", "aabaaabaaba", "abaabaaababaabaaabX", "aaabab", "babaab"};
  for (int i = 0; i < 6; i++) {
    const char *f = strstr(big, pats[i]);
    printf("periodic '%s' -> %td\n", pats[i], f ? f - big : -1);
  }
  char longpat[100];
  memcpy(longpat, big + 300, 90);
  longpat[90] = 0;
  printf("long pattern -> %td\n", strstr(big, longpat) - big);
}

static void tokens(void) {
  printf("-- strtok and strtok_r\n");
  char line[] = "  alpha, beta;;gamma ,delta,,  epsilon;  ";
  int n = 0;
  for (char *t = strtok(line, " ,;"); t; t = strtok(NULL, " ,;")) printf("tok%d=[%s] ", n++, t);
  printf("\n");
  char table[] = "a=1;b=22;ccc=333;d=;=5";
  char *save1;
  for (char *rec = strtok_r(table, ";", &save1); rec; rec = strtok_r(NULL, ";", &save1)) {
    char *save2;
    char *k = strtok_r(rec, "=", &save2);
    char *v = strtok_r(NULL, "=", &save2);
    printf("rec key=[%s] value=[%s]\n", k ? k : "(null)", v ? v : "(null)");
  }
}

static void reverse(char *s) {
  size_t n = strlen(s);
  for (size_t i = 0; i < n / 2; i++) {
    char t = s[i];
    s[i] = s[n - 1 - i];
    s[n - 1 - i] = t;
  }
}

static void derived(void) {
  printf("-- strdup, reversing, joining\n");
  const char *words[] = {"stressed", "level", "drawer", "a", "", "racecar", "palindrome"};
  char joined[128] = "";
  for (int i = 0; i < 7; i++) {
    char *d = strdup(words[i]);
    reverse(d);
    printf("%s -> %s %s\n", words[i], d, strcmp(d, words[i]) ? "" : "(palindrome)");
    if (i) strcat(joined, "+");
    strcat(joined, d);
    free(d);
  }
  printf("joined=%s\n", joined);
  char *nd = strndup("truncate me please", 8);
  printf("strndup=[%s] %zu\n", nd, strlen(nd));
  free(nd);
  // count words by hand and with strspn/strcspn
  const char *text = "  the  quick brown\tfox  jumped   ";
  int count = 0;
  const char *p = text;
  for (;;) {
    p += strspn(p, " \t");
    if (!*p) break;
    size_t w = strcspn(p, " \t");
    printf("word '%.*s' len %zu\n", (int)w, p, w);
    p += w;
    count++;
  }
  printf("words=%d\n", count);
  // replace all occurrences of a substring
  const char *in = "the cat and the hat and the bat";
  char out[128];
  char *o = out;
  const char *q = in;
  const char *hit;
  while ((hit = strstr(q, "the")) != NULL) {
    memcpy(o, q, (size_t)(hit - q));
    o += hit - q;
    o = stpcpy(o, "THE");
    q = hit + 3;
  }
  strcpy(o, q);
  printf("replaced=%s\n", out);
}

int main(void) {
  lengths();
  copies();
  compares();
  searches();
  tokens();
  derived();
  return 0;
}
