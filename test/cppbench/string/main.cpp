// std::string building and searching: append numbers and words, find and
// replace inside the result, take substrings and compare them.
//
// cbench/strings is the C library's routines on buffers the program sizes;
// this is std::string doing its own sizing -- the short-string buffer inside
// the object, the reallocation when an append outgrows it, and the find and
// replace members libc++ implements over char_traits rather than over
// strstr and memmove calls.
#include <cstdint>
#include <cstdio>
#include <string>

int main() {
  uint64_t h = 1469598103934665603ull;
  uint32_t x = 362436069u;
  for (int round = 0; round < 6000; round++) {
    std::string s;
    for (int i = 0; i < 40; i++) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      s += std::to_string(x % 100000);
      s += (x & 1) ? " alpha " : " beta ";
    }
    for (size_t at = s.find("beta"); at != std::string::npos; at = s.find("beta", at + 5))
      s.replace(at, 4, "gamma");
    std::string tail = s.substr(s.size() / 2);
    std::string head = s.substr(0, s.size() / 2);
    int c = head.compare(tail);
    size_t digits = 0;
    for (char ch : s) digits += ch >= '0' && ch <= '9';
    h = (h ^ s.size()) * 1099511628211ull;
    h = (h ^ static_cast<uint64_t>(c < 0 ? 1 : c > 0 ? 2 : 3)) * 1099511628211ull;
    h = (h ^ digits) * 1099511628211ull;
    h = (h ^ static_cast<unsigned char>(s[s.size() / 3])) * 1099511628211ull;
  }
  std::printf("h = %016llx\n", static_cast<unsigned long long>(h));
  return 0;
}
