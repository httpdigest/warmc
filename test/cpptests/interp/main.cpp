// An integration test: a small interpreter for a toy language, written the
// way a C++ program usually is -- a tokenizer over std::string_view, a
// recursive-descent parser building an AST of std::unique_ptr nodes with
// virtual eval, values as std::variant<long long, double, std::string,
// bool>, environments as std::map chained through shared_ptr, user
// functions with recursion and closures over their defining scope, errors
// reported as exceptions carrying the source line, and output collected in
// a std::ostringstream.  A dozen programs run through it, some of them
// deliberately broken so the error paths unwind through the evaluator's
// recursion.
//
// Nearly every feature of the other tests is exercised at once here, in
// the call patterns a real program has: deep mutual recursion between the
// parser's functions, virtual calls on every node visit, variant copies on
// every value, map lookups up a chain of scopes, and exceptions thrown from
// many frames down.
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

struct Error : std::runtime_error {
  int line;
  Error(const std::string &m, int l) : std::runtime_error(m), line(l) {}
};

// ---------------------------------------------------------------- lexer
enum class Tok { Num, Str, Ident, Op, Kw, End };
struct Token {
  Tok kind;
  std::string text;
  int line;
};

const char *const kKeywords[] = {"let", "fn", "if", "else", "while", "return", "print", "true", "false", "and", "or", "not"};

