// strconv, all of it: ParseInt/ParseUint in every base from 2 to 36 and base
// 0 with prefixes and underscores, at the edges of every bit size, with the
// exact *NumError text for syntax and range errors; Atoi/Itoa; FormatInt and
// FormatUint in every base; AppendInt/AppendUint/AppendBool/AppendQuote;
// ParseBool; Quote, QuoteToASCII, QuoteToGraphic, QuoteRune and their Append
// forms over control characters, invalid UTF-8 and astral-plane runes;
// Unquote and UnquoteChar over every escape form; CanBackquote, IsPrint,
// IsGraphic; ParseComplex and FormatComplex.
//
// Integer parsing is a loop of multiply-and-check-overflow per digit; the
// cut-off arithmetic is 64-bit unsigned and the bit-size checks are shifts,
// so the edge values are where a miscompile would show.
package main

import (
	"errors"
	"fmt"
	"strconv"
)

func main() {
	// ParseInt edges for each bit size.
	for _, bits := range []int{8, 16, 32, 64} {
		max := int64(1)<<(bits-1) - 1
		min := -max - 1
		cases := []string{
			strconv.FormatInt(max, 10), strconv.FormatInt(min, 10),
			strconv.FormatInt(max, 10) + "0", "-" + strconv.FormatUint(uint64(max)+2, 10),
		}
		for _, c := range cases {
			v, err := strconv.ParseInt(c, 10, bits)
			fmt.Printf("ParseInt(%q, 10, %d) = %d, %v\n", c, bits, v, err)
		}
		umax := uint64(1)<<bits - 1
		if bits == 64 {
			umax = ^uint64(0)
		}
		for _, c := range []string{strconv.FormatUint(umax, 10), strconv.FormatUint(umax, 10) + "1", "-1"} {
			v, err := strconv.ParseUint(c, 10, bits)
			fmt.Printf("ParseUint(%q, 10, %d) = %d, %v\n", c, bits, v, err)
		}
	}

	// Bases and prefixes.
	inputs := []struct {
		s    string
		base int
	}{
		{"101010", 2}, {"777", 8}, {"ff", 16}, {"FF", 16}, {"zz", 36}, {"Zz", 36}, {"-80", 16},
		{"0x1F", 0}, {"0X1f", 0}, {"0o17", 0}, {"017", 0}, {"0b1011", 0}, {"1_000_000", 0},
		{"0x_ff", 0}, {"1__0", 0}, {"_10", 0}, {"10_", 0}, {"1_0", 10}, {"0x", 0}, {"", 10},
		{"+42", 10}, {"--1", 10}, {" 1", 10}, {"12a", 10}, {"9", 8}, {"1", 1}, {"1", 37},
		{"-9223372036854775808", 0}, {"9223372036854775808", 0}, {"0b" + "1" + string(make([]byte, 0)), 0},
	}
	for _, in := range inputs {
		v, err := strconv.ParseInt(in.s, in.base, 64)
		fmt.Printf("ParseInt(%q, %d) = %d, err=%v\n", in.s, in.base, v, err)
	}

	// Error structure.
	_, err := strconv.Atoi("12x")
	var ne *strconv.NumError
	if errors.As(err, &ne) {
		fmt.Printf("NumError Func=%s Num=%q Err=%v syntax=%v range=%v\n", ne.Func, ne.Num, ne.Err,
			errors.Is(err, strconv.ErrSyntax), errors.Is(err, strconv.ErrRange))
	}
	_, err = strconv.ParseInt("99999999999999999999", 10, 64)
	fmt.Println("range error is ErrRange:", errors.Is(err, strconv.ErrRange), err)

	// Atoi / Itoa.
	for _, s := range []string{"0", "-0", "123", "-456", "+7", "2147483648", "9223372036854775807", "abc", ""} {
		v, err := strconv.Atoi(s)
		fmt.Printf("Atoi(%q) = %d %v | Itoa back %q\n", s, v, err, strconv.Itoa(v))
	}

	// FormatInt / FormatUint in every base.
	for base := 2; base <= 36; base++ {
		fmt.Printf("base %2d: %s %s %s %s\n", base,
			strconv.FormatInt(-1234567890123, base), strconv.FormatUint(^uint64(0), base),
			strconv.FormatInt(-9223372036854775808, base), strconv.FormatInt(int64(base*base*base-1), base))
	}

	// Appends.
	buf := []byte("vals:")
	buf = strconv.AppendInt(buf, -42, 10)
	buf = append(buf, ' ')
	buf = strconv.AppendUint(buf, 0xdead, 16)
	buf = append(buf, ' ')
	buf = strconv.AppendBool(buf, true)
	buf = append(buf, ' ')
	buf = strconv.AppendQuote(buf, "q\"t")
	buf = append(buf, ' ')
	buf = strconv.AppendQuoteRune(buf, '☺')
	buf = append(buf, ' ')
	buf = strconv.AppendQuoteRuneToASCII(buf, '☺')
	buf = append(buf, ' ')
	buf = strconv.AppendFloat(buf, 1.0/3, 'g', 5, 64)
	fmt.Println(string(buf))

	// ParseBool.
	for _, s := range []string{"1", "t", "T", "TRUE", "true", "True", "0", "f", "F", "FALSE", "false", "False", "yes", "tRuE", ""} {
		b, err := strconv.ParseBool(s)
		fmt.Printf("ParseBool(%q) = %v %v\n", s, b, err)
	}

	// Quoting.
	strs := []string{
		"plain", "tab\there", "nl\nand\rcr", "quote\"back\\", "bell\a\b\f\v", "\x00\x01\x1f\x7f",
		"héllo", "日本語", "emoji 😀 astral", "\u00a0nbsp", "\u200bzero-width", "\u2028line-sep",
		"bad\xffutf8", "\xc0\x80", "'single'", "`back`", "\ufeffbom", "\U0010ffff",
	}
	for _, s := range strs {
		fmt.Printf("Quote %s | ASCII %s | Graphic %s | CanBackquote=%v\n",
			strconv.Quote(s), strconv.QuoteToASCII(s), strconv.QuoteToGraphic(s), strconv.CanBackquote(s))
	}
	for _, r := range []rune{'a', '\'', '"', '\\', '\n', 0, 0x7f, 'é', '☺', 0x1F600, 0xFFFD, 0x10FFFF, -1, 0x110000, 0xD800} {
		fmt.Printf("QuoteRune(%d) %s ascii %s graphic %s IsPrint=%v IsGraphic=%v\n", r,
			strconv.QuoteRune(r), strconv.QuoteRuneToASCII(r), strconv.QuoteRuneToGraphic(r), strconv.IsPrint(r), strconv.IsGraphic(r))
	}

	// Unquote.
	quoted := []string{
		`"abc"`, `"a\tb\nc"`, `"\x41\101\u00e9\U0001F600"`, `"\a\b\f\v\\\""`, "`raw\\n`", `'x'`, `'\''`, `'\u263a'`, `'☺'`,
		`"unterminated`, `"bad\q"`, `'ab'`, `"\xff"`, `"\400"`, `"\uD800"`, "`multi\nline`", `""`, `"'"`, `'"'`, `"\'"`,
		"\"tab\tinside\"", `abc`,
	}
	for _, q := range quoted {
		s, err := strconv.Unquote(q)
		fmt.Printf("Unquote(%s) = %q %v\n", q, s, err)
	}
	for _, q := range []string{`\n rest`, `\u263a!`, `☺!`, `é`, `\x7Fz`, `\'`, `\"`, `\z`} {
		v, mb, tail, err := strconv.UnquoteChar(q, '"')
		fmt.Printf("UnquoteChar(%q) = %q mb=%v tail=%q err=%v\n", q, v, mb, tail, err)
	}
	pre, err := strconv.QuotedPrefix(`"hello\" world" and more`)
	fmt.Printf("QuotedPrefix %q %v\n", pre, err)

	// Complex.
	for _, s := range []string{"1+2i", "(3-4i)", "-5i", "6", "1e3+1e-3i", "inf-infi", "NaN+NaNi", "0x1p3+0x1p-1i", "1+2j", "(1+2i"} {
		c, err := strconv.ParseComplex(s, 128)
		fmt.Printf("ParseComplex(%q) = %v %v\n", s, c, err)
	}
	for _, c := range []complex128{complex(1, 2), complex(-1.5, -0.25), complex(1e21, 1e-21)} {
		fmt.Println("FormatComplex", strconv.FormatComplex(c, 'f', 2, 128), strconv.FormatComplex(c, 'g', -1, 128), strconv.FormatComplex(c, 'e', 3, 64))
	}

	// Round trips over many integers.
	sum := 0
	for i := -2000; i <= 2000; i += 7 {
		for base := 2; base <= 36; base += 5 {
			s := strconv.FormatInt(int64(i)*int64(i)*int64(i), base)
			v, err := strconv.ParseInt(s, base, 64)
			if err != nil || v != int64(i)*int64(i)*int64(i) {
				fmt.Println("ROUND TRIP FAIL", i, base, s, v, err)
			}
			sum += len(s)
		}
	}
	fmt.Println("round trips ok, digits", sum)
}
