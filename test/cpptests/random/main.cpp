// <random> with fixed seeds: the raw output of every standard engine
// (minstd_rand0, minstd_rand, mt19937, mt19937_64, ranlux24_base,
// ranlux48_base, ranlux24, knuth_b, default_random_engine), discard(),
// seeding through seed_seq, engine equality and save/restore through
// streams; then the distributions -- uniform_int and uniform_real,
// bernoulli, binomial, geometric, negative_binomial, poisson, exponential,
// gamma, weibull, extreme_value, normal, lognormal, chi_squared, cauchy,
// fisher_f, student_t, discrete, piecewise_constant, piecewise_linear --
// each sampled from a freshly seeded engine with the first values printed
// and a histogram or sample mean of a few thousand draws.
//
// The engines are integer arithmetic (64-bit multiplies and modulo for the
// linear congruential ones, shifts and xors for the twister, subtract with
// borrow for ranlux); the distributions add libm's log, exp, sqrt, pow and
// trig on top, with rejection loops whose iteration count depends on every
// bit of every draw.  The standard fixes the engines' sequences but not the
// distributions' algorithms, and both engines here run libc++'s code, so the
// output is deterministic for this module.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

template <class E>
void engine(const char *name, E e) {
  std::printf("%-22s", name);
  for (int i = 0; i < 5; i++) std::printf(" %llu", (unsigned long long)e());
  e.discard(1000);
  std::printf(" | +1000: %llu\n", (unsigned long long)e());
}

template <class D>
void sample(const char *name, D d, int draws = 4000) {
  std::mt19937 gen(20241022);
  std::printf("%-18s first:", name);
  for (int i = 0; i < 6; i++) {
    auto v = d(gen);
    if constexpr (std::is_integral_v<decltype(v)>)
      std::printf(" %lld", (long long)v);
    else
      std::printf(" %.5f", (double)v);
  }
  double sum = 0, sumsq = 0;
  for (int i = 0; i < draws; i++) {
    double v = (double)d(gen);
    sum += v;
    sumsq += v * v;
  }
  double mean = sum / draws;
  std::printf("  mean=%.4f var=%.4f\n", mean, sumsq / draws - mean * mean);
}

template <class D>
void histogram(const char *name, D d, int lo, int hi, int draws = 2000) {
  std::mt19937_64 gen(777);
  std::vector<int> bins(hi - lo + 1);
  int outside = 0;
  for (int i = 0; i < draws; i++) {
    long long v = (long long)d(gen);
    if (v < lo || v > hi)
      ++outside;
    else
      ++bins[v - lo];
  }
  std::printf("%s:", name);
  for (int i = 0; i <= hi - lo; i++) std::printf(" %d:%d", lo + i, bins[i]);
  std::printf(" out=%d\n", outside);
}

}  // namespace

