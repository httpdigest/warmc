// The standard library's exceptions and the exception-handling library:
// every logic_error and runtime_error subclass thrown by real library calls
// (vector::at, string::substr, std::stoi and friends, bitset, optional,
// variant, any, function, weak_ptr, dynamic_cast, array new with a bad
// length), caught through their bases and their what() printed;
// std::exception_ptr captured with current_exception and
// make_exception_ptr, stored in a container, copied, compared and rethrown
// later with rethrow_exception; std::nested_exception through
// throw_with_nested and rethrow_if_nested, unwound into a chain of messages;
// std::system_error with errc codes; and an error-collecting batch runner
// that converts exceptions to results.
//
// The exception objects are allocated by libc++abi and reference counted
// once an exception_ptr holds them, so a capture made in one frame and
// rethrown from another, after the original handler has finished, exercises
// the runtime's ownership of in-flight exceptions rather than just its
// unwinder.
#include <any>
#include <bitset>
#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <typeinfo>
#include <variant>
#include <vector>

namespace {

struct Base {
  virtual ~Base() = default;
};
struct Derived : Base {};
struct Other : Base {};

volatile std::size_t g_huge = static_cast<std::size_t>(-1) / 2;
int *volatile g_sink;

// Run f, report what it threw.
template <class F>
void probe(const char *label, F f) {
  try {
    f();
    std::printf("%-26s no exception\n", label);
  } catch (const std::out_of_range &e) {
    std::printf("%-26s out_of_range: %s\n", label, e.what());
  } catch (const std::invalid_argument &e) {
    std::printf("%-26s invalid_argument: %s\n", label, e.what());
  } catch (const std::length_error &e) {
    std::printf("%-26s length_error\n", label);
  } catch (const std::logic_error &e) {
    std::printf("%-26s logic_error: %s\n", label, e.what());
  } catch (const std::bad_optional_access &e) {
    std::printf("%-26s bad_optional_access\n", label);
  } catch (const std::bad_variant_access &e) {
    std::printf("%-26s bad_variant_access\n", label);
  } catch (const std::bad_any_cast &e) {
    std::printf("%-26s bad_any_cast\n", label);
  } catch (const std::bad_function_call &e) {
    std::printf("%-26s bad_function_call\n", label);
  } catch (const std::bad_weak_ptr &e) {
    std::printf("%-26s bad_weak_ptr\n", label);
  } catch (const std::bad_cast &e) {
    std::printf("%-26s bad_cast\n", label);
  } catch (const std::bad_array_new_length &e) {
    std::printf("%-26s bad_array_new_length\n", label);
  } catch (const std::bad_alloc &e) {
    std::printf("%-26s bad_alloc\n", label);
  } catch (const std::system_error &e) {
    std::printf("%-26s system_error code=%d\n", label, e.code().value());
  } catch (const std::overflow_error &e) {
    std::printf("%-26s overflow_error: %s\n", label, e.what());
  } catch (const std::range_error &e) {
    std::printf("%-26s range_error: %s\n", label, e.what());
  } catch (const std::runtime_error &e) {
    std::printf("%-26s runtime_error: %s\n", label, e.what());
  } catch (const std::exception &e) {
    std::printf("%-26s exception: %s\n", label, e.what());
  }
}

std::exception_ptr capture(int kind) {
  try {
    switch (kind) {
      case 0: throw std::runtime_error("captured runtime");
      case 1: throw std::out_of_range("captured range");
      case 2: throw 77;
      case 3: throw std::string("captured string");
      default: return nullptr;
    }
  } catch (...) {
    return std::current_exception();
  }
}

std::string describe(std::exception_ptr p) {
  if (!p) return "(null)";
  try {
    std::rethrow_exception(p);
  } catch (const std::exception &e) {
    return std::string("exception: ") + e.what();
  } catch (int v) {
    return "int: " + std::to_string(v);
  } catch (const std::string &s) {
    return "string: " + s;
  }
}

// Nested exceptions: each layer wraps the one below.
void openFile(const std::string &name) {
  throw std::runtime_error("cannot open " + name);
}
void readConfig(const std::string &name) {
  try {
    openFile(name);
  } catch (...) {
    std::throw_with_nested(std::runtime_error("reading config " + name));
  }
}
void startService(int id) {
  try {
    readConfig("svc" + std::to_string(id) + ".conf");
  } catch (...) {
    std::throw_with_nested(std::logic_error("service " + std::to_string(id) + " failed"));
  }
}

void printChain(const std::exception &e, int level = 0) {
  std::printf("%*s%s\n", level * 2, "", e.what());
  try {
    std::rethrow_if_nested(e);
  } catch (const std::exception &inner) {
    printChain(inner, level + 1);
  }
}

struct Result {
  bool ok;
  std::string text;
};

template <class F>
Result attempt(F f) {
  try {
    return {true, f()};
  } catch (const std::exception &e) {
    return {false, e.what()};
  }
}

}  // namespace