std::vector<Token> lex(std::string_view src) {
  std::vector<Token> out;
  int line = 1;
  std::size_t i = 0;
  while (i < src.size()) {
    char c = src[i];
    if (c == '\n') {
      ++line;
      ++i;
    } else if (std::isspace((unsigned char)c)) {
      ++i;
    } else if (c == '#') {
      while (i < src.size() && src[i] != '\n') ++i;
    } else if (std::isdigit((unsigned char)c)) {
      std::size_t j = i;
      while (j < src.size() && (std::isdigit((unsigned char)src[j]) || src[j] == '.')) ++j;
      out.push_back({Tok::Num, std::string(src.substr(i, j - i)), line});
      i = j;
    } else if (std::isalpha((unsigned char)c) || c == '_') {
      std::size_t j = i;
      while (j < src.size() && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
      std::string w(src.substr(i, j - i));
      bool kw = false;
      for (const char *k : kKeywords) kw |= w == k;
      out.push_back({kw ? Tok::Kw : Tok::Ident, w, line});
      i = j;
    } else if (c == '"') {
      std::size_t j = i + 1;
      std::string s;
      while (j < src.size() && src[j] != '"') {
        if (src[j] == '\\' && j + 1 < src.size()) {
          ++j;
          s += src[j] == 'n' ? '\n' : src[j];
        } else {
          s += src[j];
        }
        ++j;
      }
      if (j >= src.size()) throw Error("unterminated string", line);
      out.push_back({Tok::Str, s, line});
      i = j + 1;
    } else {
      static const char *two[] = {"==", "!=", "<=", ">=", "->"};
      std::string op(1, c);
      for (const char *t : two)
        if (src.substr(i, 2) == t) op = t;
      if (std::string_view("+-*/%<>=(){},;").find(c) == std::string_view::npos && op.size() == 1)
        throw Error(std::string("unexpected character '") + c + "'", line);
      out.push_back({Tok::Op, op, line});
      i += op.size();
    }
  }
  out.push_back({Tok::End, "", line});
  return out;
}

// ---------------------------------------------------------------- values
struct Function;
using Value = std::variant<long long, double, std::string, bool, std::shared_ptr<Function>>;

std::string show(const Value &v) {
  struct {
    std::string operator()(long long x) const { return std::to_string(x); }
    std::string operator()(double x) const {
      char b[32];
      std::snprintf(b, sizeof b, "%.6g", x);
      return b;
    }
    std::string operator()(const std::string &s) const { return s; }
    std::string operator()(bool b) const { return b ? "true" : "false"; }
    std::string operator()(const std::shared_ptr<Function> &) const { return "<fn>"; }
  } vis;
  return std::visit(vis, v);
}

struct Env {
  std::map<std::string, Value> vars;
  std::shared_ptr<Env> parent;
  Value *find(const std::string &n) {
    for (Env *e = this; e; e = e->parent.get()) {
      auto it = e->vars.find(n);
      if (it != e->vars.end()) return &it->second;
    }
    return nullptr;
  }
};

struct ReturnSignal {
  Value v;
};

struct Ctx {
  std::ostringstream out;
  long steps = 0;
  int depth = 0;
};

// ---------------------------------------------------------------- AST
struct Expr {
  int line;
  explicit Expr(int l) : line(l) {}
  virtual ~Expr() = default;
  virtual Value eval(const std::shared_ptr<Env> &env, Ctx &ctx) const = 0;
};
using ExprP = std::unique_ptr<Expr>;

struct Stmt {
  int line;
  explicit Stmt(int l) : line(l) {}
  virtual ~Stmt() = default;
  virtual void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const = 0;
};
using StmtP = std::unique_ptr<Stmt>;

struct Function {
  std::vector<std::string> params;
  std::vector<StmtP> *body;  // owned by the FnStmt
  std::shared_ptr<Env> closure;
  std::string name;
};

double asDouble(const Value &v, int line) {
  if (auto p = std::get_if<long long>(&v)) return (double)*p;
  if (auto p = std::get_if<double>(&v)) return *p;
  throw Error("expected a number, got " + show(v), line);
}
bool truthy(const Value &v) {
  if (auto p = std::get_if<bool>(&v)) return *p;
  if (auto p = std::get_if<long long>(&v)) return *p != 0;
  if (auto p = std::get_if<std::string>(&v)) return !p->empty();
  return true;
}

struct Lit : Expr {
  Value v;
  Lit(Value x, int l) : Expr(l), v(std::move(x)) {}
  Value eval(const std::shared_ptr<Env> &, Ctx &) const override { return v; }
};
struct Var : Expr {
  std::string name;
  Var(std::string n, int l) : Expr(l), name(std::move(n)) {}
  Value eval(const std::shared_ptr<Env> &env, Ctx &) const override {
    if (Value *p = env->find(name)) return *p;
    throw Error("undefined variable " + name, line);
  }
};
struct Unary : Expr {
  std::string op;
  ExprP e;
  Unary(std::string o, ExprP x, int l) : Expr(l), op(std::move(o)), e(std::move(x)) {}
  Value eval(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    Value v = e->eval(env, ctx);
    if (op == "not") return !truthy(v);
    if (auto p = std::get_if<long long>(&v)) return -*p;
    return -asDouble(v, line);
  }
};
struct Binary : Expr {
  std::string op;
  ExprP l, r;
  Binary(std::string o, ExprP a, ExprP b, int ln) : Expr(ln), op(std::move(o)), l(std::move(a)), r(std::move(b)) {}
  Value eval(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    if (op == "and") {
      Value a = l->eval(env, ctx);
      return truthy(a) ? Value(truthy(r->eval(env, ctx))) : Value(false);
    }
    if (op == "or") {
      Value a = l->eval(env, ctx);
      return truthy(a) ? Value(true) : Value(truthy(r->eval(env, ctx)));
    }
    Value a = l->eval(env, ctx), b = r->eval(env, ctx);
    if (op == "+" && (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)))
      return show(a) + show(b);
    if (op == "==") return show(a) == show(b) && a.index() == b.index();
    if (op == "!=") return !(show(a) == show(b) && a.index() == b.index());
    auto *ia = std::get_if<long long>(&a);
    auto *ib = std::get_if<long long>(&b);
    if (ia && ib) {
      long long x = *ia, y = *ib;
      if (op == "+") return x + y;
      if (op == "-") return x - y;
      if (op == "*") return x * y;
      if (op == "/" || op == "%") {
        if (y == 0) throw Error("division by zero", line);
        return op == "/" ? x / y : x % y;
      }
      if (op == "<") return x < y;
      if (op == ">") return x > y;
      if (op == "<=") return x <= y;
      if (op == ">=") return x >= y;
    }
    double x = asDouble(a, line), y = asDouble(b, line);
    if (op == "+") return x + y;
    if (op == "-") return x - y;
    if (op == "*") return x * y;
    if (op == "/") return x / y;
    if (op == "<") return x < y;
    if (op == ">") return x > y;
    if (op == "<=") return x <= y;
    if (op == ">=") return x >= y;
    throw Error("bad operator " + op, line);
  }
};
struct Call : Expr {
  ExprP callee;
  std::vector<ExprP> args;
  Call(ExprP c, std::vector<ExprP> a, int l) : Expr(l), callee(std::move(c)), args(std::move(a)) {}
  Value eval(const std::shared_ptr<Env> &env, Ctx &ctx) const override;
};