int main() {
  std::printf("== engines\n");
  engine("minstd_rand0(1)", std::minstd_rand0(1));
  engine("minstd_rand(42)", std::minstd_rand(42));
  engine("mt19937(5489)", std::mt19937(5489u));
  engine("mt19937(1)", std::mt19937(1u));
  engine("mt19937_64(5489)", std::mt19937_64(5489u));
  engine("ranlux24_base(7)", std::ranlux24_base(7));
  engine("ranlux48_base(7)", std::ranlux48_base(7));
  engine("ranlux24(3)", std::ranlux24(3));
  engine("knuth_b(9)", std::knuth_b(9));
  engine("default(123)", std::default_random_engine(123));
  {
    std::mt19937 m;
    m.discard(9999);
    std::printf("mt19937 10000th = %u (standard requires 4123659995)\n", (unsigned)m());
    std::mt19937_64 m64;
    m64.discard(9999);
    std::printf("mt19937_64 10000th = %llu (standard requires 9981545732273789042)\n",
                (unsigned long long)m64());
    std::minstd_rand0 m0;
    m0.discard(9999);
    std::printf("minstd_rand0 10000th = %u (standard requires 1043618065)\n", (unsigned)m0());
    std::ranlux24 rl;
    rl.discard(9999);
    std::printf("ranlux24 10000th = %u (standard requires 9901578)\n", (unsigned)rl());
  }

  std::printf("== seed_seq, equality, state round trip\n");
  {
    std::seed_seq seq{1u, 2u, 3u, 4u, 5u};
    std::vector<uint32_t> out(8);
    seq.generate(out.begin(), out.end());
    std::printf("seed_seq generate:");
    for (auto x : out) std::printf(" %u", x);
    std::printf("\n");
    std::seed_seq seq2{1u, 2u, 3u, 4u, 5u};
    std::mt19937 a(seq2);
    std::printf("mt19937(seed_seq) first: %u %u %u\n", (unsigned)a(), (unsigned)a(), (unsigned)a());
    std::mt19937 x(99), y(99);
    x();
    std::printf("equal after one draw on x: %d\n", x == y);
    y();
    std::printf("equal after one draw on both: %d\n", x == y);
    std::stringstream ss;
    ss << x;
    std::vector<unsigned> fromX;
    for (int i = 0; i < 4; i++) fromX.push_back(x());
    std::mt19937 z;
    ss >> z;
    bool same = true;
    for (int i = 0; i < 4; i++) same &= z() == fromX[i];
    std::printf("restored state reproduces: %d\n", same);
  }

  std::printf("== integer distributions\n");
  sample("uniform_int[1,6]", std::uniform_int_distribution<int>(1, 6));
  sample("uniform_int[-1e9,1e9]", std::uniform_int_distribution<long long>(-1000000000LL, 1000000000LL));
  sample("bernoulli(0.3)", std::bernoulli_distribution(0.3));
  sample("binomial(20,0.25)", std::binomial_distribution<int>(20, 0.25));
  sample("geometric(0.2)", std::geometric_distribution<int>(0.2));
  sample("neg_binomial(3,0.5)", std::negative_binomial_distribution<int>(3, 0.5));
  sample("poisson(4.5)", std::poisson_distribution<int>(4.5));
  sample("poisson(80)", std::poisson_distribution<int>(80.0));
  histogram("dice 2d6", [](auto &g) {
    std::uniform_int_distribution<int> d(1, 6);
    return d(g) + d(g);
  }, 2, 12);
  histogram("binomial(10,.5)", std::binomial_distribution<int>(10, 0.5), 0, 10);
  histogram("poisson(2)", std::poisson_distribution<int>(2.0), 0, 8);
  {
    std::mt19937 g(31337);
    std::discrete_distribution<int> dd{1, 2, 3, 4};
    std::vector<int> counts(4);
    for (int i = 0; i < 10000; i++) counts[dd(g)]++;
    std::printf("discrete{1,2,3,4}: %d %d %d %d probs:", counts[0], counts[1], counts[2], counts[3]);
    for (double p : dd.probabilities()) std::printf(" %.2f", p);
    std::printf("\n");
  }

  std::printf("== real distributions\n");
  sample("uniform_real[0,1)", std::uniform_real_distribution<double>(0.0, 1.0));
  sample("uniform_real[-5,5)", std::uniform_real_distribution<float>(-5.0f, 5.0f));
  sample("exponential(1.5)", std::exponential_distribution<double>(1.5));
  sample("gamma(2,2)", std::gamma_distribution<double>(2.0, 2.0));
  sample("gamma(0.5,1)", std::gamma_distribution<double>(0.5, 1.0));
  sample("weibull(1.5,2)", std::weibull_distribution<double>(1.5, 2.0));
  sample("extreme_value(0,1)", std::extreme_value_distribution<double>(0.0, 1.0));
  sample("normal(10,2)", std::normal_distribution<double>(10.0, 2.0));
  sample("lognormal(0,0.5)", std::lognormal_distribution<double>(0.0, 0.5));
  sample("chi_squared(3)", std::chi_squared_distribution<double>(3.0));
  sample("cauchy(0,1)", std::cauchy_distribution<double>(0.0, 1.0), 200);
  sample("fisher_f(5,10)", std::fisher_f_distribution<double>(5.0, 10.0));
  sample("student_t(5)", std::student_t_distribution<double>(5.0));
  {
    std::vector<double> bounds{0, 1, 3, 6};
    std::vector<double> weights{1, 2, 1};
    sample("piecewise_const", std::piecewise_constant_distribution<double>(bounds.begin(), bounds.end(),
                                                                           weights.begin()));
    std::vector<double> lw{0, 1, 0, 2};
    sample("piecewise_linear", std::piecewise_linear_distribution<double>(bounds.begin(), bounds.end(),
                                                                          lw.begin()));
  }
  {
    std::mt19937 g(4);
    std::normal_distribution<double> nd(0, 1);
    int bins[8] = {};
    for (int i = 0; i < 20000; i++) {
      double v = nd(g);
      int b = (int)((v + 4) / 1.0);
      if (b >= 0 && b < 8) bins[b]++;
    }
    std::printf("normal histogram [-4..4) by 1:");
    for (int bc : bins) std::printf(" %d", bc);
    std::printf("\n");
  }

  std::printf("== generate_canonical and shuffles\n");
  {
    std::mt19937 g(8);
    for (int i = 0; i < 4; i++) std::printf(" %.12f", std::generate_canonical<double, 53>(g));
    std::printf("\n");
    std::vector<int> deck(20);
    for (int i = 0; i < 20; i++) deck[i] = i;
    std::mt19937 g2(2718);
    for (int round = 0; round < 3; round++) {
      std::shuffle(deck.begin(), deck.end(), g2);
      std::printf("shuffle %d:", round);
      for (int c : deck) std::printf(" %d", c);
      std::printf("\n");
    }
    std::vector<int> pick(5);
    std::sample(deck.begin(), deck.end(), pick.begin(), 5, g2);
    std::printf("sample 5:");
    for (int c : pick) std::printf(" %d", c);
    std::printf("\n");
  }

  std::printf("== monte carlo pi\n");
  {
    std::mt19937_64 g(314159);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    int inside = 0;
    const int N = 40000;
    for (int i = 0; i < N; i++) {
      double x = u(g), y = u(g);
      inside += x * x + y * y <= 1.0;
    }
    std::printf("inside=%d pi~%.5f\n", inside, 4.0 * inside / N);
  }
  return 0;
}
