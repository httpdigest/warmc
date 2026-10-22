// C++ exceptions as a language feature: throw and catch by reference, by
// value and by pointer; handler selection by first match down a class
// hierarchy, including a base handler written before a derived one; throw
// of non-class types (int, const char *, double, enum); rethrow with
// `throw;` preserving the dynamic type, and throwing a different exception
// from a handler; nested try blocks at one level and across frames;
// exceptions propagating through twenty frames each holding an object with a
// destructor, so the unwinder runs every cleanup in order; an exception
// thrown from a constructor, with the already-built members and bases
// destroyed and the object's own destructor not run; function-try-blocks on
// constructors; catch(...); std::uncaught_exceptions observed in a
// destructor during unwinding; noexcept as an operator; and exceptions
// thrown and caught inside loops so the same landing pads run many times.
//
// Built with -fwasm-exceptions and the exnref encoding: each throw is
// libc++abi calling _Unwind_RaiseException, which executes wasm's throw with
// the C++ tag; every frame with a cleanup or handler is a try_table whose
// catch lands in a pad that runs the cleanup and rethrows (throw_ref) or
// selects a handler by comparing type_info.
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string g_trace;

struct Guard {
  std::string name;
  explicit Guard(std::string n) : name(std::move(n)) {}
  ~Guard() {
    g_trace += name;
    g_trace += ' ';
    if (std::uncaught_exceptions() > 0)
      std::printf("  ~Guard %s (unwinding, %d in flight)\n", name.c_str(),
                  std::uncaught_exceptions());
    else
      std::printf("  ~Guard %s\n", name.c_str());
  }
};

// A hierarchy of exception types.
struct AppError : std::exception {
  int code;
  explicit AppError(int c) : code(c) {}
  const char *what() const noexcept override { return "AppError"; }
};
struct IoError : AppError {
  std::string path;
  IoError(int c, std::string p) : AppError(c), path(std::move(p)) {}
  const char *what() const noexcept override { return "IoError"; }
};
struct DiskFull final : IoError {
  long needed;
  DiskFull(std::string p, long n) : IoError(28, std::move(p)), needed(n) {}
  const char *what() const noexcept override { return "DiskFull"; }
};
struct ParseError : AppError {
  int line;
  ParseError(int c, int l) : AppError(c), line(l) {}
  const char *what() const noexcept override { return "ParseError"; }
};

enum class Color { Red, Green, Blue };

void thrower(int which) {
  switch (which) {
    case 0: throw AppError(1);
    case 1: throw IoError(5, "/etc/passwd");
    case 2: throw DiskFull("/var/log", 4096);
    case 3: throw ParseError(22, 17);
    case 4: throw 42;
    case 5: throw "c-string";
    case 6: throw 2.5;
    case 7: throw Color::Blue;
    case 8: throw std::runtime_error("runtime");
    case 9: throw std::string("std::string");
    default: break;
  }
}

std::string classify(int which) {
  try {
    thrower(which);
    return "no throw";
  } catch (const DiskFull &e) {
    return std::string("DiskFull path=") + e.path + " needed=" + std::to_string(e.needed);
  } catch (const IoError &e) {
    return std::string("IoError path=") + e.path + " code=" + std::to_string(e.code);
  } catch (const AppError &e) {
    return std::string("AppError what=") + e.what() + " code=" + std::to_string(e.code);
  } catch (int v) {
    return "int " + std::to_string(v);
  } catch (const char *s) {
    return std::string("const char* ") + s;
  } catch (double d) {
    return "double " + std::to_string(d);
  } catch (Color c) {
    return "Color " + std::to_string((int)c);
  } catch (const std::exception &e) {
    return std::string("std::exception ") + e.what();
  } catch (...) {
    return "catch-all";
  }
}

// Base handler first: a derived exception is caught by it.
std::string baseFirst(int which) {
  try {
    thrower(which);
  } catch (const AppError &e) {
    return std::string("base handler got ") + e.what();
  }
  return "none";
}

// Catch by value slices; catch by reference does not.
std::string sliceCheck() {
  std::string out;
  try {
    throw DiskFull("/tmp", 1);
  } catch (AppError e) {
    out += std::string("by value: ") + e.what();
  }
  try {
    throw DiskFull("/tmp", 1);
  } catch (AppError &e) {
    out += std::string(", by ref: ") + e.what();
  }
  return out;
}

// Rethrow keeps the dynamic type; throwing e copies the static type.
std::string rethrowKinds(bool plain) {
  try {
    try {
      throw DiskFull("/r", 9);
    } catch (AppError &e) {
      e.code += 100;  // modifies the in-flight exception
      if (plain) throw;
      throw e;  // sliced copy
    }
  } catch (DiskFull &d) {
    return "outer got DiskFull code=" + std::to_string(d.code);
  } catch (AppError &a) {
    return std::string("outer got ") + a.what() + " code=" + std::to_string(a.code);
  }
}

// Through many frames with a guard in each.
int deep(int n, int throwAt) {
  Guard g("d" + std::to_string(n));
  if (n == throwAt) throw ParseError(n, n * 10);
  if (n == 0) return 0;
  return deep(n - 1, throwAt) + 1;
}

// Translate between exception types at each level.
void level3(int v) {
  Guard g("L3");
  if (v < 0) throw std::invalid_argument("negative");
  throw std::out_of_range("too big");
}
void level2(int v) {
  Guard g("L2");
  try {
    level3(v);
  } catch (const std::invalid_argument &e) {
    throw AppError(-1);
  }
}
int level1(int v) {
  Guard g("L1");
  try {
    level2(v);
  } catch (const AppError &e) {
    return e.code;
  } catch (const std::logic_error &e) {
    return 1000 + (int)std::strlen(e.what());
  }
  return 0;
}

