// The vocabulary types: std::optional (engaged and empty, value_or,
// emplace, reset, comparisons against values and nullopt, optional of a
// non-trivial type with its lifetime logged, optional references through
// pointers), std::variant (index, holds_alternative, get and get_if,
// std::visit with an overload set and with a generic lambda, visiting two
// variants at once, assignment that changes the active alternative and
// destroys the old one, monostate, a variant-based expression tree and a
// state machine), std::any (type(), any_cast to value, reference and
// pointer, reset, small and large payloads), std::tuple and std::pair
// (get by index and by type, tie and ignore, tuple_cat, apply,
// make_from_tuple, comparisons, tuple_size/tuple_element), and structured
// bindings over tuples, pairs, arrays, structs and map entries, by value
// and by reference.
#include <any>
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <class... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

struct Logged {
  std::string tag;
  explicit Logged(std::string t) : tag(std::move(t)) { std::printf("  Logged(%s)\n", tag.c_str()); }
  Logged(const Logged &o) : tag(o.tag + "'") { std::printf("  Logged copy(%s)\n", tag.c_str()); }
  ~Logged() { std::printf("  ~Logged(%s)\n", tag.c_str()); }
};

std::optional<int> parseInt(const std::string &s) {
  if (s.empty()) return std::nullopt;
  int v = 0, sign = 1;
  std::size_t i = 0;
  if (s[0] == '-') {
    sign = -1;
    i = 1;
    if (s.size() == 1) return std::nullopt;
  }
  for (; i < s.size(); i++) {
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
    v = v * 10 + (s[i] - '0');
  }
  return v * sign;
}

std::optional<double> safeDiv(double a, double b) {
  if (b == 0) return {};
  return a / b;
}

// Expression tree with variant nodes.
struct Expr;
using ExprPtr = std::shared_ptr<Expr>;
struct Num {
  double v;
};
struct Var {
  std::string name;
};
struct Bin {
  char op;
  ExprPtr l, r;
};
struct Neg {
  ExprPtr e;
};
struct Expr {
  std::variant<Num, Var, Bin, Neg> node;
};
ExprPtr num(double v) { return std::make_shared<Expr>(Expr{Num{v}}); }
ExprPtr var(std::string n) { return std::make_shared<Expr>(Expr{Var{std::move(n)}}); }
ExprPtr bin(char op, ExprPtr l, ExprPtr r) {
  return std::make_shared<Expr>(Expr{Bin{op, std::move(l), std::move(r)}});
}
ExprPtr neg(ExprPtr e) { return std::make_shared<Expr>(Expr{Neg{std::move(e)}}); }

double eval(const ExprPtr &e, const std::map<std::string, double> &env) {
  return std::visit(Overloaded{
                        [](const Num &n) { return n.v; },
                        [&](const Var &v) {
                          auto it = env.find(v.name);
                          return it == env.end() ? 0.0 : it->second;
                        },
                        [&](const Bin &b) {
                          double x = eval(b.l, env), y = eval(b.r, env);
                          switch (b.op) {
                            case '+': return x + y;
                            case '-': return x - y;
                            case '*': return x * y;
                            default: return y != 0 ? x / y : 0.0;
                          }
                        },
                        [&](const Neg &n) { return -eval(n.e, env); },
                    },
                    e->node);
}

std::string show(const ExprPtr &e) {
  return std::visit(Overloaded{
                        [](const Num &n) {
                          char buf[32];
                          std::snprintf(buf, sizeof buf, "%g", n.v);
                          return std::string(buf);
                        },
                        [](const Var &v) { return v.name; },
                        [](const Bin &b) { return "(" + show(b.l) + " " + b.op + " " + show(b.r) + ")"; },
                        [](const Neg &n) { return "-" + show(n.e); },
                    },
                    e->node);
}

// State machine with variant states.
struct Idle {};
struct Running {
  int ticks;
};
struct Paused {
  int ticks;
  int pauses;
};
struct Done {
  int total;
};
using State = std::variant<Idle, Running, Paused, Done>;

State step(const State &s, char ev) {
  return std::visit(Overloaded{
                        [&](const Idle &) -> State { return ev == 's' ? State(Running{0}) : s; },
                        [&](const Running &r) -> State {
                          if (ev == 't') return Running{r.ticks + 1};
                          if (ev == 'p') return Paused{r.ticks, 1};
                          if (ev == 'x') return Done{r.ticks};
                          return s;
                        },
                        [&](const Paused &p) -> State {
                          if (ev == 'r') return Running{p.ticks};
                          if (ev == 'p') return Paused{p.ticks, p.pauses + 1};
                          if (ev == 'x') return Done{p.ticks};
                          return s;
                        },
                        [&](const Done &) -> State { return s; },
                    },
                    s);
}

const char *stateName(const State &s) {
  static const char *names[] = {"Idle", "Running", "Paused", "Done"};
  return names[s.index()];
}

struct Rgb {
  int r, g, b;
};

}  // namespace