struct ExprStmt : Stmt {
  ExprP e;
  ExprStmt(ExprP x, int l) : Stmt(l), e(std::move(x)) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override { e->eval(env, ctx); }
};
struct Let : Stmt {
  std::string name;
  ExprP e;
  bool declare;
  Let(std::string n, ExprP x, bool d, int l) : Stmt(l), name(std::move(n)), e(std::move(x)), declare(d) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    Value v = e->eval(env, ctx);
    if (declare) {
      env->vars[name] = std::move(v);
      return;
    }
    Value *p = env->find(name);
    if (!p) throw Error("assignment to undeclared " + name, line);
    *p = std::move(v);
  }
};
struct Print : Stmt {
  std::vector<ExprP> es;
  Print(std::vector<ExprP> x, int l) : Stmt(l), es(std::move(x)) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    for (std::size_t i = 0; i < es.size(); i++) ctx.out << (i ? " " : "") << show(es[i]->eval(env, ctx));
    ctx.out << '\n';
  }
};
void runBlock(const std::vector<StmtP> &b, const std::shared_ptr<Env> &env, Ctx &ctx) {
  for (auto &s : b) {
    if (++ctx.steps > 2000000) throw Error("step limit exceeded", s->line);
    s->exec(env, ctx);
  }
}
struct If : Stmt {
  ExprP cond;
  std::vector<StmtP> then, otherwise;
  If(int l) : Stmt(l) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    auto scope = std::make_shared<Env>();
    scope->parent = env;
    runBlock(truthy(cond->eval(env, ctx)) ? then : otherwise, scope, ctx);
  }
};
struct While : Stmt {
  ExprP cond;
  std::vector<StmtP> body;
  While(int l) : Stmt(l) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    while (truthy(cond->eval(env, ctx))) {
      auto scope = std::make_shared<Env>();
      scope->parent = env;
      runBlock(body, scope, ctx);
    }
  }
};
struct Return : Stmt {
  ExprP e;
  Return(ExprP x, int l) : Stmt(l), e(std::move(x)) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &ctx) const override {
    throw ReturnSignal{e ? e->eval(env, ctx) : Value(0LL)};
  }
};
struct FnStmt : Stmt {
  std::string name;
  std::vector<std::string> params;
  std::vector<StmtP> body;
  FnStmt(int l) : Stmt(l) {}
  void exec(const std::shared_ptr<Env> &env, Ctx &) const override {
    auto f = std::make_shared<Function>();
    f->params = params;
    f->body = const_cast<std::vector<StmtP> *>(&body);
    f->closure = env;
    f->name = name;
    env->vars[name] = f;
  }
};

Value Call::eval(const std::shared_ptr<Env> &env, Ctx &ctx) const {
  Value c = callee->eval(env, ctx);
  auto *fp = std::get_if<std::shared_ptr<Function>>(&c);
  if (!fp) throw Error("calling a non-function " + show(c), line);
  auto f = *fp;
  if (f->params.size() != args.size())
    throw Error(f->name + " expects " + std::to_string(f->params.size()) + " arguments, got " +
                    std::to_string(args.size()),
                line);
  auto scope = std::make_shared<Env>();
  scope->parent = f->closure;
  for (std::size_t i = 0; i < args.size(); i++) scope->vars[f->params[i]] = args[i]->eval(env, ctx);
  if (++ctx.depth > 60) {
    ctx.depth = 0;
    throw Error("recursion too deep in " + f->name, line);
  }
  try {
    runBlock(*f->body, scope, ctx);
  } catch (ReturnSignal &r) {
    --ctx.depth;
    return std::move(r.v);
  }
  --ctx.depth;
  return 0LL;
}

// ---------------------------------------------------------------- parser
class Parser {
  std::vector<Token> t_;
  std::size_t p_ = 0;

  const Token &peek() const { return t_[p_]; }
  bool is(const char *s) const { return (peek().kind == Tok::Op || peek().kind == Tok::Kw) && peek().text == s; }
  Token take() { return t_[p_++]; }
  void expect(const char *s) {
    if (!is(s)) throw Error(std::string("expected '") + s + "' but found '" + peek().text + "'", peek().line);
    ++p_;
  }
  std::string ident() {
    if (peek().kind != Tok::Ident) throw Error("expected identifier, found '" + peek().text + "'", peek().line);
    return take().text;
  }