// Constructor failures.
struct Part {
  std::string name;
  explicit Part(std::string n, bool fail = false) : name(std::move(n)) {
    if (fail) {
      std::printf("  Part %s throwing\n", name.c_str());
      throw std::runtime_error("part " + name + " failed");
    }
    std::printf("  Part %s built\n", name.c_str());
  }
  ~Part() { std::printf("  ~Part %s\n", name.c_str()); }
};
struct Machine : Part {
  Part a, b, c;
  explicit Machine(int failAt)
      : Part("base", failAt == 0), a("a", failAt == 1), b("b", failAt == 2), c("c", failAt == 3) {
    std::printf("  Machine body\n");
    if (failAt == 4) throw std::runtime_error("body failed");
  }
  ~Machine() { std::printf("  ~Machine\n"); }
};

struct Wrapped {
  Part p;
  explicit Wrapped(bool fail) try : p("wrapped", fail) {
    std::printf("  Wrapped body\n");
  } catch (const std::exception &e) {
    std::printf("  function-try-block saw: %s\n", e.what());
    // implicitly rethrown
  }
};

int noexceptFn() noexcept { return 1; }
int mayThrow() { return 2; }
template <class T>
int conditionallyNoexcept(T) noexcept(sizeof(T) <= 4) { return 3; }

}  // namespace

int main() {
  std::printf("== classify\n");
  for (int i = 0; i <= 10; i++) std::printf("%2d: %s\n", i, classify(i).c_str());

  std::printf("== base handler first\n");
  for (int i = 0; i <= 3; i++) std::printf("%d: %s\n", i, baseFirst(i).c_str());

  std::printf("== slicing\n%s\n", sliceCheck().c_str());

  std::printf("== rethrow\n");
  std::printf("throw;   -> %s\n", rethrowKinds(true).c_str());
  std::printf("throw e; -> %s\n", rethrowKinds(false).c_str());

  std::printf("== deep unwinding\n");
  g_trace.clear();
  try {
    deep(20, 3);
  } catch (const ParseError &e) {
    std::printf("caught ParseError code=%d line=%d\n", e.code, e.line);
  }
  std::printf("trace: %s\n", g_trace.c_str());
  g_trace.clear();
  std::printf("deep without throw = %d\n", deep(5, -1));
  std::printf("trace: %s\n", g_trace.c_str());

  std::printf("== translation across frames\n");
  g_trace.clear();
  std::printf("level1(-5) = %d\n", level1(-5));
  std::printf("level1(5) = %d\n", level1(5));
  std::printf("trace: %s\n", g_trace.c_str());

  std::printf("== nested try in one frame\n");
  {
    std::string log;
    for (int i = 0; i < 4; i++) {
      try {
        try {
          try {
            if (i == 0) throw 1;
            if (i == 1) throw std::string("two");
            if (i == 2) throw 3.0;
            log += "[none]";
          } catch (int v) {
            log += "[int " + std::to_string(v) + "]";
            throw std::string("from int handler");
          }
        } catch (const std::string &s) {
          log += "[string " + s + "]";
        }
      } catch (...) {
        log += "[outer ...]";
      }
    }
    std::printf("%s\n", log.c_str());
  }

  std::printf("== constructor failures\n");
  for (int f = 0; f <= 5; f++) {
    std::printf("failAt=%d\n", f);
    try {
      Machine m(f);
      std::printf("  built ok\n");
    } catch (const std::exception &e) {
      std::printf("  caught: %s\n", e.what());
    }
  }
  for (bool fail : {false, true}) {
    try {
      Wrapped w(fail);
      std::printf("  wrapped ok\n");
    } catch (const std::exception &e) {
      std::printf("  rethrown from function-try-block: %s\n", e.what());
    }
  }

  std::printf("== unwinding with guards in flight\n");
  try {
    Guard outer("outer");
    try {
      Guard inner("inner");
      throw AppError(7);
    } catch (AppError &e) {
      Guard handler("in-handler");
      std::printf("  in handler, uncaught=%d\n", std::uncaught_exceptions());
      e.code *= 3;
      throw;
    }
  } catch (AppError &e) {
    std::printf("caught code=%d\n", e.code);
  }

  std::printf("== loops of throws\n");
  {
    long sum = 0;
    int caught[4] = {0, 0, 0, 0};
    for (int i = 0; i < 2000; i++) {
      try {
        Guard *dummy = nullptr;
        (void)dummy;
        if (i % 3 == 0) throw IoError(i, "x");
        if (i % 5 == 0) throw ParseError(i, i);
        if (i % 7 == 0) throw i;
        sum += i;
      } catch (const IoError &e) {
        caught[0]++;
        sum += e.code / 3;
      } catch (const AppError &e) {
        caught[1]++;
        sum -= e.code;
      } catch (int v) {
        caught[2]++;
        sum += v * 2;
      }
    }
    std::printf("sum=%ld io=%d parse=%d int=%d\n", sum, caught[0], caught[1], caught[2]);
  }

  std::printf("== noexcept operator\n");
  std::printf("noexceptFn=%d mayThrow=%d cond<char>=%d cond<double>=%d expr=%d\n",
              (int)noexcept(noexceptFn()), (int)noexcept(mayThrow()),
              (int)noexcept(conditionallyNoexcept('c')),
              (int)noexcept(conditionallyNoexcept(1.0)), (int)noexcept(1 + 2));
  std::printf("vector<int> nothrow move=%d\n",
              (int)noexcept(std::vector<int>(std::declval<std::vector<int>>())));
  return 0;
}
