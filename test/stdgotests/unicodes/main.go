// Text as runes: range over strings (which decodes UTF-8 and yields U+FFFD
// with a width of one for each invalid byte), unicode/utf8 (DecodeRune,
// DecodeLastRune, EncodeRune, AppendRune, RuneLen, ValidString, FullRune,
// RuneCount over truncated and overlong sequences and surrogates),
// unicode/utf16 (Encode, Decode, surrogate pairs, AppendRune, RuneLen),
// unicode character classes (IsLetter, IsDigit, IsSpace, IsPunct, IsUpper,
// IsLower, IsTitle, IsControl, IsMark, IsSymbol, In over scripts), case
// mapping (ToUpper, ToLower, ToTitle, SimpleFold orbits, SpecialCase for
// Turkish), and []rune / string conversions in both directions.
//
// The class tests are binary searches over the package's range tables, and
// the decoder is a table-driven state machine with first-byte lookups; both
// are exercised over every byte value and a spread of code points.
package main

import (
	"fmt"
	"strings"
	"unicode"
	"unicode/utf16"
	"unicode/utf8"
)

func classes(r rune) string {
	var c []string
	add := func(name string, f func(rune) bool) {
		if f(r) {
			c = append(c, name)
		}
	}
	add("L", unicode.IsLetter)
	add("D", unicode.IsDigit)
	add("N", unicode.IsNumber)
	add("S", unicode.IsSpace)
	add("P", unicode.IsPunct)
	add("Up", unicode.IsUpper)
	add("Lo", unicode.IsLower)
	add("Ti", unicode.IsTitle)
	add("C", unicode.IsControl)
	add("M", unicode.IsMark)
	add("Sy", unicode.IsSymbol)
	add("G", unicode.IsGraphic)
	add("Pr", unicode.IsPrint)
	return strings.Join(c, ",")
}