int main() {
  std::printf("== optional\n");
  for (const char *s : {"123", "-45", "", "-", "12a", "0"}) {
    auto v = parseInt(s);
    std::printf("parse(\"%s\") has=%d value_or=%d\n", s, v.has_value(), v.value_or(-999));
  }
  for (auto [a, b] : {std::pair{1.0, 4.0}, std::pair{3.0, 0.0}}) {
    auto r = safeDiv(a, b);
    std::printf("div(%.1f,%.1f) = %s\n", a, b, r ? std::to_string(*r).c_str() : "none");
  }
  std::optional<int> o1 = 5, o2, o3 = 7;
  std::printf("cmp: o1<o3=%d o2<o1=%d o2==nullopt=%d o1==5=%d o1>4=%d o2<0=%d\n", o1 < o3, o2 < o1,
              o2 == std::nullopt, o1 == 5, o1 > 4, o2 < 0);
  {
    std::optional<Logged> ol;
    std::printf("emplace:\n");
    ol.emplace("first");
    std::printf("emplace again:\n");
    ol.emplace("second");
    std::printf("copy optional:\n");
    std::optional<Logged> copy = ol;
    std::printf("reset:\n");
    ol.reset();
    std::printf("has=%d copy has=%d tag=%s\n", ol.has_value(), copy.has_value(), copy->tag.c_str());
    std::printf("leaving scope:\n");
  }
  std::optional<std::string> os = "text";
  os->append("!");
  std::printf("optional string=%s size=%zu\n", os->c_str(), os.value().size());
  std::optional<std::vector<int>> ov(std::in_place, 3, 9);
  std::printf("in_place vector size=%zu [1]=%d\n", ov->size(), (*ov)[1]);

  std::printf("== variant basics\n");
  std::variant<int, double, std::string> v = 42;
  auto describe = [](const auto &var) {
    std::printf("index=%zu int=%d double=%d string=%d", var.index(),
                std::holds_alternative<int>(var), std::holds_alternative<double>(var),
                std::holds_alternative<std::string>(var));
    if (auto p = std::get_if<int>(&var)) std::printf(" -> int %d", *p);
    if (auto p = std::get_if<double>(&var)) std::printf(" -> double %.3f", *p);
    if (auto p = std::get_if<std::string>(&var)) std::printf(" -> string %s", p->c_str());
    std::printf("\n");
  };
  describe(v);
  v = 2.5;
  describe(v);
  v = std::string("hello");
  describe(v);
  std::get<std::string>(v) += " world";
  describe(v);
  v.emplace<0>(-1);
  describe(v);
  auto printer = Overloaded{[](int i) { std::printf("visit int %d\n", i); },
                            [](double d) { std::printf("visit double %.2f\n", d); },
                            [](const std::string &s) { std::printf("visit string %s\n", s.c_str()); }};
  std::vector<std::variant<int, double, std::string>> items = {1, 2.5, "three", 4, "five", 6.75};
  for (auto &it : items) std::visit(printer, it);
  double total = 0;
  for (auto &it : items)
    std::visit([&](auto &&x) {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, std::string>)
        total += x.size();
      else
        total += x;
    }, it);
  std::printf("total=%.2f\n", total);
  std::variant<int, char> va = 'x';
  std::variant<double, bool> vb = true;
  for (int round = 0; round < 2; round++) {
    std::visit([](auto a, auto b) { std::printf("pair visit: %d %d (sizes %zu %zu)\n", (int)a, (int)b,
                                                sizeof(a), sizeof(b)); }, va, vb);
    va = 7;
    vb = 1.5;
  }
  {
    std::variant<std::monostate, Logged, int> vl;
    std::printf("monostate index=%zu\n", vl.index());
    vl.emplace<Logged>("in-variant");
    std::printf("switch to int:\n");
    vl = 3;
    std::printf("index=%zu\n", vl.index());
  }
  std::printf("variant sizes: %zu %zu\n", std::variant_size_v<decltype(v)>,
              std::variant_size_v<std::variant<int, char, long, float>>);

  std::printf("== expression tree\n");
  auto ex = bin('+', bin('*', num(3), var("x")), neg(bin('/', var("y"), num(4))));
  auto ex2 = bin('-', bin('*', var("x"), var("x")), bin('*', num(2), bin('+', var("x"), var("y"))));
  for (double x : {0.0, 1.0, 2.5, -3.0}) {
    std::map<std::string, double> env{{"x", x}, {"y", x * 2 + 1}};
    std::printf("x=%.1f %s = %.4f ; %s = %.4f\n", x, show(ex).c_str(), eval(ex, env),
                show(ex2).c_str(), eval(ex2, env));
  }

  std::printf("== state machine\n");
  State st = Idle{};
  for (char ev : std::string("tsttpprttqxs")) {
    st = step(st, ev);
    std::printf("event %c -> %s", ev, stateName(st));
    if (auto r = std::get_if<Running>(&st)) std::printf(" ticks=%d", r->ticks);
    if (auto p = std::get_if<Paused>(&st)) std::printf(" ticks=%d pauses=%d", p->ticks, p->pauses);
    if (auto d = std::get_if<Done>(&st)) std::printf(" total=%d", d->total);
    std::printf("\n");
  }

  std::printf("== any\n");
  std::vector<std::any> bag = {1, 2.5, std::string("str"), 'c', std::vector<int>{1, 2, 3}, Rgb{1, 2, 3},
                               std::any{}};
  for (auto &a : bag) {
    std::printf("has=%d ", a.has_value());
    if (auto p = std::any_cast<int>(&a)) std::printf("int %d", *p);
    else if (auto p = std::any_cast<double>(&a)) std::printf("double %.2f", *p);
    else if (auto p = std::any_cast<std::string>(&a)) std::printf("string %s", p->c_str());
    else if (auto p = std::any_cast<char>(&a)) std::printf("char %c", *p);
    else if (auto p = std::any_cast<std::vector<int>>(&a)) std::printf("vector size %zu", p->size());
    else if (auto p = std::any_cast<Rgb>(&a)) std::printf("rgb %d,%d,%d", p->r, p->g, p->b);
    else std::printf("empty");
    std::printf(" type=%s\n", a.type().name());
  }
  std::any changer = 10;
  std::any_cast<int &>(changer) += 5;
  std::printf("modified through ref: %d\n", std::any_cast<int>(changer));
  changer = std::string("now a string");
  std::printf("reassigned: %s\n", std::any_cast<const std::string &>(changer).c_str());
  changer.reset();
  std::printf("after reset has=%d\n", changer.has_value());
  {
    std::any al = Logged("any-held");
    std::printf("copying any:\n");
    std::any al2 = al;
    std::printf("leaving scope:\n");
  }

  std::printf("== tuple and pair\n");
  std::tuple<int, std::string, double> t{1, "one", 1.5};
  std::printf("get<0>=%d get<1>=%s get<double>=%.2f size=%zu\n", std::get<0>(t), std::get<1>(t).c_str(),
              std::get<double>(t), std::tuple_size_v<decltype(t)>);
  int ti;
  std::string ts;
  std::tie(ti, ts, std::ignore) = t;
  std::printf("tie: %d %s\n", ti, ts.c_str());
  auto cat = std::tuple_cat(t, std::make_tuple('z', 99L), std::pair<short, bool>{3, true});
  std::printf("tuple_cat size=%zu get<3>=%c get<4>=%ld get<6>=%d\n", std::tuple_size_v<decltype(cat)>,
              std::get<3>(cat), std::get<4>(cat), (int)std::get<6>(cat));
  auto sum3 = [](int a, double b, long c) { return a + b + c; };
  std::printf("apply=%.2f\n", std::apply(sum3, std::make_tuple(1, 2.5, 3L)));
  auto rgb = std::make_from_tuple<Rgb>(std::make_tuple(10, 20, 30));
  std::printf("make_from_tuple: %d %d %d\n", rgb.r, rgb.g, rgb.b);
  std::tuple<int, int, int> ta{1, 2, 3}, tb{1, 3, 0};
  std::printf("compare: < %d == %d > %d\n", ta < tb, ta == tb, ta > tb);
  auto pr = std::make_pair(std::string("key"), 7);
  auto pr2 = pr;
  pr2.second++;
  std::printf("pairs: %s/%d %s/%d less=%d\n", pr.first.c_str(), pr.second, pr2.first.c_str(), pr2.second,
              pr < pr2);
  std::printf("tuple_element is double: %d\n",
              (int)std::is_same_v<std::tuple_element_t<2, decltype(t)>, double>);

  std::printf("== structured bindings\n");
  auto [n1, s1, d1] = t;
  std::printf("by value: %d %s %.1f\n", n1, s1.c_str(), d1);
  auto &[n2, s2, d2] = t;
  n2 = 100;
  s2 = "hundred";
  std::printf("by ref modified tuple: %d %s\n", std::get<0>(t), std::get<1>(t).c_str());
  int arr[3] = {7, 8, 9};
  auto &[a0, a1, a2] = arr;
  a1 = 80;
  std::printf("array: %d %d %d -> arr[1]=%d\n", a0, a1, a2, arr[1]);
  Rgb color{255, 128, 0};
  auto [cr, cg, cb] = color;
  std::printf("struct: %d %d %d\n", cr, cg, cb);
  std::map<std::string, std::pair<int, double>> m{{"a", {1, 1.5}}, {"b", {2, 2.5}}};
  for (auto &[key, val] : m) {
    auto &[iv, dv] = val;
    iv *= 10;
    std::printf("map %s -> %d %.1f\n", key.c_str(), iv, dv);
  }
  std::printf("map a after=%d\n", m["a"].first);
  std::array<std::tuple<int, char>, 3> at{{{1, 'a'}, {2, 'b'}, {3, 'c'}}};
  for (auto [num2, ch] : at) std::printf("%d%c ", num2, ch);
  std::printf("\n");
  return 0;
}
