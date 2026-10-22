// Runes and UTF-8.  `range` over a string decodes as it goes and must turn
// each invalid byte, truncated sequence, overlong encoding and surrogate into
// U+FFFD with a width of 1; the same rules apply in utf8.DecodeRune and
// DecodeLastRune, ValidString, RuneCount, and the []rune(s) conversion.
// Encoding goes the other way: string(rune) and EncodeRune with negative,
// surrogate and past-U+10FFFF values, AppendRune, RuneLen, and UTF-16
// through unicode/utf16 with surrogate pairs.  Every decoded position, rune
// and width is printed.
package main

import (
	"fmt"
	"os"
	"unicode/utf16"
	"unicode/utf8"
)

var samples = []string{
	"",
	"ascii",
	"héllo",
	"日本語",
	"😀👍🏽",
	"a\xffb",
	"\xc3",             // truncated two-byte
	"\xe6\x97",         // truncated three-byte
	"\xf0\x9f\x98",     // truncated four-byte
	"\xc0\xaf",         // overlong '/'
	"\xe0\x80\xaf",     // overlong three-byte
	"\xed\xa0\x80",     // surrogate half D800
	"\xf4\x90\x80\x80", // past U+10FFFF
	"\x80\x80",         // bare continuation bytes
	"mixed é\xffż\xe2\x82",
	"\u00e9\u0301 e\u0301",
	"\U0010FFFF\uFFFD\uFEFF",
}

func init() {
	if len(os.Args) > 1000 {
		samples[0] = "x"
	}
}

//go:noinline
func rangeDecode(s string) string {
	out := ""
	for i, r := range s {
		out += fmt.Sprintf(" %d:%U", i, r)
	}
	return out
}

//go:noinline
func manualDecode(s string) string {
	out := ""
	for i := 0; i < len(s); {
		r, w := utf8.DecodeRuneInString(s[i:])
		out += fmt.Sprintf(" %d:%U/%d", i, r, w)
		i += w
	}
	return out
}

//go:noinline
func backwardDecode(s string) string {
	out := ""
	for i := len(s); i > 0; {
		r, w := utf8.DecodeLastRuneInString(s[:i])
		i -= w
		out += fmt.Sprintf(" %d:%U/%d", i, r, w)
	}
	return out
}

func main() {
	for _, s := range samples {
		fmt.Printf("%q bytes=% x\n", s, []byte(s))
		fmt.Println("  range:", rangeDecode(s))
		fmt.Println("  fwd:  ", manualDecode(s))
		fmt.Println("  back: ", backwardDecode(s))
		rs := []rune(s)
		fmt.Println("  runes:", len(rs), utf8.RuneCountInString(s), utf8.RuneCount([]byte(s)),
			utf8.ValidString(s), utf8.Valid([]byte(s)), utf8.FullRuneInString(s))
		back := string(rs)
		fmt.Printf("  back: %q %v %d\n", back, back == s, len(back))
		n := 0
		for range s {
			n++
		}
		idx := 0
		for i := range s {
			idx += i
		}
		fmt.Println("  counts:", n, idx)
	}
	fmt.Println("-- encoding")
	runes := []rune{0, 'A', 0x7f, 0x80, 0xe9, 0x7ff, 0x800, 0xfffd, 0xffff, 0x10000,
		0x1f600, 0x10ffff, 0x110000, -1, 0xd800, 0xdfff, 0xe000}
	for _, r := range runes {
		var buf [4]byte
		n := utf8.EncodeRune(buf[:], r)
		s := string(r)
		app := utf8.AppendRune([]byte("<"), r)
		fmt.Printf("%U: n=%d % x | %q len=%d | %q | len=%d valid=%v\n", r, n, buf[:n], s,
			len(s), app, utf8.RuneLen(r), utf8.ValidRune(r))
	}
	fmt.Println("-- utf16")
	for _, s := range samples {
		u := utf16.Encode([]rune(s))
		d := string(utf16.Decode(u))
		fmt.Printf("%q -> %04x -> %q\n", s, u, d)
	}
	for _, r := range []rune{0x10000, 0x1f600, 0x10ffff, 'a', 0xd800} {
		r1, r2 := utf16.EncodeRune(r)
		fmt.Printf("%U %04x %04x %U %v\n", r, r1, r2, utf16.DecodeRune(r1, r2), utf16.IsSurrogate(r1))
	}
	bad := []uint16{0xd800, 0x41, 0xdc00, 0xd83d, 0xde00, 0xdbff}
	fmt.Printf("%q\n", string(utf16.Decode(bad)))
	fmt.Println("-- byte and rune slices")
	s := "Grüße, 世界!"
	b := []byte(s)
	r := []rune(s)
	for i := range b {
		b[i] ^= 0x20
	}
	for i := range r {
		r[i]++
	}
	fmt.Printf("%q %q %q\n", s, b, string(r))
	fmt.Println(utf8.RuneStart(s[2]), utf8.RuneStart(s[3]), s[2], s[3])
	// Reverse by rune, keeping multi-byte runes intact.
	rev := func(x string) string {
		rr := []rune(x)
		for i, j := 0, len(rr)-1; i < j; i, j = i+1, j-1 {
			rr[i], rr[j] = rr[j], rr[i]
		}
		return string(rr)
	}
	for _, x := range samples {
		fmt.Printf("%q\n", rev(x))
	}
	// Count bytes per rune width over a constructed text.
	var text []byte
	for i := 0; i < 2000; i++ {
		text = utf8.AppendRune(text, rune(i*37%0x2ffff))
	}
	widths := [5]int{}
	for len(text) > 0 {
		_, w := utf8.DecodeRune(text)
		widths[w]++
		text = text[w:]
	}
	fmt.Println(widths)
}
