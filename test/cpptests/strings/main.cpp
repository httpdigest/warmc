// std::string and std::string_view: construction, the small-string
// boundary (libc++ keeps up to 10 chars inline on 32-bit targets, then
// switches to the heap), append/insert/erase/replace at every position,
// find/rfind/find_first_of/find_last_not_of with and without positions,
// substr, compare, starts_with/ends_with, resize, case mapping, numeric
// conversions both ways (to_string of every arithmetic type, stoi/stol/
// stoll/stoul/stoull with bases and the end position, stof/stod), and
// string_view slicing with remove_prefix/remove_suffix.  Ends with a
// tokenizer, a word reverser, a run-length codec and a Caesar cipher, each
// printed for several inputs.
//
// libc++'s string is a union of a short and a long layout distinguished by
// one bit; every mutation near the inline capacity crosses that boundary,
// so the lengths around it are printed one at a time.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

void show(const char *label, const std::string &s) {
  std::printf("%-22s \"%s\" (len %zu)\n", label, s.c_str(), s.size());
}

std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  while (true) {
    auto p = s.find(sep);
    out.push_back(s.substr(0, p));
    if (p == std::string_view::npos) break;
    s.remove_prefix(p + 1);
  }
  return out;
}

std::string_view trim(std::string_view s) {
  auto b = s.find_first_not_of(" \t\n");
  if (b == std::string_view::npos) return {};
  auto e = s.find_last_not_of(" \t\n");
  return s.substr(b, e - b + 1);
}

std::string rle(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size();) {
    std::size_t j = i;
    while (j < s.size() && s[j] == s[i]) ++j;
    out += std::to_string(j - i);
    out += s[i];
    i = j;
  }
  return out;
}

std::string unrle(std::string_view s) {
  std::string out;
  std::size_t n = 0;
  for (char c : s) {
    if (std::isdigit((unsigned char)c))
      n = n * 10 + (std::size_t)(c - '0');
    else {
      out.append(n, c);
      n = 0;
    }
  }
  return out;
}

std::string caesar(std::string s, int k) {
  for (char &c : s) {
    if (std::islower((unsigned char)c)) c = (char)('a' + (c - 'a' + k + 26) % 26);
    else if (std::isupper((unsigned char)c)) c = (char)('A' + (c - 'A' + k + 26) % 26);
  }
  return s;
}

std::string reverseWords(std::string_view s) {
  auto parts = split(s, ' ');
  std::string out;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (!out.empty()) out += ' ';
    out += *it;
  }
  return out;
}

}  // namespace

