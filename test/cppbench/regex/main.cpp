// std::regex over generated log lines: a handful of ECMAScript patterns with
// groups, alternation, classes and repetition, searched for with
// regex_search, iterated over with sregex_iterator and checked whole with
// regex_match.
//
// libc++'s regex is an interpreter.  Compiling a pattern builds a linked
// graph of heap-allocated nodes -- some two dozen kinds, each with a virtual
// __exec -- and matching walks that graph with a backtracking driver,
// __match_at_start_ecma, that keeps its states in a vector and steps each
// one through a call_indirect to the current node's __exec or __exec_split.
// A state carries vectors of its own for the submatches and loop counters,
// so a branch point copies them, and a class like \d or [a-z] is a
// bracket-expression node with a general matcher of its own behind it.
// Notoriously slow on every platform, which is why it is worth having: it is
// real code people ship, and its cost is dispatch, allocation and
// bookkeeping rather than arithmetic.
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

int main() {
  static const char *const users[] = {"ada", "bob", "cy", "dana", "eve", "fritz", "gus"};
  static const char *const hosts[] = {"example", "mail", "warm", "lab"};
  static const char *const tlds[] = {"com", "org", "net", "io"};
  static const char *const verbs[] = {"GET", "PUT", "POST", "DELETE"};

  uint32_t x = 4004004u;
  auto next = [&x] {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
  };
  std::vector<std::string> lines;
  for (int i = 0; i < 1200; i++) {
    uint32_t r = next();
    std::string s = std::to_string(r % 24) + ":" + std::to_string(10 + r % 50) + " ";
    s += verbs[r >> 30];
    s += " /api/v" + std::to_string(1 + (r >> 8) % 3) + "/item/" + std::to_string(r % 9973);
    if (r & 16) s += std::string(" from ") + users[(r >> 12) % 7] + "@" + hosts[(r >> 15) % 4] + "." + tlds[(r >> 17) % 4];
    if (r & 32) s += " status=" + std::to_string(200 + (r >> 19) % 4 * 100 + (r >> 22) % 5);
    lines.push_back(std::move(s));
  }

  const std::regex email(R"(([a-z]+)@([a-z]+)\.(com|org))");
  const std::regex path(R"((GET|PUT) /api/v([12])/item/(\d+))");
  const std::regex status(R"(status=([45])\d\d)");
  const std::regex whole(R"(\d+:\d+ [A-Z]+ /[a-z0-9/]+( .*)?)");
  const std::regex number(R"(\d+)");

  uint64_t check = 0;
  for (int round = 0; round < 4; round++) {
    for (const std::string &s : lines) {
      std::smatch m;
      if (std::regex_search(s, m, email)) check += m[1].length() * 7 + m[3].str()[0];
      if (std::regex_search(s, m, path)) check += std::stoul(m[3].str()) + m[2].str()[0];
      if (std::regex_search(s, m, status)) check += m[1].str()[0] * 3;
      check += std::regex_match(s, whole) ? 5 : 1;
      for (std::sregex_iterator it(s.begin(), s.end(), number), end; it != end; ++it)
        check = check * 31 + it->length() + it->position();
    }
  }
  std::printf("check = %llu\n", static_cast<unsigned long long>(check));
  return 0;
}
