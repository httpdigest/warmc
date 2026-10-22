// <numeric>: accumulate with default and custom operations and with
// non-numeric accumulators, inner_product (plain and generalised),
// partial_sum, adjacent_difference, iota, reduce, transform_reduce (unary
// and binary), inclusive_scan and exclusive_scan with and without an
// initial value and a custom operation, transform_inclusive_scan and
// transform_exclusive_scan, gcd and lcm over signed and unsigned and mixed
// widths, and midpoint for integers, pointers and floating point.
//
// Built on top of those: a polynomial evaluated by inner_product, running
// statistics from scans, a prefix-sum range query table, and std::sort with
// several multi-key comparators driven by the computed values.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

namespace {

template <class T>
void dump(const char *label, const std::vector<T> &v) {
  std::printf("%-30s", label);
  for (auto &x : v) {
    if constexpr (std::is_floating_point_v<T>)
      std::printf(" %.4g", (double)x);
    else
      std::printf(" %lld", (long long)x);
  }
  std::printf("\n");
}

struct Employee {
  std::string name;
  int dept;
  double salary;
  int age;
};

}  // namespace

int main() {
  std::printf("== accumulate\n");
  std::vector<int> v(10);
  std::iota(v.begin(), v.end(), 1);
  dump("iota 1..10", v);
  std::printf("sum=%d product=%lld sumsq=%d\n", std::accumulate(v.begin(), v.end(), 0),
              std::accumulate(v.begin(), v.end(), 1LL, std::multiplies<long long>()),
              std::accumulate(v.begin(), v.end(), 0, [](int a, int b) { return a + b * b; }));
  std::string joined = std::accumulate(std::next(v.begin()), v.end(), std::to_string(v[0]),
                                       [](std::string a, int b) { return a + "-" + std::to_string(b); });
  std::printf("joined=%s\n", joined.c_str());
  std::printf("double acc=%.6f (int init truncates: %d)\n",
              std::accumulate(v.begin(), v.end(), 0.5, [](double a, int b) { return a + 1.0 / b; }),
              std::accumulate(v.begin(), v.end(), 0, [](int a, int b) { return a + 10 / b; }));
  std::vector<char> letters(26);
  std::iota(letters.begin(), letters.end(), 'a');
  std::printf("letters=%s\n", std::string(letters.begin(), letters.end()).c_str());

  std::printf("== inner_product\n");
  std::vector<double> a{1.5, -2.0, 3.25, 0.5}, b{4.0, 1.0, -2.0, 8.0};
  std::printf("dot=%.4f\n", std::inner_product(a.begin(), a.end(), b.begin(), 0.0));
  std::printf("matches=%d\n",
              std::inner_product(v.begin(), v.end(), v.rbegin(), 0, std::plus<>(),
                                 [](int x, int y) { return x + y == 11 ? 1 : 0; }));
  std::printf("max diff=%d\n",
              std::inner_product(v.begin(), v.end(), v.rbegin(), 0,
                                 [](int x, int y) { return std::max(x, y); },
                                 [](int x, int y) { return std::abs(x - y); }));
  // Horner's polynomial evaluation vs inner_product with powers.
  std::vector<double> coef{2, -3, 0, 1.5, -0.25};  // 2 - 3x + 1.5x^3 - 0.25x^4
  for (double x : {-2.0, -0.5, 0.0, 1.0, 2.5}) {
    std::vector<double> pw(coef.size());
    double p = 1;
    for (auto &e : pw) {
      e = p;
      p *= x;
    }
    double viaIP = std::inner_product(coef.begin(), coef.end(), pw.begin(), 0.0);
    double horner = 0;
    for (auto it = coef.rbegin(); it != coef.rend(); ++it) horner = horner * x + *it;
    std::printf("p(%.1f)=%.6f horner=%.6f\n", x, viaIP, horner);
  }

  std::printf("== partial_sum and adjacent_difference\n");
  std::vector<int> ps(10);
  std::partial_sum(v.begin(), v.end(), ps.begin());
  dump("partial_sum", ps);
  std::partial_sum(v.begin(), v.end(), ps.begin(), std::multiplies<>());
  dump("partial product", ps);
  std::vector<int> sq{1, 4, 9, 16, 25, 36, 49};
  std::vector<int> diff(sq.size());
  std::adjacent_difference(sq.begin(), sq.end(), diff.begin());
  dump("adjacent_difference", diff);
  std::adjacent_difference(diff.begin(), diff.end(), diff.begin());
  dump("second difference (in place)", diff);
  std::vector<long long> fib(20);
  fib[0] = 1;
  std::adjacent_difference(fib.begin(), fib.end() - 1, fib.begin() + 1, std::plus<>());
  dump("fibonacci via adjacent_difference", fib);

  std::printf("== reduce and transform_reduce\n");
  std::printf("reduce=%d reduce(init 100)=%d reduce(max)=%d\n", std::reduce(v.begin(), v.end()),
              std::reduce(v.begin(), v.end(), 100),
              std::reduce(v.begin(), v.end(), 0, [](int x, int y) { return std::max(x, y); }));
  std::printf("transform_reduce sumsq=%d\n",
              std::transform_reduce(v.begin(), v.end(), 0, std::plus<>(), [](int x) { return x * x; }));
  std::printf("transform_reduce dot=%.4f\n",
              std::transform_reduce(a.begin(), a.end(), b.begin(), 0.0));
  std::printf("transform_reduce count-odd=%d\n",
              std::transform_reduce(v.begin(), v.end(), 0, std::plus<>(), [](int x) { return x & 1; }));
  std::vector<std::string> ws{"alpha", "be", "gamma", "delta", "epsilon"};
  std::printf("total length=%zu longest=%zu\n",
              std::transform_reduce(ws.begin(), ws.end(), std::size_t(0), std::plus<>(),
                                    [](const std::string &s) { return s.size(); }),
              std::transform_reduce(ws.begin(), ws.end(), std::size_t(0),
                                    [](std::size_t x, std::size_t y) { return std::max(x, y); },
                                    [](const std::string &s) { return s.size(); }));

  std::printf("== scans\n");
  std::vector<int> out(v.size());
  std::inclusive_scan(v.begin(), v.end(), out.begin());
  dump("inclusive_scan", out);
  std::exclusive_scan(v.begin(), v.end(), out.begin(), 0);
  dump("exclusive_scan", out);
  std::inclusive_scan(v.begin(), v.end(), out.begin(), std::multiplies<>(), 1);
  dump("inclusive_scan * init 1", out);
  std::exclusive_scan(v.begin(), v.end(), out.begin(), 1000, std::minus<>());
  dump("exclusive_scan - init 1000", out);
  std::transform_inclusive_scan(v.begin(), v.end(), out.begin(), std::plus<>(),
                                [](int x) { return x % 3; });
  dump("transform_inclusive_scan %3", out);
  std::transform_exclusive_scan(v.begin(), v.end(), out.begin(), 0, std::plus<>(),
                                [](int x) { return x * 2; });
  dump("transform_exclusive_scan *2", out);
  std::vector<int> running{3, -1, 4, -1, 5, -9, 2, 6};
  std::vector<int> rmax(running.size());
  std::inclusive_scan(running.begin(), running.end(), rmax.begin(),
                      [](int x, int y) { return std::max(x, y); });
  dump("running max", rmax);

  std::printf("== prefix-sum queries\n");
  std::vector<long> data;
  uint32_t seed = 4242;
  for (int i = 0; i < 64; i++) {
    seed = seed * 1103515245u + 12345u;
    data.push_back((long)((seed >> 16) % 201) - 100);
  }
  std::vector<long> pre(data.size() + 1, 0);
  std::partial_sum(data.begin(), data.end(), pre.begin() + 1);
  const std::pair<int, int> queries[] = {{0, 64}, {0, 1}, {10, 20}, {31, 33}, {50, 64}, {7, 7}};
  for (auto [l, r] : queries) {
    long viaPre = pre[r] - pre[l];
    long direct = std::accumulate(data.begin() + l, data.begin() + r, 0L);
    std::printf("sum[%2d,%2d)=%5ld direct=%5ld\n", l, r, viaPre, direct);
  }
  // max subarray (Kadane) via a scan-like loop
  long best = data[0], cur = 0;
  for (long x : data) {
    cur = std::max(x, cur + x);
    best = std::max(best, cur);
  }
  std::printf("max subarray sum=%ld\n", best);

  std::printf("== gcd, lcm, midpoint\n");
  int pairs[][2] = {{12, 18}, {-12, 18}, {0, 7}, {0, 0}, {17, 5}, {1071, 462}, {-48, -180}};
  for (auto &p : pairs)
    std::printf("gcd(%d,%d)=%d lcm=%d\n", p[0], p[1], std::gcd(p[0], p[1]), std::lcm(p[0], p[1]));
  std::printf("gcd(u64)=%llu lcm(u64)=%llu mixed gcd=%lld\n",
              (unsigned long long)std::gcd(1ull << 40, 3ull << 35),
              (unsigned long long)std::lcm(1ull << 20, 3ull * 5 * 7),
              (long long)std::gcd(36LL, 24));
  int lcmAll = std::accumulate(v.begin(), v.end(), 1, [](int x, int y) { return std::lcm(x, y); });
  std::printf("lcm(1..10)=%d\n", lcmAll);
  std::printf("midpoint ints: %d %d %d %d\n", std::midpoint(1, 4), std::midpoint(4, 1),
              std::midpoint(-3, 4), std::midpoint(2147483600, 2147483640));
  std::printf("midpoint unsigned: %u %u\n", std::midpoint(0u, 4294967295u),
              std::midpoint(4294967295u, 0u));
  std::printf("midpoint doubles: %.4f %.4g\n", std::midpoint(1.5, 2.25), std::midpoint(1e308, 1e308));
  int arr[11];
  std::printf("midpoint pointers: %ld\n", (long)(std::midpoint(arr, arr + 11) - arr));

  std::printf("== multi-key sorting\n");
  std::vector<Employee> emps = {{"ana", 2, 5200, 31},   {"ben", 1, 4100, 45}, {"cat", 2, 5200, 28},
                                {"dov", 3, 3900, 52},   {"eli", 1, 6100, 38}, {"fio", 3, 3900, 25},
                                {"gil", 2, 4700, 41},   {"hui", 1, 4100, 29}, {"ivy", 3, 7000, 33}};
  auto print = [&](const char *label) {
    std::printf("%s:\n", label);
    for (auto &e : emps)
      std::printf("  %s d%d %.0f %d\n", e.name.c_str(), e.dept, e.salary, e.age);
  };
  std::sort(emps.begin(), emps.end(), [](const Employee &x, const Employee &y) {
    if (x.dept != y.dept) return x.dept < y.dept;
    if (x.salary != y.salary) return x.salary > y.salary;
    return x.age < y.age;
  });
  print("dept asc, salary desc, age asc");
  std::sort(emps.begin(), emps.end(), [](const Employee &x, const Employee &y) {
    return std::tie(x.age, x.name) > std::tie(y.age, y.name);
  });
  print("age desc via tie");
  std::vector<double> deptTotal(4, 0);
  for (auto &e : emps) deptTotal[e.dept] += e.salary;
  std::printf("dept totals: %.0f %.0f %.0f payroll=%.0f\n", deptTotal[1], deptTotal[2],
              deptTotal[3], std::reduce(deptTotal.begin(), deptTotal.end()));
  return 0;
}
