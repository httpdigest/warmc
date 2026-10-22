// The strings package and string values.  Searching (Index and friends,
// which reach the byte-search helpers and the Rabin-Karp fallback),
// splitting and joining, case mapping, trimming, replacing (including a
// Replacer with overlapping patterns), Builder growth, Repeat, Compare and
// the comparison operators, Fields/FieldsFunc, Cut/CutPrefix/CutSuffix,
// EqualFold, Map, and the iterator forms (SplitSeq, FieldsSeq, Lines).
// Strings are immutable values sharing backing bytes, so substrings,
// concatenation in a loop, and conversions to and from []byte are checked
// for independence.
package main

import (
	"fmt"
	"os"
	"strings"
	"unicode"
)

var texts = []string{
	"",
	"a",
	"hello, world",
	"  leading and trailing  ",
	"the quick brown fox jumps over the lazy dog",
	"a,b,,c,",
	"mississippi",
	"ÄÖÜ äöü ß Straße",
	"日本語のテキスト",
	"tab\tseparated\tvalues\n",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",
}

func init() {
	if len(os.Args) > 1000 {
		texts[0] = "x"
	}
}

func main() {
	needles := []string{"", "a", "ss", "issi", "o", "the", "ü", "語", "\t", "aab", "zz"}
	fmt.Println("-- searching")
	for _, s := range texts {
		fmt.Printf("%q len=%d\n", s, len(s))
		for _, n := range needles {
			fmt.Printf("  %q: %v %d %d %d %v %v\n", n, strings.Contains(s, n),
				strings.Index(s, n), strings.LastIndex(s, n), strings.Count(s, n),
				strings.HasPrefix(s, n), strings.HasSuffix(s, n))
		}
		fmt.Println("  any:", strings.IndexAny(s, "xyzo"), strings.LastIndexAny(s, "aeiou"),
			strings.ContainsAny(s, "ß日"), strings.IndexByte(s, 's'),
			strings.IndexRune(s, 'ö'), strings.ContainsRune(s, '本'),
			strings.IndexFunc(s, unicode.IsUpper), strings.LastIndexFunc(s, unicode.IsSpace))
	}
	fmt.Println("-- splitting")
	for _, s := range texts {
		fmt.Printf("%q\n", strings.Split(s, ","))
		fmt.Printf("%q\n", strings.SplitN(s, "s", 3))
		fmt.Printf("%q\n", strings.SplitAfter(s, "i"))
		fmt.Printf("%q\n", strings.SplitAfterN(s, " ", 2))
		fmt.Printf("%q\n", strings.Fields(s))
		fmt.Printf("%q\n", strings.FieldsFunc(s, func(r rune) bool {
			return r == ',' || r == ' ' || r == 'i'
		}))
		fmt.Printf("%q\n", strings.Split(s, ""))
	}
	for part := range strings.SplitSeq("x:y::z", ":") {
		fmt.Printf("seq %q\n", part)
	}
	for f := range strings.FieldsSeq("  one two\tthree\n") {
		fmt.Printf("field %q\n", f)
	}
	for l := range strings.Lines("line one\nline two\n\nlast") {
		fmt.Printf("line %q\n", l)
	}
	fmt.Println("-- joining and repeating")
	fmt.Println(strings.Join(nil, ","), strings.Join([]string{"a"}, ","),
		strings.Join([]string{"a", "b", "c"}, ", "), strings.Join(texts[1:4], "|"))
	for i := 0; i < 5; i++ {
		fmt.Printf("%q\n", strings.Repeat("ab", i)+strings.Repeat("-", 2*i))
	}
	fmt.Println("-- case and trimming")
	for _, s := range texts {
		fmt.Printf("%q %q %q\n", strings.ToUpper(s), strings.ToLower(s), strings.ToTitle(s))
		fmt.Printf("  %q %q %q %q\n", strings.TrimSpace(s), strings.Trim(s, " a"),
			strings.TrimLeft(s, "ath "), strings.TrimRight(s, "\n\tsg"))
		fmt.Printf("  %q %q %q\n", strings.TrimPrefix(s, "the "),
			strings.TrimSuffix(s, "dog"), strings.TrimFunc(s, unicode.IsLetter))
	}
	fmt.Println("-- replacing")
	for _, s := range texts {
		fmt.Printf("%q %q %q %q\n", strings.ReplaceAll(s, "ss", "SS"),
			strings.Replace(s, "a", "AA", 2), strings.Replace(s, "", "-", 3),
			strings.ReplaceAll(s, "i", ""))
	}
	r := strings.NewReplacer("a", "1", "aa", "2", "aaa", "3", "<", "&lt;", ">", "&gt;", "ss", "ß")
	for _, s := range []string{"aaaa", "a<b>c", "mississippi", "aaaaaaa", "", "<<>>"} {
		fmt.Printf("%q -> %q\n", s, r.Replace(s))
	}
	fmt.Println("-- cut, fold, compare, map")
	for _, s := range []string{"key=value", "noequals", "=x", "a=b=c"} {
		before, after, found := strings.Cut(s, "=")
		rest, ok := strings.CutPrefix(s, "key")
		head, ok2 := strings.CutSuffix(s, "c")
		fmt.Printf("%q %q %v | %q %v | %q %v\n", before, after, found, rest, ok, head, ok2)
	}
	folds := [][2]string{{"Go", "GO"}, {"straße", "STRASSE"}, {"ß", "ẞ"}, {"k", "\u212a"},
		{"σ", "Σ"}, {"ς", "Σ"}, {"abc", "abd"}}
	for _, p := range folds {
		fmt.Println(p[0], p[1], strings.EqualFold(p[0], p[1]), strings.Compare(p[0], p[1]),
			p[0] < p[1], p[0] >= p[1])
	}
	rot13 := func(r rune) rune {
		switch {
		case r >= 'a' && r <= 'z':
			return 'a' + (r-'a'+13)%26
		case r >= 'A' && r <= 'Z':
			return 'A' + (r-'A'+13)%26
		case r == ' ':
			return -1
		}
		return r
	}
	for _, s := range texts {
		fmt.Printf("%q\n", strings.Map(rot13, s))
	}
	fmt.Println("-- builder")
	var b strings.Builder
	for i := 0; i < 40; i++ {
		b.WriteString(fmt.Sprint(i))
		b.WriteByte(' ')
		b.WriteRune(rune('α' + i%24))
		if i%8 == 7 {
			fmt.Println(b.Len(), b.String())
		}
	}
	b.Reset()
	b.Grow(64)
	b.WriteString(fmt.Sprintf("%d-%s-%v", 7, "x", []int{1, 2}))
	fmt.Println(b.String(), b.Len())
	fmt.Println("-- immutability and sharing")
	base := strings.Repeat("0123456789", 5)
	subs := []string{}
	for i := 0; i+3 <= len(base); i += 7 {
		subs = append(subs, base[i:i+3])
	}
	bs := []byte(base)
	for i := range bs {
		bs[i] = 'x'
	}
	fmt.Println(subs, base[45:], string(bs[:5]))
	acc := ""
	for i := 0; i < 30; i++ {
		acc += string(rune('a' + i%26))
		if i%5 == 0 {
			acc = acc[1:] + acc[:1]
		}
	}
	fmt.Println(acc, len(acc))
	cmpList := []string{"b", "a", "ab", "", "B", "ä", "aa", "a\x00", "z"}
	for _, x := range cmpList {
		line := x + ":"
		for _, y := range cmpList {
			line += fmt.Sprint(" ", strings.Compare(x, y))
		}
		fmt.Println(line)
	}
}
