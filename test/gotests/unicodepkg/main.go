// The unicode package.  Every predicate (IsLetter, IsDigit, IsNumber,
// IsSpace, IsUpper, IsLower, IsTitle, IsPunct, IsSymbol, IsMark, IsControl,
// IsGraphic, IsPrint) and the case mappings ToUpper/ToLower/ToTitle and
// SimpleFold orbits, for runes from many scripts, special cases (Kelvin
// sign, dotted and dotless i, final sigma, title-case digraphs, U+00DF), and
// script and category membership through unicode.Is/In with range tables.
// Then the predicates are counted over every rune below U+3100 and over a
// sampled stretch of the supplementary planes: binary searches through the
// range tables, with the counts printed per block so that one wrong
// comparison moves a number.
package main

import (
	"fmt"
	"os"
	"strings"
	"unicode"
)

var limit rune = 0x3100

func init() {
	if len(os.Args) > 1000 {
		limit = 0x100
	}
}

func flags(r rune) string {
	b := []byte("-------------")
	set := func(i int, ok bool, c byte) {
		if ok {
			b[i] = c
		}
	}
	set(0, unicode.IsLetter(r), 'L')
	set(1, unicode.IsDigit(r), 'D')
	set(2, unicode.IsNumber(r), 'N')
	set(3, unicode.IsSpace(r), 'S')
	set(4, unicode.IsUpper(r), 'U')
	set(5, unicode.IsLower(r), 'l')
	set(6, unicode.IsTitle(r), 'T')
	set(7, unicode.IsPunct(r), 'P')
	set(8, unicode.IsSymbol(r), 'Y')
	set(9, unicode.IsMark(r), 'M')
	set(10, unicode.IsControl(r), 'C')
	set(11, unicode.IsGraphic(r), 'G')
	set(12, unicode.IsPrint(r), 'p')
	return string(b)
}

func main() {
	samples := []rune{
		0, '\t', '\n', ' ', '!', '0', '9', 'A', 'Z', 'a', 'z', '_', '~', 0x7f,
		0x85, 0xa0, 0xaa, 0xb2, 0xb5, 0xbd, 0xc0, 0xdf, 0xe9, 0xff, 0x130, 0x131,
		0x149, 0x17f, 0x1c4, 0x1c5, 0x1c6, 0x1f0, 0x300, 0x345, 0x391, 0x3a3,
		0x3c2, 0x3c3, 0x3d0, 0x401, 0x451, 0x5d0, 0x627, 0x661, 0x903, 0x966,
		0x1e9e, 0x1f88, 0x2028, 0x2029, 0x2126, 0x212a, 0x212b, 0x2160, 0x2170,
		0x2460, 0x3000, 0x3042, 0x30a2, 0x4e00, 0xac00, 0xfb01, 0xfeff, 0xff21,
		0xff41, 0x10400, 0x10428, 0x1d400, 0x1f600, 0x1f1e6, 0xe0001, 0x10ffff,
	}
	fmt.Println("-- predicates  L D N S U l T P Y M C G p")
	for _, r := range samples {
		fmt.Printf("%U %s up=%U lo=%U ti=%U\n", r, flags(r), unicode.ToUpper(r),
			unicode.ToLower(r), unicode.ToTitle(r))
	}
	fmt.Println("-- fold orbits")
	for _, r := range []rune{'A', 'k', 's', 0xb5, 0xdf, 0x130, 0x131, 0x1c4, 0x3a3,
		0x3b8, 0x1e9e, 0x212b, '1'} {
		orbit := []string{fmt.Sprintf("%U", r)}
		for f := unicode.SimpleFold(r); f != r; f = unicode.SimpleFold(f) {
			orbit = append(orbit, fmt.Sprintf("%U", f))
		}
		fmt.Println(strings.Join(orbit, " -> "))
	}
	fmt.Println("-- special cases")
	tr := unicode.TurkishCase
	for _, r := range []rune{'i', 'I', 0x130, 0x131} {
		fmt.Printf("%U turkish up=%U lo=%U title=%U\n", r, tr.ToUpper(r), tr.ToLower(r), tr.ToTitle(r))
	}
	words := []string{"straße", "ǅemal", "ΟΔΥΣΣΕΥΣ", "İstanbul", "ﬁne", "Ｆｕｌｌ", "𐐀𐐨"}
	for _, w := range words {
		fmt.Printf("%q up=%q lo=%q title=%q fold=%v\n", w, strings.ToUpper(w), strings.ToLower(w),
			strings.ToTitle(w), strings.EqualFold(w, strings.ToUpper(w)))
	}
	fmt.Println("-- scripts and categories")
	tables := []struct {
		name string
		t    *unicode.RangeTable
	}{
		{"Latin", unicode.Latin}, {"Greek", unicode.Greek}, {"Cyrillic", unicode.Cyrillic},
		{"Han", unicode.Han}, {"Hiragana", unicode.Hiragana}, {"Arabic", unicode.Arabic},
		{"Hebrew", unicode.Hebrew}, {"Devanagari", unicode.Devanagari}, {"Hangul", unicode.Hangul},
		{"Lu", unicode.Lu}, {"Ll", unicode.Ll}, {"Nd", unicode.Nd}, {"Sm", unicode.Sm},
		{"Sc", unicode.Sc}, {"Zs", unicode.Zs}, {"Mn", unicode.Mn}, {"Cf", unicode.Cf},
		{"White_Space", unicode.White_Space}, {"Dash", unicode.Dash},
	}
	for _, r := range samples[:40] {
		var in []string
		for _, t := range tables {
			if unicode.Is(t.t, r) {
				in = append(in, t.name)
			}
		}
		fmt.Printf("%U %v %v\n", r, in, unicode.In(r, unicode.Greek, unicode.Cyrillic, unicode.Nd))
	}
	fmt.Println("-- counts per block")
	type counter [13]int
	var total counter
	for blk := rune(0); blk < limit; blk += 0x100 {
		var c counter
		for r := blk; r < blk+0x100; r++ {
			f := flags(r)
			for i := range f {
				if f[i] != '-' {
					c[i]++
				}
			}
		}
		for i := range c {
			total[i] += c[i]
		}
		if blk%0x400 == 0 || blk >= 0x3000 {
			fmt.Printf("%04X %v\n", blk, c)
		}
	}
	fmt.Println("total", total)
	var supp counter
	for r := rune(0x10000); r < 0x30000; r += 37 {
		f := flags(r)
		for i := range f {
			if f[i] != '-' {
				supp[i]++
			}
		}
	}
	fmt.Println("supplementary", supp)
	scripts := map[string]int{}
	for r := rune(0); r < limit; r++ {
		for _, t := range tables[:9] {
			if unicode.Is(t.t, r) {
				scripts[t.name]++
			}
		}
	}
	for _, t := range tables[:9] {
		fmt.Print(t.name, "=", scripts[t.name], " ")
	}
	fmt.Println()
	caseChanges := 0
	roundTrips := 0
	for r := rune(0); r < limit; r++ {
		u, l := unicode.ToUpper(r), unicode.ToLower(r)
		if u != r || l != r {
			caseChanges++
		}
		if unicode.ToLower(u) == l && unicode.ToUpper(l) == u {
			roundTrips++
		}
	}
	fmt.Println(caseChanges, roundTrips, unicode.MaxRune, unicode.ReplacementChar, string(unicode.MaxASCII))
}
