// strconv: integers formatted and parsed in every base from 2 to 36, with
// prefixes, underscores, signs, and the range and syntax errors (whose
// messages are printed); floats formatted in each of the 'b', 'e', 'E', 'f',
// 'g', 'G', 'x' and 'X' formats at several precisions and at 32 and 64 bits,
// and parsed back from decimal, exponent, hexadecimal-mantissa, infinity and
// out-of-range spellings; quoting and unquoting of strings and runes;
// booleans; and the Append* forms.  Float formatting is the Ryu and
// Eisel-Lemire code, heavy on 64x64->128 multiplies and shifts, and the
// round trip f -> string -> f is checked as well as printed.
package main

import (
	"fmt"
	"math"
	"os"
	"strconv"
)

var ints = []int64{0, 1, -1, 7, -8, 35, 36, 255, -256, 1000000, -987654321,
	1<<31 - 1, -1 << 31, 1<<53 + 1, 1<<63 - 1, -1 << 63}

var floats = []float64{0, 1, -1, 0.1, 0.5, 1.0 / 3, 2.0 / 3, 100, 1234.5678,
	1e21, 1e-7, 123456789, 5e-324, 1.7976931348623157e308, math.Pi, -math.E,
	0.000123, 9.999999e9, 2.5, 3.5, 0.125, 1 << 60}

func init() {
	if len(os.Args) > 1000 {
		ints[0], floats[0] = 3, 3
	}
}

