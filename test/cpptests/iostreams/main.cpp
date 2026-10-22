// iostreams formatting and parsing: std::cout with endl and '\n', and
// std::ostringstream with every manipulator that changes output -- setw
// with left/right/internal adjustment and setfill, setprecision under the
// default, fixed, scientific and hexfloat float fields, showpoint, showpos,
// uppercase, hex/oct/dec with showbase, boolalpha -- and the persistence of
// each flag (setw resets after one item, the others stick).  Then
// std::istringstream parsing of mixed fields, getline with delimiters,
// stream states (fail, eof, clear) after bad input, seekg/tellg, and a
// user type with operator<< and operator>> that round-trips.
//
// Every formatted number goes through libc++'s num_put facet and every
// parsed one through num_get, which call into the C library's snprintf and
// strtod; a table of values under each flag combination is printed so that
// a difference in any one of those paths is a one-line diff.
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Point3 {
  double x, y, z;
};

std::ostream &operator<<(std::ostream &os, const Point3 &p) {
  return os << '(' << p.x << ", " << p.y << ", " << p.z << ')';
}

std::istream &operator>>(std::istream &is, Point3 &p) {
  char c;
  if (!(is >> c) || c != '(') {
    is.setstate(std::ios::failbit);
    return is;
  }
  is >> p.x >> c >> p.y >> c >> p.z >> c;
  if (c != ')') is.setstate(std::ios::failbit);
  return is;
}

std::string states(const std::ios &s) {
  std::string r;
  r += s.good() ? 'G' : '-';
  r += s.eof() ? 'E' : '-';
  r += s.fail() ? 'F' : '-';
  r += s.bad() ? 'B' : '-';
  return r;
}

}  // namespace