  ExprP primary() {
    const Token &tk = peek();
    int line = tk.line;
    if (tk.kind == Tok::Num) {
      std::string s = take().text;
      if (s.find('.') != std::string::npos) return std::make_unique<Lit>(std::stod(s), line);
      return std::make_unique<Lit>(std::stoll(s), line);
    }
    if (tk.kind == Tok::Str) return std::make_unique<Lit>(take().text, line);
    if (is("true") || is("false")) return std::make_unique<Lit>(take().text == "true", line);
    if (tk.kind == Tok::Ident) return std::make_unique<Var>(take().text, line);
    if (is("(")) {
      ++p_;
      auto e = expr();
      expect(")");
      return e;
    }
    throw Error("unexpected '" + tk.text + "'", line);
  }
  ExprP postfix() {
    auto e = primary();
    while (is("(")) {
      int line = peek().line;
      ++p_;
      std::vector<ExprP> args;
      if (!is(")")) {
        do args.push_back(expr());
        while (is(",") && (++p_, true));
      }
      expect(")");
      e = std::make_unique<Call>(std::move(e), std::move(args), line);
    }
    return e;
  }
  ExprP unary() {
    if (is("-") || is("not")) {
      int line = peek().line;
      std::string op = take().text;
      return std::make_unique<Unary>(op, unary(), line);
    }
    return postfix();
  }
  template <class Next>
  ExprP binaryLevel(Next next, std::initializer_list<const char *> ops) {
    auto e = (this->*next)();
    while (true) {
      const char *hit = nullptr;
      for (const char *o : ops)
        if (is(o)) hit = o;
      if (!hit) return e;
      int line = peek().line;
      ++p_;
      e = std::make_unique<Binary>(hit, std::move(e), (this->*next)(), line);
    }
  }
  ExprP mul() { return binaryLevel(&Parser::unary, {"*", "/", "%"}); }
  ExprP add() { return binaryLevel(&Parser::mul, {"+", "-"}); }
  ExprP cmp() { return binaryLevel(&Parser::add, {"<", ">", "<=", ">=", "==", "!="}); }
  ExprP conj() { return binaryLevel(&Parser::cmp, {"and"}); }
  ExprP expr() { return binaryLevel(&Parser::conj, {"or"}); }

  std::vector<StmtP> block() {
    expect("{");
    std::vector<StmtP> out;
    while (!is("}")) {
      if (peek().kind == Tok::End) throw Error("unterminated block", peek().line);
      out.push_back(stmt());
    }
    ++p_;
    return out;
  }
  StmtP stmt() {
    int line = peek().line;
    if (is("let")) {
      ++p_;
      std::string n = ident();
      expect("=");
      auto e = expr();
      expect(";");
      return std::make_unique<Let>(n, std::move(e), true, line);
    }
    if (is("print")) {
      ++p_;
      std::vector<ExprP> es;
      do es.push_back(expr());
      while (is(",") && (++p_, true));
      expect(";");
      return std::make_unique<Print>(std::move(es), line);
    }
    if (is("if")) {
      ++p_;
      auto s = std::make_unique<If>(line);
      s->cond = expr();
      s->then = block();
      if (is("else")) {
        ++p_;
        if (is("if")) {
          s->otherwise.push_back(stmt());
        } else {
          s->otherwise = block();
        }
      }
      return s;
    }
    if (is("while")) {
      ++p_;
      auto s = std::make_unique<While>(line);
      s->cond = expr();
      s->body = block();
      return s;
    }
    if (is("return")) {
      ++p_;
      ExprP e;
      if (!is(";")) e = expr();
      expect(";");
      return std::make_unique<Return>(std::move(e), line);
    }
    if (is("fn")) {
      ++p_;
      auto s = std::make_unique<FnStmt>(line);
      s->name = ident();
      expect("(");
      if (!is(")")) {
        do s->params.push_back(ident());
        while (is(",") && (++p_, true));
      }
      expect(")");
      s->body = block();
      return s;
    }
    if (peek().kind == Tok::Ident && t_[p_ + 1].kind == Tok::Op && t_[p_ + 1].text == "=") {
      std::string n = take().text;
      ++p_;
      auto e = expr();
      expect(";");
      return std::make_unique<Let>(n, std::move(e), false, line);
    }
    auto e = expr();
    expect(";");
    return std::make_unique<ExprStmt>(std::move(e), line);
  }

 public:
  explicit Parser(std::vector<Token> t) : t_(std::move(t)) {}
  std::vector<StmtP> program() {
    std::vector<StmtP> out;
    while (peek().kind != Tok::End) out.push_back(stmt());
    return out;
  }
};

