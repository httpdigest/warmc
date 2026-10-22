// <regex>: regex_match and regex_search with capture groups and their
// positions, regex_replace with $1-style back-references and format flags,
// sregex_iterator over all matches, sregex_token_iterator for splitting
// and for selecting submatches, case-insensitive matching, the basic,
// extended, awk, grep and egrep grammars beside ECMAScript, character
// classes ([[:digit:]], \d, \w, \s), anchors, alternation, greedy and lazy
// quantifiers, bounded repetition, back-references inside a pattern,
// lookahead, and std::regex_error for malformed patterns with its code.
//
// libc++'s regex is a backtracking matcher built from a graph of node
// objects with virtual exec functions, and its compiler is a hand-written
// recursive-descent parser; both run here, over small inputs, with every
// match and every submatch printed.
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

namespace {

void showMatch(const char *label, const std::smatch &m) {
  std::printf("%-22s matched=%d groups=%zu", label, !m.empty(), m.size());
  for (std::size_t i = 0; i < m.size(); i++) {
    if (m[i].matched)
      std::printf(" [%zu:'%s'@%ld]", i, m[i].str().c_str(), (long)m.position(i));
    else
      std::printf(" [%zu:-]", i);
  }
  std::printf("\n");
}

}  // namespace

int main() {
  std::printf("== regex_match\n");
  {
    std::regex date(R"((\d{4})-(\d{2})-(\d{2}))");
    for (std::string s : {"2025-10-22", "2025-1-22", "x2025-10-22", "1999-12-31", "2025-10-22 "}) {
      std::smatch m;
      bool ok = std::regex_match(s, m, date);
      std::printf("%-14s ", ("'" + s + "'").c_str());
      showMatch(ok ? "match" : "no match", m);
    }
    std::regex ident("[A-Za-z_][A-Za-z0-9_]*");
    for (const char *s : {"foo", "_bar9", "9lives", "a-b", ""})
      std::printf("ident('%s')=%d\n", s, std::regex_match(s, ident));
  }

  std::printf("== regex_search\n");
  {
    std::string text = "Contact: alice@example.com, bob.smith@mail.org; invalid@; carol+tag@x.io";
    std::regex email(R"(([\w.+]+)@([\w]+)\.(\w+))");
    std::smatch m;
    std::string rest = text;
    int n = 0;
    while (std::regex_search(rest, m, email)) {
      std::printf("email %d: user=%s domain=%s tld=%s prefix_len=%zu\n", n++, m[1].str().c_str(),
                  m[2].str().c_str(), m[3].str().c_str(), m.prefix().str().size());
      rest = m.suffix().str();
    }
    std::smatch first;
    std::regex_search(text, first, std::regex(R"(\b(\w+):)"));
    showMatch("label", first);
    std::smatch opt;
    std::string sample = "color colour";
    std::regex_search(sample, opt, std::regex("colo(u)?r"));
    showMatch("optional group", opt);
  }

  std::printf("== iterators\n");
  {
    std::string log = "GET /a 200 12ms; POST /b/c 404 3ms; GET / 500 120ms; PUT /x 201 7ms";
    std::regex req(R"((GET|POST|PUT) (\S+) (\d{3}) (\d+)ms)");
    int total = 0;
    for (auto it = std::sregex_iterator(log.begin(), log.end(), req); it != std::sregex_iterator(); ++it) {
      const std::smatch &m = *it;
      total += std::stoi(m[4]);
      std::printf("method=%-4s path=%-6s status=%s at=%ld\n", m[1].str().c_str(), m[2].str().c_str(),
                  m[3].str().c_str(), (long)m.position(0));
    }
    std::printf("total ms=%d\n", total);
    std::string csv = "alpha, beta,gamma ,, delta";
    std::regex sep(R"(\s*,\s*)");
    std::printf("split:");
    for (std::sregex_token_iterator it(csv.begin(), csv.end(), sep, -1), end; it != end; ++it)
      std::printf(" [%s]", it->str().c_str());
    std::printf("\n");
    std::string kv = "a=1;bb=22;ccc=333";
    std::regex pair(R"((\w+)=(\d+))");
    std::printf("keys:");
    for (std::sregex_token_iterator it(kv.begin(), kv.end(), pair, 1), end; it != end; ++it)
      std::printf(" %s", it->str().c_str());
    std::printf("\nvalues and keys:");
    for (std::sregex_token_iterator it(kv.begin(), kv.end(), pair, {2, 1}), end; it != end; ++it)
      std::printf(" %s", it->str().c_str());
    std::printf("\n");
  }

  std::printf("== regex_replace\n");
  {
    std::string s = "2025-10-22 and 1999-01-05";
    std::regex date(R"((\d{4})-(\d{2})-(\d{2}))");
    std::printf("%s\n", std::regex_replace(s, date, "$3/$2/$1").c_str());
    std::printf("%s\n", std::regex_replace(s, date, "[$&]").c_str());
    std::printf("%s\n", std::regex_replace(s, date, "<$1>", std::regex_constants::format_first_only).c_str());
    std::printf("%s\n", std::regex_replace(s, date, "$2", std::regex_constants::format_no_copy).c_str());
    std::string spaces = "  too   many    spaces here  ";
    std::printf("[%s]\n", std::regex_replace(spaces, std::regex(" +"), " ").c_str());
    std::printf("[%s]\n", std::regex_replace(spaces, std::regex("^ +| +$"), "").c_str());
    std::string camel = "parseHttpRequestHeader";
    std::printf("%s\n", std::regex_replace(camel, std::regex("([a-z])([A-Z])"), "$1_$2").c_str());
  }

  std::printf("== quantifiers, classes, anchors, back-references\n");
  {
    std::string html = "<b>bold</b> and <i>italic</i>";
    std::smatch g, l;
    std::regex_search(html, g, std::regex("<.+>"));
    std::regex_search(html, l, std::regex("<.+?>"));
    std::printf("greedy='%s' lazy='%s'\n", g.str().c_str(), l.str().c_str());
    std::regex tag(R"(<(\w+)>[^<]*</\1>)");
    for (auto it = std::sregex_iterator(html.begin(), html.end(), tag); it != std::sregex_iterator(); ++it)
      std::printf("balanced tag: %s (name %s)\n", it->str().c_str(), (*it)[1].str().c_str());
    std::regex doubled(R"(\b(\w+) \1\b)");
    std::string dup = "this is is a test test of the the regex";
    std::printf("doubled words:");
    for (auto it = std::sregex_iterator(dup.begin(), dup.end(), doubled); it != std::sregex_iterator(); ++it)
      std::printf(" '%s'", (*it)[1].str().c_str());
    std::printf("\n");
    for (const char *s : {"aaa", "aaaa", "aaaaa", "aaaaaa", "a"})
      std::printf("a{3,5}: %s -> %d   ", s, std::regex_match(s, std::regex("a{3,5}")));
    std::printf("\n");
    for (const char *s : {"abc123", "123", "abc", " 12 ", "x9y"})
      std::printf("[[:digit:]]+$ in '%s': %d  ", s, std::regex_search(s, std::regex("[[:digit:]]+$")));
    std::printf("\n");
    for (const char *s : {"password1", "Password1", "PASSWORD", "Pass1"})
      std::printf("lookahead strong('%s')=%d\n", s,
                  std::regex_match(s, std::regex(R"((?=.*[A-Z])(?=.*\d)\w{6,})")));
    std::printf("alternation: %d %d %d\n", std::regex_match("cat", std::regex("cat|dog|bird")),
                std::regex_match("bird", std::regex("cat|dog|bird")),
                std::regex_match("cow", std::regex("cat|dog|bird")));
    std::printf("whitespace and word classes: %d %d\n", std::regex_match("a_1 \t", std::regex(R"(\w+\s+)")),
                std::regex_match("a-1", std::regex(R"(\w+)")));
  }

  std::printf("== flags and grammars\n");
  {
    std::regex ci("hello world", std::regex::icase);
    std::printf("icase: %d %d\n", std::regex_match("HeLLo WoRLD", ci), std::regex_match("hello  world", ci));
    std::regex basic(R"(\(ab\)*c)", std::regex::basic);
    std::regex ext("(ab)*c", std::regex::extended);
    std::regex awk("(ab)*c", std::regex::awk);
    std::regex grep("a.c", std::regex::grep);
    std::regex egrep("abc|xyz", std::regex::egrep);
    std::printf("basic: %d %d extended: %d %d awk: %d grep: %d egrep: %d %d\n", std::regex_match("ababc", basic),
                std::regex_match("abac", basic), std::regex_match("abababc", ext), std::regex_match("c", ext),
                std::regex_match("abc", awk), std::regex_match("a-c", grep), std::regex_match("xyz", egrep),
                std::regex_match("abz", egrep));
    std::regex nosubs(R"((\d+)-(\d+))", std::regex::nosubs);
    std::smatch m;
    std::string r = "10-20";
    std::regex_match(r, m, nosubs);
    std::printf("nosubs groups=%zu mark_count=%u\n", m.size(), nosubs.mark_count());
    std::printf("mark_count of date pattern=%u\n", std::regex(R"((\d{4})-(\d{2})-(\d{2}))").mark_count());
  }

  std::printf("== errors\n");
  for (const char *bad : {"(unclosed", "[a-", "a{2,1}", "*start", "a)b", "\\", "(?<name>x)"}) {
    try {
      std::regex re(bad);
      std::printf("'%s' compiled (mark_count %u)\n", bad, re.mark_count());
    } catch (const std::regex_error &e) {
      std::printf("'%s' -> regex_error code=%d\n", bad, (int)e.code());
    }
  }
  return 0;
}