int main() {
  std::cout << "== cout basics" << std::endl;
  std::cout << "int " << 42 << " negative " << -7 << " char " << 'c' << " bool " << true << '\n';
  std::cout << "double " << 3.14159265358979 << " float " << 2.5f << " big " << 1e21
            << " small " << 1.5e-9 << '\n';
  std::cout << "unsigned " << 4294967295u << " long long " << -9223372036854775807LL
            << " cstring " << "text" << " string " << std::string("str") << std::endl;

  std::cout << "== width and fill\n";
  for (auto adj : {0, 1, 2}) {
    std::ostringstream os;
    if (adj == 0) os << std::left;
    if (adj == 1) os << std::right;
    if (adj == 2) os << std::internal;
    os << '[' << std::setw(8) << -42 << "][" << std::setw(8) << "ab" << "][" << std::setfill('*')
       << std::setw(8) << 3.5 << "][" << std::setw(3) << 12345 << "][" << 7 << ']';
    std::cout << (adj == 0 ? "left    " : adj == 1 ? "right   " : "internal") << ' ' << os.str() << '\n';
  }
  {
    std::ostringstream os;
    os << std::setfill('0') << std::setw(6) << 42 << ' ' << std::setw(6) << -42 << ' '
       << std::internal << std::setw(6) << -42 << ' ' << std::showbase << std::hex << std::setw(8) << 255;
    std::cout << "zero pad " << os.str() << '\n';
  }

  std::cout << "== floating point fields\n";
  const double vals[] = {0.0, 1.0, -1.5, 3.14159265358979, 1234567.891, 0.000123456, 1e100, 2.0 / 3.0};
  for (int prec : {0, 1, 3, 6, 10}) {
    for (int mode = 0; mode < 3; mode++) {
      std::ostringstream os;
      os << std::setprecision(prec);
      if (mode == 1) os << std::fixed;
      if (mode == 2) os << std::scientific;
      for (double v : vals) os << ' ' << v;
      std::cout << "p" << prec << (mode == 0 ? " default " : mode == 1 ? " fixed   " : " sci     ")
                << os.str() << '\n';
    }
  }
  {
    std::ostringstream os;
    os << std::hexfloat << 1.0 << ' ' << 0.1 << ' ' << -3.75 << ' ' << std::defaultfloat << 0.1;
    std::cout << "hexfloat " << os.str() << '\n';
  }
  {
    std::ostringstream os;
    os << std::showpoint << 1.0 << ' ' << 2.5 << ' ' << std::noshowpoint << 1.0 << ' '
       << std::showpos << 5 << ' ' << 0 << ' ' << -5 << ' ' << 2.5 << std::noshowpos << ' '
       << std::uppercase << std::scientific << 12345.678 << ' ' << std::hex << 48879 << ' '
       << std::nouppercase << 48879;
    std::cout << "flags " << os.str() << '\n';
  }

  std::cout << "== integer bases\n";
  for (int v : {0, 7, 8, 255, 4096, -1}) {
    std::ostringstream os;
    os << std::dec << v << ' ' << std::hex << v << ' ' << std::oct << v << ' ' << std::showbase
       << std::hex << v << ' ' << std::oct << v << ' ' << std::uppercase << std::hex << v;
    std::cout << os.str() << '\n';
  }
  {
    std::ostringstream os;
    os << std::boolalpha << true << ' ' << false << ' ' << std::noboolalpha << true << ' ' << false;
    std::cout << "bool " << os.str() << '\n';
  }

  std::cout << "== flag persistence\n";
  {
    std::ostringstream os;
    os << std::setw(5) << 1 << '|' << 2 << '|' << std::hex << 31 << '|' << 31 << '|' << std::dec
       << std::setprecision(3) << 3.14159 << '|' << 2.71828 << '|';
    std::cout << os.str() << '\n';
    std::ios::fmtflags saved = os.flags();
    os.str("");
    os << std::hex << std::uppercase << 0xabc << ' ';
    os.flags(saved);
    os << 0xabc;
    std::cout << "restored flags: " << os.str() << '\n';
  }

  std::cout << "== table\n";
  {
    struct Row {
      const char *item;
      int qty;
      double price;
    };
    Row rows[] = {{"widget", 3, 4.5}, {"gadget", 12, 19.99}, {"thingamajig", 1, 1234.5}, {"nut", 1000, 0.035}};
    std::ostringstream os;
    os << std::left << std::setw(12) << "item" << std::right << std::setw(6) << "qty" << std::setw(12)
       << "price" << std::setw(12) << "total" << '\n';
    double grand = 0;
    for (auto &r : rows) {
      double total = r.qty * r.price;
      grand += total;
      os << std::left << std::setw(12) << r.item << std::right << std::setw(6) << r.qty << std::fixed
         << std::setprecision(3) << std::setw(12) << r.price << std::setprecision(2) << std::setw(12)
         << total << '\n';
    }
    os << std::setfill('-') << std::setw(42) << "" << std::setfill(' ') << '\n'
       << std::left << std::setw(30) << "grand total" << std::right << std::setw(12) << grand;
    std::cout << os.str() << '\n';
  }

  std::cout << "== parsing\n";
  {
    std::istringstream is("42 -17 3.75 word 0x1F 1e3 x");
    int a, b;
    double d;
    std::string w;
    is >> a >> b >> d >> w;
    std::cout << "a=" << a << " b=" << b << " d=" << d << " w=" << w << " state=" << states(is) << '\n';
    int h;
    is >> std::hex >> h >> std::dec;
    double e;
    is >> e;
    std::cout << "hex=" << h << " e=" << e << " state=" << states(is) << '\n';
    int bad = -99;
    is >> bad;
    std::cout << "bad read -> " << bad << " state=" << states(is) << '\n';
    is.clear();
    std::string rest;
    is >> rest;
    std::cout << "after clear rest=" << rest << " state=" << states(is) << '\n';
    is >> rest;
    std::cout << "at end state=" << states(is) << '\n';
  }
  {
    std::istringstream is("name,age,city\nann,31,oslo\nbob,,rome\n\ncid,45,\n");
    std::string line;
    int lineNo = 0;
    while (std::getline(is, line)) {
      std::istringstream ls(line);
      std::string field;
      std::vector<std::string> fields;
      while (std::getline(ls, field, ',')) fields.push_back(field);
      std::cout << "line " << lineNo++ << " fields=" << fields.size() << ':';
      for (auto &f : fields) std::cout << " [" << f << ']';
      std::cout << '\n';
    }
  }
  {
    std::istringstream is("  10   20\t30\n40");
    int sum = 0, n = 0, v;
    while (is >> v) {
      sum += v;
      ++n;
    }
    std::cout << "whitespace-separated n=" << n << " sum=" << sum << " eof=" << is.eof() << '\n';
    std::istringstream nows("abc");
    char c1, c2;
    nows >> std::noskipws >> c1 >> c2;
    std::cout << "noskipws " << c1 << c2 << '\n';
    std::istringstream bools("true false 1 0");
    bool b1, b2, b3, b4;
    bools >> std::boolalpha >> b1 >> b2 >> std::noboolalpha >> b3 >> b4;
    std::cout << "bools " << b1 << b2 << b3 << b4 << '\n';
  }

  std::cout << "== seek and tell\n";
  {
    std::stringstream ss;
    ss << "0123456789abcdef";
    ss.seekg(10);
    char c;
    ss >> c;
    std::cout << "at 10: " << c << " tellg=" << ss.tellg() << '\n';
    ss.seekg(-3, std::ios::end);
    std::string tail;
    ss >> tail;
    std::cout << "last 3: " << tail << '\n';
    ss.clear();
    ss.seekp(4);
    ss << "XY";
    std::cout << "after seekp write: " << ss.str() << " tellp=" << ss.tellp() << '\n';
  }

  std::cout << "== user types\n";
  {
    std::vector<Point3> pts = {{1, 2, 3}, {-0.5, 1e-3, 42}, {1e6, 2.25, -7.125}};
    std::ostringstream os;
    for (auto &p : pts) os << p << ' ';
    std::cout << "written: " << os.str() << '\n';
    std::istringstream is(os.str());
    Point3 q;
    int count = 0;
    while (is >> q) {
      std::cout << "read back " << count++ << ": " << std::setprecision(10) << q << '\n';
    }
    std::istringstream badIn("(1, 2, 3] (4,5,6)");
    badIn >> q;
    std::cout << "malformed state=" << states(badIn) << '\n';
  }

  std::cout << "== numeric round trips\n";
  {
    const double tricky[] = {0.1, 1.0 / 3.0, 123456789.125, 5e-324, 1.7976931348623157e308, -2.5e-5};
    for (double v : tricky) {
      std::ostringstream os;
      os << std::setprecision(17) << v;
      std::istringstream is(os.str());
      double back = 0;
      is >> back;
      std::cout << os.str() << " -> exact=" << (back == v) << '\n';
    }
    std::ostringstream os;
    os << std::setprecision(4) << 123456.0 << ' ' << 0.00001234 << ' ' << 99999.0 << ' ' << 100000.0;
    std::cout << "precision 4: " << os.str() << '\n';
  }
  std::cout << "done" << std::endl;
  return 0;
}