void run(const char *title, const char *src) {
  std::printf("--- %s\n", title);
  Ctx ctx;
  try {
    Parser p(lex(src));
    auto prog = p.program();
    auto global = std::make_shared<Env>();
    runBlock(prog, global, ctx);
    std::printf("%s", ctx.out.str().c_str());
    std::printf("[ok, %ld steps]\n", ctx.steps);
  } catch (const Error &e) {
    std::printf("%s", ctx.out.str().c_str());
    std::printf("[error at line %d: %s]\n", e.line, e.what());
  } catch (const ReturnSignal &) {
    std::printf("[error: return outside function]\n");
  }
}

}  // namespace

int main() {
  run("arithmetic", R"(
    print 1 + 2 * 3, (1 + 2) * 3, 7 / 2, 7 % 3, -7 / 2, 2.5 * 4, 1 / 3.0;
    print "con" + "cat", "n=" + 42, 10 > 3, 3 >= 3, 2 == 2.0, "a" != "b";
    print not true, true and false, false or 1, 0 or "", -(-5);
  )");
  run("variables and loops", R"(
    let sum = 0;
    let i = 1;
    while i <= 100 { sum = sum + i; i = i + 1; }
    print "sum 1..100 =", sum;
    let fact = 1;
    let n = 1;
    while n <= 20 { fact = fact * n; n = n + 1; }
    print "20! =", fact;
    let x = 27; let steps = 0;
    while x != 1 { if x % 2 == 0 { x = x / 2; } else { x = 3 * x + 1; } steps = steps + 1; }
    print "collatz(27) steps:", steps;
  )");
  run("recursion", R"(
    fn fib(n) { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); }
    let k = 0;
    while k <= 20 { print "fib", k, "=", fib(k); k = k + 5; }
    fn ack(m, n) {
      if m == 0 { return n + 1; }
      if n == 0 { return ack(m - 1, 1); }
      return ack(m - 1, ack(m, n - 1));
    }
    print "ack(2,3) =", ack(2, 3);
    fn even(n) { if n == 0 { return true; } return odd(n - 1); }
    fn odd(n) { if n == 0 { return false; } return even(n - 1); }
    print even(10), odd(7), even(7);
  )");
  run("closures", R"(
    fn makeCounter(start) {
      let count = start;
      fn next() { count = count + 1; return count; }
      return next;
    }
    let a = makeCounter(10);
    let b = makeCounter(100);
    print a(), a(), b(), a(), b();
    fn compose(f, g) { fn h(x) { return f(g(x)); } return h; }
    fn inc(x) { return x + 1; }
    fn dbl(x) { return x * 2; }
    let incThenDbl = compose(dbl, inc);
    print incThenDbl(5), compose(inc, dbl)(5), compose(incThenDbl, incThenDbl)(1);
  )");
  run("primes", R"(
    fn isPrime(n) {
      if n < 2 { return false; }
      let d = 2;
      while d * d <= n { if n % d == 0 { return false; } d = d + 1; }
      return true;
    }
    let n = 0; let found = 0; let line = "";
    while found < 25 { if isPrime(n) { line = line + n + " "; found = found + 1; } n = n + 1; }
    print line;
  )");
  run("floating point", R"(
    fn sqrt(x) { let g = x; let i = 0; while i < 30 { g = (g + x / g) / 2; i = i + 1; } return g; }
    print sqrt(2.0), sqrt(10.0), sqrt(144.0);
    let pi = 0.0; let k = 0; let sign = 1.0;
    while k < 1000 { pi = pi + sign * 4.0 / (2 * k + 1); sign = -sign; k = k + 1; }
    print "pi ~", pi;
  )");
  run("strings", R"(
    let s = "";
    let i = 0;
    while i < 5 { s = s + i + ","; i = i + 1; }
    print s;
    print "line one\nline two", "quote: \"q\"";
  )");
  run("division by zero", R"(
    fn f(x) { return 10 / x; }
    print f(5);
    print f(0);
    print "not reached";
  )");
  run("undefined variable deep in recursion", R"(
    fn down(n) { if n == 0 { return missing; } return down(n - 1); }
    print "start";
    print down(30);
  )");
  run("arity error", R"(
    fn two(a, b) { return a + b; }
    print two(1, 2);
    print two(1);
  )");
  run("runaway recursion", R"(
    fn forever(n) { return forever(n + 1); }
    print forever(0);
  )");
  run("syntax error", R"(
    let x = 1;
    let y = (x + 2;
    print y;
  )");
  run("bad character", "let a = 1;\nlet b = a @ 2;\n");
  run("unterminated string", "print \"oops;\n");
  run("type error", R"(
    let s = "text";
    print s * 2;
  )");
  return 0;
}