int main() {
  std::printf("== library throws\n");
  std::vector<int> v{1, 2, 3};
  std::string s = "hello";
  probe("vector::at(3)", [&] { (void)v.at(3); });
  probe("vector::at(2)", [&] { (void)v.at(2); });
  probe("string::at(10)", [&] { (void)s.at(10); });
  probe("string::substr(6)", [&] { (void)s.substr(6); });
  probe("string::substr(5)", [&] { (void)s.substr(5); });
  probe("stoi(\"abc\")", [] { (void)std::stoi("abc"); });
  probe("stoi(\"99999999999\")", [] { (void)std::stoi("99999999999"); });
  probe("stol(\"  -12x\")", [] { (void)std::stol("  -12x"); });
  probe("stoul(\"\")", [] { (void)std::stoul(""); });
  probe("stod(\"1e999\")", [] { (void)std::stod("1e999"); });
  probe("stod(\"nan-ish\")", [] { (void)std::stod("q1.5"); });
  probe("bitset(\"10x1\")", [] { std::bitset<8> b(std::string("10x1")); });
  probe("bitset.to_ulong overflow", [] {
    std::bitset<64> b;
    b.set(40);
    (void)b.to_ulong();
  });
  probe("bitset.test(70)", [] { (void)std::bitset<64>().test(70); });
  probe("optional.value()", [] {
    std::optional<int> o;
    (void)o.value();
  });
  probe("variant get<1>", [] {
    std::variant<int, std::string> var = 3;
    (void)std::get<1>(var);
  });
  probe("any_cast<double>", [] {
    std::any a = 1;
    (void)std::any_cast<double>(a);
  });
  probe("empty function", [] {
    std::function<void()> f;
    f();
  });
  probe("shared_ptr(expired weak)", [] {
    std::weak_ptr<int> w;
    std::shared_ptr<int> p(w);
  });
  probe("dynamic_cast<Other&>", [] {
    Derived d;
    Base &b = d;
    (void)dynamic_cast<Other &>(b);
  });
  probe("new int[huge]", [] {
    std::size_t n = g_huge;
    int *p = new int[n];
    g_sink = p;  // keeps the allocation from being elided
    delete[] p;
  });
  probe("vector reserve(max+1)", [&] { v.reserve(v.max_size() + 1); });
  probe("string(npos-ish)", [] { std::string big(std::string().max_size() + 1, 'x'); });
  probe("map::at(missing)", [] {
    std::map<int, int> m{{1, 1}};
    (void)m.at(2);
  });
  probe("system_error", [] { throw std::system_error(std::make_error_code(std::errc::invalid_argument)); });
  probe("overflow_error", [] { throw std::overflow_error("too large"); });
  probe("range_error", [] { throw std::range_error("out of range"); });
  probe("domain_error", [] { throw std::domain_error("bad domain"); });

  std::printf("== exception_ptr\n");
  std::vector<std::exception_ptr> saved;
  for (int k = 0; k <= 4; k++) saved.push_back(capture(k));
  for (std::size_t i = 0; i < saved.size(); i++)
    std::printf("saved[%zu] = %s\n", i, describe(saved[i]).c_str());
  std::exception_ptr copy = saved[0];
  std::printf("copy==orig %d, orig==other %d, null==null %d\n", copy == saved[0],
              saved[0] == saved[1], saved[4] == nullptr);
  saved.clear();
  std::printf("copy still alive: %s\n", describe(copy).c_str());
  auto made = std::make_exception_ptr(std::invalid_argument("made directly"));
  std::printf("made: %s\n", describe(made).c_str());
  // rethrow the same exception_ptr twice
  for (int i = 0; i < 2; i++) {
    try {
      std::rethrow_exception(made);
    } catch (std::invalid_argument &e) {
      std::printf("rethrow %d: %s\n", i, e.what());
    }
  }

  std::printf("== nested exceptions\n");
  for (int id = 1; id <= 2; id++) {
    try {
      startService(id);
    } catch (const std::exception &e) {
      printChain(e);
    }
  }
  try {
    try {
      throw 5;
    } catch (...) {
      std::throw_with_nested(std::runtime_error("wrapping an int"));
    }
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    try {
      std::rethrow_if_nested(e);
    } catch (int v) {
      std::printf("  inner int %d\n", v);
    }
  }

  std::printf("== system_error and error codes\n");
  for (auto e : {std::errc::invalid_argument, std::errc::no_such_file_or_directory,
                 std::errc::permission_denied, std::errc::result_out_of_range}) {
    std::error_code ec = std::make_error_code(e);
    std::error_condition cond = ec.default_error_condition();
    std::printf("value=%d category=%s matches=%d\n", ec.value(), ec.category().name(),
                cond == e);
  }
  std::error_code none;
  std::printf("default error_code bool=%d value=%d\n", (bool)none, none.value());

  std::printf("== batch runner\n");
  std::vector<std::string> inputs = {"12", "-7", "abc", "3.75", "99999999999", "", "0x1F", " 42 "};
  int ok = 0;
  for (auto &in : inputs) {
    Result r = attempt([&] {
      std::size_t pos = 0;
      int val = std::stoi(in, &pos, 0);
      return std::to_string(val) + " (consumed " + std::to_string(pos) + ")";
    });
    ok += r.ok;
    std::printf("stoi(\"%s\") -> %s %s\n", in.c_str(), r.ok ? "ok" : "error", r.text.c_str());
  }
  std::printf("ok=%d of %zu\n", ok, inputs.size());
  std::printf("uncaught at end=%d\n", std::uncaught_exceptions());
  return 0;
}