func main() {
	fmt.Println("-- FormatInt in every base")
	for _, x := range ints {
		line := strconv.FormatInt(x, 10) + ":"
		for base := 2; base <= 36; base += 1 {
			if base == 2 || base == 3 || base == 7 || base == 8 || base == 16 ||
				base == 20 || base == 32 || base == 36 {
				line += " " + strconv.FormatInt(x, base)
			}
		}
		fmt.Println(line)
		fmt.Println("  u", strconv.FormatUint(uint64(x), 2), strconv.FormatUint(uint64(x), 36))
	}
	fmt.Println("-- round trip through every base")
	bad := 0
	for _, x := range ints {
		for base := 2; base <= 36; base++ {
			s := strconv.FormatInt(x, base)
			y, err := strconv.ParseInt(s, base, 64)
			if err != nil || y != x {
				bad++
				fmt.Println("mismatch", x, base, s, y, err)
			}
			u := strconv.FormatUint(uint64(x), base)
			uy, err := strconv.ParseUint(u, base, 64)
			if err != nil || uy != uint64(x) {
				bad++
				fmt.Println("mismatch u", x, base, u, uy, err)
			}
		}
	}
	fmt.Println("bad", bad)
	fmt.Println("-- ParseInt with prefixes, sizes, errors")
	inputs := []string{"0", "42", "-42", "+42", "0x1F", "0X1f", "-0x80", "0o17",
		"017", "0b1011", "-0b1", "1_000_000", "0x_ff", "1__0", "_1", "", "-",
		"12a", "9223372036854775807", "9223372036854775808",
		"-9223372036854775808", "-9223372036854775809", "127", "128", "-129",
		"32767", "65536", "4294967295", "4294967296", " 1", "z"}
	for _, s := range inputs {
		a, e1 := strconv.ParseInt(s, 0, 64)
		b, e2 := strconv.ParseInt(s, 10, 8)
		c, e3 := strconv.ParseInt(s, 0, 32)
		d, e4 := strconv.ParseUint(s, 0, 16)
		n, e5 := strconv.Atoi(s)
		fmt.Printf("%q: %d %v | %d %v | %d %v | %d %v | %d %v\n", s, a, e1, b, e2,
			c, e3, d, e4, n, e5)
	}
	for _, s := range []string{"zz", "ZZ", "10", "-hello", "Az09"} {
		v, err := strconv.ParseInt(s, 36, 64)
		fmt.Println(s, v, err)
	}
	fmt.Println("-- FormatFloat")
	formats := []byte{'b', 'e', 'E', 'f', 'g', 'G', 'x', 'X'}
	for _, f := range floats {
		fmt.Println(strconv.FormatFloat(f, 'g', -1, 64), strconv.FormatFloat(f, 'g', -1, 32))
		for _, fm := range formats {
			line := "  " + string(fm) + ":"
			for _, prec := range []int{-1, 0, 1, 3, 8, 17} {
				if fm == 'b' && prec != -1 {
					continue
				}
				line += " " + strconv.FormatFloat(f, fm, prec, 64)
			}
			line += " |32 " + strconv.FormatFloat(f, fm, -1, 32) + " " +
				strconv.FormatFloat(f, fm, 4, 32)
			fmt.Println(line)
		}
	}
	fmt.Println("-- special floats")
	for _, f := range []float64{math.Inf(1), math.Inf(-1), math.NaN(), math.Copysign(0, -1)} {
		fmt.Println(strconv.FormatFloat(f, 'g', -1, 64), strconv.FormatFloat(f, 'f', 2, 64),
			strconv.FormatFloat(f, 'e', 3, 32), strconv.FormatFloat(f, 'x', -1, 64))
	}
	fmt.Println("-- ParseFloat")
	finputs := []string{"0", "-0", "1", "1.5", ".5", "5.", "1e10", "1E-10",
		"-2.5e+3", "0x1p-2", "0x1.8p1", "-0X.8p0", "inf", "-Inf", "+INF",
		"infinity", "NaN", "nan", "1e400", "-1e400", "1e-400", "4.9e-324",
		"2.4e-324", "1_000.5", "0.1", "0.30000000000000004",
		"179769313486231570000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
		"123456789012345678901234567890", "1e", "e1", "--1", "0x", "1.2.3",
		"3.4028235e38", "3.4028236e38", "1.401298464324817e-45", "16777217"}
	for _, s := range finputs {
		f64, e1 := strconv.ParseFloat(s, 64)
		f32, e2 := strconv.ParseFloat(s, 32)
		fmt.Printf("%q: %v %v | %v %v\n", s, f64, e1, f32, e2)
	}
	fmt.Println("-- float round trips")
	x := 0.1 + floats[0]
	worst := 0
	for i := 0; i < 300; i++ {
		x = x*1.7 + 1/(x+1)
		if i%50 == 0 {
			x /= 1e5
		}
		for _, fm := range []byte{'e', 'g', 'x'} {
			s := strconv.FormatFloat(x, fm, -1, 64)
			y, err := strconv.ParseFloat(s, 64)
			if err != nil || y != x {
				worst++
				fmt.Println("round trip failed", s, y, err)
			}
		}
		s32 := strconv.FormatFloat(float64(float32(x)), 'g', -1, 32)
		y32, err := strconv.ParseFloat(s32, 32)
		if err != nil || float32(y32) != float32(x) {
			worst++
			fmt.Println("round trip 32 failed", s32, y32, err)
		}
		if i%10 == 0 {
			fmt.Println(i, strconv.FormatFloat(x, 'g', -1, 64), s32,
				strconv.FormatFloat(x, 'e', 5, 64))
		}
	}
	fmt.Println("round trip failures", worst)
	fmt.Println("-- quoting")
	strs := []string{"", "hello", "tab\there", "quote\"s", "back\\slash",
		"new\nline", "héllo", "日本語", "\x00\x01\x7f", "\xff\xfe", "emoji 😀",
		"\u2028", "bell\a"}
	for _, s := range strs {
		q := strconv.Quote(s)
		qa := strconv.QuoteToASCII(s)
		qg := strconv.QuoteToGraphic(s)
		u, err := strconv.Unquote(q)
		u2, err2 := strconv.Unquote(qa)
		fmt.Println(q, qa, qg, u == s, err, u2 == s, err2, strconv.CanBackquote(s))
	}
	for _, r := range []rune{'a', '\'', '"', '\n', 'é', '日', 0x1F600, 0x7f, -1, 0x110000} {
		fmt.Println(strconv.QuoteRune(r), strconv.QuoteRuneToASCII(r), strconv.IsPrint(r))
	}
	for _, s := range []string{`"abc"`, "`raw\\n`", `'x'`, `"\u00e9\x41\101\n"`,
		`"unterminated`, `'ab'`, `"\z"`, `abc`} {
		u, err := strconv.Unquote(s)
		fmt.Printf("%s -> %q %v\n", s, u, err)
	}
	fmt.Println("-- bools and appends")
	for _, s := range []string{"1", "t", "T", "TRUE", "true", "True", "0", "f",
		"F", "FALSE", "false", "False", "yes", ""} {
		b, err := strconv.ParseBool(s)
		fmt.Println(s, b, err)
	}
	buf := []byte("x=")
	buf = strconv.AppendInt(buf, -12345, 10)
	buf = append(buf, ' ')
	buf = strconv.AppendUint(buf, 0xbeef, 16)
	buf = append(buf, ' ')
	buf = strconv.AppendFloat(buf, 3.25, 'f', 3, 64)
	buf = append(buf, ' ')
	buf = strconv.AppendBool(buf, true)
	buf = append(buf, ' ')
	buf = strconv.AppendQuote(buf, "q\"")
	buf = strconv.AppendQuoteRune(buf, '☺')
	fmt.Println(string(buf), strconv.Itoa(-99), strconv.Itoa(int(ints[11])))
}
