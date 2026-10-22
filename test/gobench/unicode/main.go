// The unicode package's character classes and case mapping over text in
// several scripts: IsUpper, IsLower, IsLetter, IsDigit, IsSpace, IsPunct and
// IsSymbol on every rune, then ToUpper and ToLower with utf8.AppendRune to
// build a case-swapped copy, and swap it back.  Each rune comes from
// runtime.stringNext, TinyGo's decoder for range over a string.  A Latin-1
// rune is answered by a byte load from a 256-entry property table; anything
// above is a call to a search over one of the package's range tables
// (`unicode.is16`, `is32`, `isExcludingLatin`), whose stride check is a
// remainder.  Case mapping is a binary search over the CaseRanges table,
// inlined into the caller.
//
// utf8scan only decodes.  This is what a decoded rune is then used for, and
// the table searches make it a branchy loop over read-only data whose path
// depends on which script is being read.
package main

import (
	"unicode"
	"unicode/utf8"
)

var samples = []string{
	"The quick brown fox jumps over the lazy dog. ",
	"Größe, Maß und Übermut; ",
	"Ελληνικά γράμματα ΑΒΓΔ; ",
	"Кириллица и ЗАГЛАВНЫЕ буквы; ",
	"日本語のテキスト、カタカナ。",
	"Ünïcödé 123 456 ٤٥٦ 七八九\t\n",
	"emoji 😀 and symbols ∑ ≤ ∞ — “quoted” ",
	"العربية والأرقام ١٢٣ ",
}

//go:noinline
func classify(s string) (letters, upper, lower, digits, spaces, puncts int) {
	for _, r := range s {
		switch {
		case unicode.IsUpper(r):
			upper++
			letters++
		case unicode.IsLower(r):
			lower++
			letters++
		case unicode.IsLetter(r):
			letters++
		case unicode.IsDigit(r):
			digits++
		case unicode.IsSpace(r):
			spaces++
		case unicode.IsPunct(r) || unicode.IsSymbol(r):
			puncts++
		}
	}
	return
}

// swapCase re-encodes the string with every letter's case flipped, so the
// result can be a different length in bytes from the input.
//
//go:noinline
func swapCase(dst []byte, s string) []byte {
	dst = dst[:0]
	for _, r := range s {
		if unicode.IsUpper(r) {
			r = unicode.ToLower(r)
		} else if unicode.IsLower(r) {
			r = unicode.ToUpper(r)
		}
		dst = utf8.AppendRune(dst, r)
	}
	return dst
}

func main() {
	text := ""
	for i := 0; i < 24; i++ {
		text += samples[(i*5)%len(samples)]
	}
	buf := make([]byte, 0, 2*len(text))
	total := 0
	for r := 0; r < 2500; r++ {
		l, u, lo, d, sp, p := classify(text)
		total += l + u*3 + lo*5 + d*7 + sp*11 + p*13
		buf = swapCase(buf, text)
		total += len(buf) + utf8.RuneCount(buf)
		// Flip it back and fold: the round trip is not the identity for every
		// letter, which is part of what is being counted.
		buf = swapCase(buf, string(buf))
		for i := 0; i < len(buf); i += 7 {
			total += int(buf[i])
		}
		total += int(unicode.SimpleFold(rune('A' + r%26)))
	}
	println("total =", total)
}
