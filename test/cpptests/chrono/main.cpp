// std::chrono arithmetic with no clocks read: durations of every standard
// period and of custom ratios, implicit lossless conversions and explicit
// duration_cast, floor/ceil/round/abs, arithmetic and comparison across
// periods (the common type is the finer one), modulo and division of
// durations, floating-point durations, the literal suffixes, std::ratio
// arithmetic and comparison computed at compile time, time_points on
// system_clock built from fixed epochs, and the C++20 calendar types:
// year_month_day to and from sys_days, weekday, last day of month, leap
// years, adding months and years, and hh_mm_ss splitting.
//
// Duration conversions are integer multiplies and divides by the ratio's
// constants, folded by the compiler where it can see them; values that come
// from a volatile make sure the run-time paths, including 64-bit division,
// are exercised as well.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ratio>

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;

volatile long long g_secs = 98765;
volatile int g_days = 20000;

using Fortnights = duration<long long, std::ratio<14 * 86400>>;
using Jiffies = duration<int64_t, std::ratio<1, 60>>;
using FracHours = duration<double, std::ratio<3600>>;

void printDate(const char *label, year_month_day d) {
  std::printf("%-26s %04d-%02u-%02u ok=%d\n", label, (int)d.year(), (unsigned)d.month(), (unsigned)d.day(),
              d.ok());
}

const char *weekdayName(weekday w) {
  static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return names[w.c_encoding()];
}

}  // namespace