int main() {
  std::printf("== construction\n");
  show("default", std::string());
  show("count+char", std::string(5, 'x'));
  show("c-string", std::string("hello"));
  show("prefix of c-string", std::string("hello world", 7));
  show("substring ctor", std::string(std::string("abcdefgh"), 2, 4));
  show("from range", std::string(std::string_view("ranged!")));
  std::string withNul("a\0b\0c", 5);
  std::printf("embedded NULs size=%zu bytes:", withNul.size());
  for (char c : withNul) std::printf(" %02x", (unsigned)(unsigned char)c);
  std::printf("\n");

  std::printf("== short/long boundary\n");
  std::string grow;
  for (int i = 0; i < 26; i++) {
    grow += (char)('a' + i);
    std::printf("len=%2zu cap=%2zu %s\n", grow.size(), grow.capacity(), grow.c_str());
  }
  grow.resize(8);
  grow.shrink_to_fit();
  std::printf("after resize(8)+shrink: cap=%zu %s\n", grow.capacity(), grow.c_str());
  std::string moved = std::move(grow);
  std::printf("moved=%s source empty=%d\n", moved.c_str(), grow.empty());

  std::printf("== mutation\n");
  std::string s = "The quick fox";
  s.insert(4, "very ");
  show("insert", s);
  s.insert(s.size(), 3, '!');
  show("insert count at end", s);
  s.append(" jumps").append(2, '?');
  show("append chain", s);
  s.erase(0, 4);
  show("erase prefix", s);
  s.erase(s.find('!'), 3);
  show("erase found", s);
  s.replace(0, 4, "an extremely");
  show("replace longer", s);
  s.replace(s.find("extremely"), 9, "odd");
  show("replace shorter", s);
  s.push_back('.');
  s.pop_back();
  s += '#';
  show("push/pop/+=", s);
  s.resize(s.size() + 3, '*');
  show("resize up", s);
  s.resize(10);
  show("resize down", s);
  std::string t = s;
  t.swap(s);
  t.clear();
  show("after swap+clear", t);
  show("swapped", s);

  std::printf("== searching\n");
  std::string hay = "abracadabra cadabra abra";
  std::printf("find(abra)=%zu find(abra,1)=%zu rfind(abra)=%zu rfind(abra,10)=%zu\n",
              hay.find("abra"), hay.find("abra", 1), hay.rfind("abra"), hay.rfind("abra", 10));
  std::printf("find(z)=%s find_first_of(cd)=%zu find_last_of(cd)=%zu\n",
              hay.find('z') == std::string::npos ? "npos" : "?", hay.find_first_of("cd"),
              hay.find_last_of("cd"));
  std::printf("find_first_not_of(abr)=%zu find_last_not_of(a)=%zu\n",
              hay.find_first_not_of("abr"), hay.find_last_not_of("a"));
  std::size_t count = 0;
  for (std::size_t p = hay.find("a"); p != std::string::npos; p = hay.find("a", p + 1)) ++count;
  std::printf("occurrences of a=%zu\n", count);
  std::printf("substr(4,3)=%s substr(20)=%s\n", hay.substr(4, 3).c_str(), hay.substr(20).c_str());
  std::printf("starts_with(abra)=%d ends_with(abra)=%d ends_with('x')=%d contains-ish=%d\n",
              hay.starts_with("abra"), hay.ends_with("abra"), hay.ends_with('x'),
              hay.find("cad") != std::string::npos);

  std::printf("== comparison\n");
  const char *ws[] = {"apple", "Apple", "app", "apples", "banana", ""};
  for (const char *x : ws) {
    std::printf("%-7s", x);
    for (const char *y : ws) {
      int c = std::string(x).compare(y);
      std::printf(" %c", c < 0 ? '<' : c > 0 ? '>' : '=');
    }
    std::printf("\n");
  }
  std::string cmp = "hello world";
  std::printf("compare(6,5,\"world\")=%d compare(0,5,\"help\")=%d\n", cmp.compare(6, 5, "world"),
              cmp.compare(0, 5, "help") < 0 ? -1 : 1);

  std::printf("== to_string\n");
  std::printf("%s %s %s %s %s\n", std::to_string(0).c_str(), std::to_string(-2147483647 - 1).c_str(),
              std::to_string(4294967295u).c_str(), std::to_string(-9223372036854775807LL - 1).c_str(),
              std::to_string(18446744073709551615ULL).c_str());
  std::printf("%s %s %s %s\n", std::to_string(3.14159).c_str(), std::to_string(-0.5f).c_str(),
              std::to_string(1e20).c_str(), std::to_string(1e-7).c_str());

  std::printf("== parsing\n");
  const char *ints[] = {"42", "-17", "  +8", "0x1f", "0777", "123abc", "2147483647", "-2147483648"};
  for (const char *x : ints) {
    std::size_t pos = 0;
    int v10 = std::stoi(x, &pos);
    std::size_t pos0 = 0;
    long v0 = std::stol(x, &pos0, 0);
    std::printf("%-12s stoi=%d (pos %zu) stol base0=%ld (pos %zu)\n", x, v10, pos, v0, pos0);
  }
  std::printf("stoi(\"ff\",16)=%d stoi(\"1010\",2)=%d stoi(\"zz\",36)=%d\n", std::stoi("ff", nullptr, 16),
              std::stoi("1010", nullptr, 2), std::stoi("zz", nullptr, 36));
  std::printf("stoll=%lld stoul=%lu stoull=%llu\n", std::stoll("-9000000000000"),
              std::stoul("4000000000"), std::stoull("18000000000000000000"));
  const char *dbls[] = {"3.5", "-0.125", "1e10", "2.5e-3xyz", "  7", "inf", "0x1p4"};
  for (const char *x : dbls) {
    std::size_t pos = 0;
    double d = std::stod(x, &pos);
    std::printf("stod(%-10s)=%.6g pos=%zu stof=%.4g\n", x, d, pos, (double)std::stof(x));
  }

  std::printf("== case and characters\n");
  std::string mixed = "Hello, World! 123 abc XYZ";
  std::string up = mixed, low = mixed;
  std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return (char)std::toupper(c); });
  std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  show("upper", up);
  show("lower", low);
  int alpha = 0, digit = 0, space = 0, punct = 0;
  for (unsigned char c : mixed) {
    alpha += std::isalpha(c) != 0;
    digit += std::isdigit(c) != 0;
    space += std::isspace(c) != 0;
    punct += std::ispunct(c) != 0;
  }
  std::printf("alpha=%d digit=%d space=%d punct=%d\n", alpha, digit, space, punct);

  std::printf("== string_view\n");
  std::string_view sv = "  key = value ; other=thing  ";
  std::printf("trimmed=[%.*s]\n", (int)trim(sv).size(), trim(sv).data());
  for (auto part : split(trim(sv), ';')) {
    auto kv = split(part, '=');
    std::printf("part=[%.*s] key=[%.*s] value=[%.*s]\n", (int)part.size(), part.data(),
                (int)trim(kv[0]).size(), trim(kv[0]).data(),
                kv.size() > 1 ? (int)trim(kv[1]).size() : 0, kv.size() > 1 ? trim(kv[1]).data() : "");
  }
  std::string_view v2 = "prefix-body-suffix";
  v2.remove_prefix(7);
  v2.remove_suffix(7);
  std::printf("middle=[%.*s] size=%zu front=%c back=%c\n", (int)v2.size(), v2.data(), v2.size(),
              v2.front(), v2.back());
  std::printf("sv compare: %d %d %d\n", std::string_view("abc") < std::string_view("abd"),
              std::string_view("abc") == "abc", std::string_view("b").compare("a") > 0);
  std::string owner = "owned " + std::string(v2);
  std::printf("owner=%s\n", owner.c_str());

  std::printf("== utilities\n");
  for (const char *x : {"", "a", "aaabbbcccd", "abcd", "zzzzzzzzzzzz", "aabbaa"}) {
    std::string enc = rle(x);
    std::printf("rle(\"%s\")=%s back=%s\n", x, enc.c_str(), unrle(enc).c_str());
  }
  for (int k : {1, 3, 13, -1, 25}) {
    std::string enc = caesar("Attack at Dawn, Zebra!", k);
    std::printf("caesar %3d: %s | back: %s\n", k, enc.c_str(), caesar(enc, -k).c_str());
  }
  for (const char *x : {"one two three", "single", "a b c d e f", " leading and trailing "})
    std::printf("reverseWords(\"%s\")=\"%s\"\n", x, reverseWords(x).c_str());
  std::string pal = "A man, a plan, a canal: Panama";
  std::string clean;
  for (char c : pal)
    if (std::isalnum((unsigned char)c)) clean += (char)std::tolower((unsigned char)c);
  std::printf("palindrome=%d clean=%s\n", std::equal(clean.begin(), clean.begin() + clean.size() / 2, clean.rbegin()),
              clean.c_str());
  return 0;
}
