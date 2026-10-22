// A key=value config parser written over std::string_view, with every field
// parsed by std::from_chars into a std::optional: split into lines, trim,
// skip comments, split at '=' and ',', and accumulate the values that parse.
//
// Text processing without a single allocation.  A string_view is a pointer
// and a length, so parse takes one as two i32 parameters and returns its
// optional<uint32_t> -- the value and a flag byte -- through a pointer into
// the caller's frame.  find on one character is a call to the C library's
// memchr, and starts_with("0x") one i32.load16_u compared with a constant.
// from_chars's base-10 path inlines into parse as a loop that skips zeros
// and stores digits into a buffer on the shadow stack, then a chain of
// multiplies by powers of ten, the last digit checked for overflow by an
// i64.mul against a table; base 16 is a call to libc++'s generic
// __subject_seq_combinator.  Short loops over bytes with many early exits,
// the shape of every parser.
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

__attribute__((noinline)) std::optional<uint32_t> parse(std::string_view s) {
  int base = 10;
  if (s.starts_with("0x")) {
    base = 16;
    s.remove_prefix(2);
  }
  uint32_t v = 0;
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
  if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
  return v;
}

struct Totals {
  uint64_t sum = 0, keys = 0, bad = 0, comments = 0;
};

void scan(std::string_view text, Totals &t) {
  while (!text.empty()) {
    size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '#') {
      t.comments++;
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      t.bad++;
      continue;
    }
    std::string_view key = trim(line.substr(0, eq));
    std::string_view rest = line.substr(eq + 1);
    t.keys += key.size();
    // A value is a comma-separated list of numbers.
    while (!rest.empty()) {
      size_t comma = rest.find(',');
      std::optional<uint32_t> v = parse(trim(rest.substr(0, comma)));
      if (v) t.sum += *v ^ key.size(); else t.bad++;
      rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
    }
  }
}

}  // namespace

int main() {
  std::string text;
  uint32_t x = 8675309u;
  auto next = [&x] {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
  };
  for (int i = 0; i < 6000; i++) {
    uint32_t r = next();
    switch (r & 7) {
      case 0: text += "# comment " + std::to_string(r) + "\n"; break;
      case 1: text += "\n"; break;
      default: {
        text += "  key";
        text += std::to_string(r % 500);
        text += (r & 8) ? " = " : "=";
        int n = 1 + (r >> 4) % 6;
        for (int k = 0; k < n; k++) {
          uint32_t v = next();
          if (k) text += (v & 1) ? ", " : ",";
          if ((v & 6) == 0) text += "0x" + std::string("0123456789abcdef").substr(v % 16, 3);
          else if ((v & 62) == 2) text += "12x";
          else if ((v & 62) == 4) text += "99999999999";
          else text += std::to_string(v >> (v % 24));
        }
        text += (r & 16) ? "\t\n" : "\n";
        break;
      }
    }
  }

  Totals t;
  // Each round starts a few bytes further in, so the rounds differ in their
  // first line.
  for (int round = 0; round < 300; round++) scan(std::string_view(text).substr(round % 11), t);
  std::printf("sum = %llu keys = %llu bad = %llu comments = %llu\n",
              static_cast<unsigned long long>(t.sum), static_cast<unsigned long long>(t.keys),
              static_cast<unsigned long long>(t.bad),
              static_cast<unsigned long long>(t.comments));
  return 0;
}