int main() {
  std::printf("== conversions\n");
  seconds s(g_secs);
  std::printf("%lld s = %lld ms = %lld us = %lld ns\n", (long long)s.count(),
              (long long)milliseconds(s).count(), (long long)microseconds(s).count(),
              (long long)nanoseconds(s).count());
  std::printf("duration_cast: %lld min %lld h %lld days\n", (long long)duration_cast<minutes>(s).count(),
              (long long)duration_cast<hours>(s).count(), (long long)duration_cast<days>(s).count());
  for (long long v : {-90LL, -61LL, -60LL, -59LL, -1LL, 0LL, 1LL, 59LL, 60LL, 89LL, 90LL, 91LL, 150LL}) {
    seconds x(v + g_secs * 0);
    std::printf("%5lld s: cast=%3lld floor=%3lld ceil=%3lld round=%3lld min, abs=%lld\n", v,
                (long long)duration_cast<minutes>(x).count(), (long long)floor<minutes>(x).count(),
                (long long)ceil<minutes>(x).count(), (long long)round<minutes>(x).count(),
                (long long)abs(x).count());
  }

  std::printf("== arithmetic across periods\n");
  auto mixed = 1h + 30min + 45s + 250ms;
  std::printf("1h+30min+45s+250ms = %lld ms\n", (long long)mixed.count());
  auto diff = 2h - 125min;
  std::printf("2h-125min = %lld min\n", (long long)diff.count());
  std::printf("3min*7 = %lld min, 100s/7 = %lld s, 100s%%7 = %lld s, 100s/7s = %lld\n",
              (long long)(3min * 7).count(), (long long)(100s / 7).count(), (long long)(100s % 7).count(),
              (long long)(100s / 7s));
  std::printf("1h %% 7min = %lld min, 1h/7min = %lld\n", (long long)(1h % 7min).count(),
              (long long)(1h / 7min));
  std::printf("compare: 1h==60min %d, 61s>1min %d, 999ms<1s %d, 1000ms==1s %d\n", 1h == 60min, 61s > 1min,
              999ms < 1s, 1000ms == 1s);
  seconds acc{0};
  for (int i = 1; i <= 10; i++) {
    acc += seconds(i * i);
    acc -= 3s;
  }
  ++acc;
  acc--;
  std::printf("accumulated %lld s\n", (long long)acc.count());

  std::printf("== custom ratios and floating durations\n");
  Fortnights f(3);
  std::printf("3 fortnights = %lld days = %lld s\n", (long long)duration_cast<days>(f).count(),
              (long long)seconds(f).count());
  Jiffies j = duration_cast<Jiffies>(milliseconds(12345));
  std::printf("12345 ms = %lld jiffies = %lld ms back\n", (long long)j.count(),
              (long long)duration_cast<milliseconds>(j).count());
  FracHours fh = 90min;
  std::printf("90 min = %.4f hours; 1234 s = %.6f h\n", fh.count(), FracHours(1234s).count());
  duration<double> secsD = 1.5h + 0.25s;
  std::printf("1.5h + 0.25s = %.3f s\n", secsD.count());
  duration<double, std::milli> ms = 3.75s;
  std::printf("3.75 s = %.1f ms, as int ms floor %lld\n", ms.count(),
              (long long)floor<milliseconds>(duration<double>(2.9999)).count());
  std::printf("zero=%lld min=%lld max=%lld (seconds)\n", (long long)seconds::zero().count(),
              (long long)seconds::min().count(), (long long)seconds::max().count());

  std::printf("== ratios\n");
  using R1 = std::ratio<2, 6>;
  using R2 = std::ratio<-3, 4>;
  using Sum = std::ratio_add<R1, R2>;
  using Prod = std::ratio_multiply<R1, R2>;
  using Quot = std::ratio_divide<R1, R2>;
  using Diff = std::ratio_subtract<R1, R2>;
  std::printf("R1=%lld/%lld R2=%lld/%lld sum=%lld/%lld prod=%lld/%lld quot=%lld/%lld diff=%lld/%lld\n",
              (long long)R1::num, (long long)R1::den, (long long)R2::num, (long long)R2::den,
              (long long)Sum::num, (long long)Sum::den, (long long)Prod::num, (long long)Prod::den,
              (long long)Quot::num, (long long)Quot::den, (long long)Diff::num, (long long)Diff::den);
  std::printf("less=%d equal(1/3,2/6)=%d kilo=%lld micro den=%lld\n", (int)std::ratio_less_v<R2, R1>,
              (int)std::ratio_equal_v<R1, std::ratio<1, 3>>, (long long)std::kilo::num,
              (long long)std::micro::den);

  std::printf("== time points\n");
  sys_seconds epoch{};
  sys_seconds t1 = epoch + seconds(1700000000);
  sys_days d1 = floor<days>(t1);
  std::printf("t1 days since epoch=%lld, seconds into day=%lld\n", (long long)d1.time_since_epoch().count(),
              (long long)(t1 - d1).count());
  printDate("1700000000 s after epoch", year_month_day{d1});
  hh_mm_ss hms{t1 - d1};
  std::printf("time of day %02lld:%02lld:%02lld\n", (long long)hms.hours().count(),
              (long long)hms.minutes().count(), (long long)hms.seconds().count());
  auto later = t1 + 36h + 15min;
  std::printf("t1+36h15m - t1 = %lld min; later>t1 %d\n", (long long)duration_cast<minutes>(later - t1).count(),
              later > t1);
  time_point<system_clock, milliseconds> tpm = time_point_cast<milliseconds>(t1) + 1500ms;
  std::printf("ms time point count=%lld floor to s=%lld\n", (long long)tpm.time_since_epoch().count(),
              (long long)floor<seconds>(tpm).time_since_epoch().count());
  hh_mm_ss neg{-(5h + 4min + 3s + 21ms)};
  std::printf("negative hh_mm_ss: neg=%d %lld:%lld:%lld.%03lld\n", neg.is_negative(),
              (long long)neg.hours().count(), (long long)neg.minutes().count(), (long long)neg.seconds().count(),
              (long long)neg.subseconds().count());

  std::printf("== calendar\n");
  year_month_day d = 2024y / February / 29;
  printDate("2024-02-29", d);
  printDate("2023-02-29", 2023y / February / 29);
  printDate("2024-02 last", 2024y / February / last);
  printDate("2023-02 last", 2023y / February / last);
  printDate("1900-02 last", 1900y / February / last);
  printDate("2000-02 last", 2000y / February / last);
  for (int y : {1900, 2000, 2023, 2024, 2100, 2400})
    std::printf("leap(%d)=%d ", y, year(y).is_leap());
  std::printf("\n");
  sys_days sd = d;
  std::printf("2024-02-29 is day %lld since epoch, a %s\n", (long long)sd.time_since_epoch().count(),
              weekdayName(weekday{sd}));
  printDate("+1 year (invalid date)", d + years(1));
  printDate("+1 month", d + months(1));
  printDate("+12 months", d + months(12));
  printDate("+30 days", year_month_day{sd + days(30)});
  printDate("-365 days", year_month_day{sd - days(365)});
  printDate("epoch", year_month_day{sys_days{}});
  printDate("volatile days", year_month_day{sys_days{days(g_days)}});
  printDate("day -1", year_month_day{sys_days{days(-1)}});
  printDate("day -719468", year_month_day{sys_days{days(-719468)}});
  for (int m = 1; m <= 12; m++) {
    year_month_day_last last{2025y, month_day_last{month(m)}};
    sys_days first = 2025y / month(m) / 1;
    std::printf("2025-%02d: %u days, starts %s, ends %s\n", m, (unsigned)last.day(),
                weekdayName(weekday{first}), weekdayName(weekday{sys_days{last}}));
  }
  auto thanksgiving = sys_days{2025y / November / Thursday[4]};
  printDate("4th Thursday Nov 2025", year_month_day{thanksgiving});
  auto lastMon = sys_days{2025y / May / Monday[last]};
  printDate("last Monday May 2025", year_month_day{lastMon});
  std::printf("days between 2000-01-01 and 2025-10-22: %lld\n",
              (long long)(sys_days{2025y / October / 22} - sys_days{2000y / January / 1}).count());
  std::printf("weekday arithmetic: Sat+3=%s Mon-2=%s Fri-Tue=%lld\n", weekdayName(Saturday + days(3)),
              weekdayName(Monday - days(2)), (long long)(Friday - Tuesday).count());
  return 0;
}
