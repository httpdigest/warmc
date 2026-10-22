// Static storage duration: dynamic initialisation of globals in the order
// they are defined in this translation unit, before main; constant
// initialisation (constexpr and constinit) that happens before any dynamic
// initialiser runs, so an earlier initialiser can already read a later
// constant-initialised variable (a later dynamically initialised one reads
// as zero, or as its final value if the compiler chose to initialise it
// statically, which the standard allows -- either way it is fixed in the
// module both engines run); function-local statics (a Meyers singleton, statics
// in member functions, lambdas and templates) initialised on first pass
// only; a local static whose initialiser throws and is retried on the next
// call; self-registering factories filled in by global objects' constructors;
// inline static data members of class templates; and destruction at exit in
// the reverse order of construction, interleaved with std::atexit handlers
// registered between them (a handler registered after an object finished
// construction runs before that object's destructor).
//
// The run continues after main returns: the destructor and atexit lines
// printed then are part of the expected output, so the exit path -- the
// runtime's __cxa_atexit list and the stdio flush after it -- is checked too.
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The dynamic initialiser of earlyReader runs before lateDynamic's, but
// after lateConstant's constant initialisation.
extern const int lateConstant;
extern int lateDynamic;

namespace {

int g_order = 0;

struct Global {
  const char *name;
  int order;
  explicit Global(const char *n) : name(n), order(++g_order) {
    std::printf("[init] Global %s #%d\n", name, order);
  }
  ~Global() { std::printf("[exit] ~Global %s #%d\n", name, order); }
};

// Registry for self-registering factories.  The map is a function-local
// static so it exists whenever the first registration happens.
using Factory = std::function<std::string(int)>;
std::map<std::string, Factory> &registry() {
  static std::map<std::string, Factory> r;
  return r;
}
std::vector<std::string> &registrationOrder() {
  static std::vector<std::string> v;
  return v;
}
struct Registrar {
  Registrar(const char *name, Factory f) {
    registry()[name] = std::move(f);
    registrationOrder().push_back(name);
    std::printf("[init] registered %s\n", name);
  }
};

int computeLate() { return 1000 + g_order; }
int readBoth() {
  std::printf("[init] earlyReader sees lateConstant=%d lateDynamic=%d\n", lateConstant, lateDynamic);
  return lateConstant + lateDynamic;
}

}  // namespace

Global g_first("first");
int earlyReader = readBoth();
const int lateConstant = 42;
int lateDynamic = computeLate();
Global g_second("second");

Registrar regSquare("square", [](int x) { return std::to_string(x * x); });
Registrar regHex("hex", [](int x) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%x", x);
  return std::string(b);
});
Registrar regStars("stars", [](int x) { return std::string((std::size_t)(x % 10), '*'); });

constinit int g_constinit = 7;
constexpr int g_constexpr = 6 * 7;

namespace {

struct Config {
  std::string name;
  int version;
  Config() : name("default-config"), version(3) { std::printf("[lazy] Config constructed\n"); }
  ~Config() { std::printf("[exit] ~Config\n"); }
  static Config &instance() {
    static Config c;
    return c;
  }
};

struct Counter {
  static int next() {
    static int n = (std::printf("[lazy] Counter::next static init\n"), 100);
    return n++;
  }
};

int fragileCalls = 0;
int makeFragile() {
  ++fragileCalls;
  std::printf("[lazy] makeFragile attempt %d\n", fragileCalls);
  if (fragileCalls < 3) throw std::runtime_error("not yet");
  return 99;
}
int fragile() {
  static int v = makeFragile();
  return v;
}

template <class T>
struct TypeId {
  static inline int id = (std::printf("[init] TypeId<%s>\n", T::label), ++g_order);
};
struct Alpha {
  static constexpr const char *label = "Alpha";
};
struct Beta {
  static constexpr const char *label = "Beta";
};

template <int N>
int perInstantiation() {
  static int calls = 0;
  return ++calls * N;
}

void atexitA() { std::printf("[exit] atexit A\n"); }
void atexitB() { std::printf("[exit] atexit B\n"); }

struct LateLocal {
  ~LateLocal() { std::printf("[exit] ~LateLocal (function static created in main)\n"); }
};

}  // namespace

Global g_third("third");
int g_alphaId = TypeId<Alpha>::id;

int main() {
  std::printf("== main starts, g_order=%d\n", g_order);
  std::printf("earlyReader=%d lateDynamic=%d constinit=%d constexpr=%d\n", earlyReader, lateDynamic,
              g_constinit, g_constexpr);
  std::printf("globals: %s#%d %s#%d %s#%d\n", g_first.name, g_first.order, g_second.name, g_second.order,
              g_third.name, g_third.order);
  std::printf("alpha id=%d beta id=%d (beta initialised before main too)\n", g_alphaId, TypeId<Beta>::id);

  std::printf("== registry\n");
  for (auto &name : registrationOrder()) std::printf("order: %s\n", name.c_str());
  for (auto &[name, f] : registry())
    for (int x : {3, 12, 255}) std::printf("%s(%d) = %s\n", name.c_str(), x, f(x).c_str());

  std::printf("== function-local statics\n");
  std::printf("before first instance() call\n");
  Config &c1 = Config::instance();
  Config &c2 = Config::instance();
  std::printf("same instance=%d name=%s version=%d\n", &c1 == &c2, c1.name.c_str(), c1.version);
  c1.version = 4;
  std::printf("modified through c1, c2 sees %d\n", c2.version);
  for (int i = 0; i < 3; i++) std::printf("Counter::next()=%d\n", Counter::next());
  auto lam = [] {
    static int hits = 0;
    return ++hits;
  };
  auto lamCopy = lam;
  std::printf("lambda statics shared across copies: %d %d %d\n", lam(), lamCopy(), lam());
  std::printf("template statics: %d %d %d %d\n", perInstantiation<1>(), perInstantiation<10>(),
              perInstantiation<1>(), perInstantiation<10>());

  std::printf("== throwing static initialiser\n");
  for (int i = 0; i < 4; i++) {
    try {
      std::printf("fragile() = %d\n", fragile());
    } catch (const std::exception &e) {
      std::printf("fragile() threw: %s\n", e.what());
    }
  }

  std::printf("== exit ordering setup\n");
  std::atexit(atexitA);
  static LateLocal late;
  std::atexit(atexitB);
  std::printf("main returns\n");
  return 0;
}