func main() {
	// Range over a string with invalid bytes.
	s := "aé☺😀\xff\xc3(\xe2\x82\xed\xa0\x80z\xf0\x9f\x98"
	for i, r := range s {
		fmt.Printf("range i=%2d r=%U len=%d valid=%v\n", i, r, utf8.RuneLen(r), r != utf8.RuneError)
	}
	fmt.Println("RuneCount", utf8.RuneCountInString(s), "len", len(s), "Valid", utf8.ValidString(s), utf8.ValidString("aé☺😀"))

	// DecodeRune at every offset, forwards and backwards.
	b := []byte(s)
	for i := 0; i < len(b); {
		r, size := utf8.DecodeRune(b[i:])
		fmt.Printf("decode@%2d %U size %d full=%v\n", i, r, size, utf8.FullRune(b[i:]))
		i += size
	}
	for j := len(b); j > 0; {
		r, size := utf8.DecodeLastRune(b[:j])
		fmt.Printf("decodeLast@%2d %U size %d\n", j, r, size)
		j -= size
	}

	// Every first byte value: what the decoder thinks of it alone and followed by continuation bytes.
	var summary strings.Builder
	for v := 0; v < 256; v++ {
		r1, _ := utf8.DecodeRune([]byte{byte(v)})
		r2, s2 := utf8.DecodeRune([]byte{byte(v), 0x90, 0x80, 0x80})
		if v%16 == 0 {
			summary.WriteString(fmt.Sprintf("\n%02x:", v))
		}
		code := "."
		if r1 != utf8.RuneError {
			code = "1"
		}
		if s2 > 1 {
			code += fmt.Sprint(s2)
		} else if r2 == utf8.RuneError {
			code += "x"
		} else {
			code += "-"
		}
		summary.WriteString(" " + code)
	}
	fmt.Println("first-byte table:" + summary.String())

	// Encode.
	for _, r := range []rune{0, 0x7f, 0x80, 0x7ff, 0x800, 0xfffd, 0xffff, 0x10000, 0x10ffff, 0x110000, -5, 0xd800, 0xdfff} {
		buf := make([]byte, 4)
		n := utf8.EncodeRune(buf, r)
		fmt.Printf("encode %U -> % x (len %d, RuneLen %d, ValidRune %v)\n", r, buf[:n], n, utf8.RuneLen(r), utf8.ValidRune(r))
	}
	app := utf8.AppendRune([]byte("x"), 'ß')
	app = utf8.AppendRune(app, 0x1F680)
	fmt.Printf("AppendRune % x %s\n", app, app)
	fmt.Println("RuneStart", utf8.RuneStart(0x80), utf8.RuneStart(0xc3), "FullRuneInString", utf8.FullRuneInString("\xe2\x82"), utf8.FullRuneInString("\xe2\x82\xac"))
	fmt.Println("overlong and surrogates valid?", utf8.Valid([]byte{0xc0, 0x80}), utf8.Valid([]byte{0xe0, 0x80, 0x80}), utf8.Valid([]byte{0xed, 0xa0, 0x80}), utf8.Valid([]byte{0xf4, 0x90, 0x80, 0x80}), utf8.Valid([]byte{0xf4, 0x8f, 0xbf, 0xbf}))

	// Conversions.
	rs := []rune(s)
	fmt.Println("[]rune len", len(rs), "back to string len", len(string(rs)))
	fmt.Printf("string(rune) %q %q %q %q\n", string(rune(65)), string(rune(0x263a)), string(rune(-1)), string(rune(0x110000)))
	fmt.Printf("[]byte of string % x\n", []byte("héllo"))
	fmt.Printf("string([]rune{...}) %q\n", string([]rune{0x48, 0x49, 0xd800, 0x1F600}))

	// UTF-16.
	for _, str := range []string{"abc", "héllo", "😀👍🏽", "\U0010ffff\U00010000"} {
		u := utf16.Encode([]rune(str))
		back := string(utf16.Decode(u))
		fmt.Printf("utf16 %q -> %04x -> %q eq=%v\n", str, u, back, back == str)
	}
	r1, r2 := utf16.EncodeRune(0x1F600)
	fmt.Printf("EncodeRune %04x %04x DecodeRune %U IsSurrogate %v %v\n", r1, r2, utf16.DecodeRune(r1, r2), utf16.IsSurrogate(r1), utf16.IsSurrogate('a'))
	fmt.Printf("lone surrogates decode %U\n", utf16.Decode([]uint16{0xd800, 0x41, 0xdc00, 0xd83d}))
	fmt.Printf("utf16.AppendRune %04x RuneLen %d %d %d\n", utf16.AppendRune(nil, 0x10437), utf16.RuneLen('a'), utf16.RuneLen(0x10437), utf16.RuneLen(0xd800))

	// Character classes across scripts.
	samples := []rune{'a', 'Z', '5', '٣', 'Ⅷ', '½', ' ', '\t', '\u00a0', '\u2028', '\u3000', '!', '¿', '«', '$', '€', '+', '©', '\u0301', '\u0903',
		'ǅ', 'ß', 'Σ', 'ς', 'Я', 'א', 'ع', '中', 'カ', '한', 'ก', '\x00', '\x7f', '\u200b', '\ufeff', 0x1F600, 0xE000, 0x10FFFF}
	for _, r := range samples {
		var scripts []string
		for _, name := range []string{"Latin", "Greek", "Cyrillic", "Hebrew", "Arabic", "Han", "Katakana", "Hangul", "Thai", "Common", "Inherited"} {
			if unicode.Is(unicode.Scripts[name], r) {
				scripts = append(scripts, name)
			}
		}
		fmt.Printf("%U %-14s classes=%-22s scripts=%v\n", r, fmt.Sprintf("%q", r), classes(r), scripts)
	}
	fmt.Println("In(Han, Hiragana)", unicode.In('中', unicode.Han, unicode.Hiragana), unicode.In('a', unicode.Han, unicode.Hiragana))
	fmt.Println("IsOneOf", unicode.IsOneOf([]*unicode.RangeTable{unicode.Letter, unicode.Digit}, '7'))

	// Case mapping.
	for _, r := range []rune{'a', 'A', 'ß', 'ǅ', 'ǆ', 'Ǆ', 'σ', 'ς', 'Σ', 'ÿ', 'Ÿ', 'ı', 'İ', 'K', '\u212a', 'ﬀ', '1', 0x10428, 0x10400} {
		fmt.Printf("case %U %q upper=%q lower=%q title=%q\n", r, r, unicode.ToUpper(r), unicode.ToLower(r), unicode.ToTitle(r))
	}
	// SimpleFold orbits.
	for _, start := range []rune{'k', 'K', 's', 'ǅ', 'θ', 'µ', '1', 'ẞ'} {
		orbit := []string{fmt.Sprintf("%U", start)}
		for r := unicode.SimpleFold(start); r != start; r = unicode.SimpleFold(r) {
			orbit = append(orbit, fmt.Sprintf("%U", r))
		}
		fmt.Println("fold orbit", strings.Join(orbit, " -> "))
	}
	tr := unicode.TurkishCase
	fmt.Printf("Turkish i: upper %q lower(I) %q title %q | strings.ToUpperSpecial %q ToLowerSpecial %q\n",
		tr.ToUpper('i'), tr.ToLower('I'), tr.ToTitle('i'),
		strings.ToUpperSpecial(unicode.TurkishCase, "istanbul ılık"), strings.ToLowerSpecial(unicode.TurkishCase, "İSTANBUL ILIK"))

	// Count classes over a mixed paragraph.
	para := "Hello, Wörld! Καλημέρα κόσμε. Привет, мир! 你好，世界。 こんにちは 123 ٤٥٦ ½ ⅞ — “quotes” ☺😀\t\n"
	counts := map[string]int{}
	for _, r := range para {
		switch {
		case unicode.IsLetter(r):
			counts["letter"]++
		case unicode.IsDigit(r):
			counts["digit"]++
		case unicode.IsNumber(r):
			counts["number"]++
		case unicode.IsSpace(r):
			counts["space"]++
		case unicode.IsPunct(r):
			counts["punct"]++
		case unicode.IsSymbol(r):
			counts["symbol"]++
		default:
			counts["other"]++
		}
	}
	for _, k := range []string{"letter", "digit", "number", "space", "punct", "symbol", "other"} {
		fmt.Printf("paragraph %-6s %d\n", k, counts[k])
	}
	fmt.Println("paragraph runes", utf8.RuneCountInString(para), "bytes", len(para))
	fmt.Println(strings.ToUpper(para[:len(para)-2]))
	fmt.Println(strings.ToLower(para[:len(para)-2]))

	// Reverse a string by runes and by grapheme-agnostic bytes.
	rev := func(s string) string {
		r := []rune(s)
		for i, j := 0, len(r)-1; i < j; i, j = i+1, j-1 {
			r[i], r[j] = r[j], r[i]
		}
		return string(r)
	}
	fmt.Println("reversed", rev("Hello, 世界! 😀"))
	fmt.Println("version", unicode.Version != "")
}
