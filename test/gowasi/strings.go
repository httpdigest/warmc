// Text: UTF-8 iteration, the strings package, and number parsing both ways.
//
// Reaches what arithmetic does not -- unicode's range tables are a binary
// search over static data, strconv's float formatting is Ryū, and ranging over
// a string decodes it a rune at a time.  All of it is table lookups and
// branches on data the compiler cannot see through.
package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"
)

func main() {
	const s = "héllo, 世界! ǆǅǄ ÿĀ߿ࠀ￿\U00010000\U0010ffff"
	fmt.Println("len", len(s), "runes", utf8.RuneCountInString(s))

	var sizes, classes []int
	for i, r := range s {
		n := utf8.RuneLen(r)
		sizes = append(sizes, i*100+n)
		c := 0
		if unicode.IsLetter(r) {
			c |= 1
		}
		if unicode.IsUpper(r) {
			c |= 2
		}
		if unicode.IsSpace(r) {
			c |= 4
		}
		if unicode.IsDigit(r) {
			c |= 8
		}
		classes = append(classes, c)
	}
	fmt.Println("sizes", sizes)
	fmt.Println("classes", classes)
	fmt.Println("case", strings.ToUpper(s) == s, len(strings.ToLower(s)),
		len(strings.ToTitle("ǆ")))

	parts := strings.FieldsFunc(s, func(r rune) bool { return r == ' ' || r == ',' })
	sort.Strings(parts)
	fmt.Println("parts", len(parts), parts)
	fmt.Println("index", strings.Index(s, "世"), strings.LastIndex(s, "ÿ"),
		strings.Count(s, "l"), strings.HasPrefix(s, "hé"))
	fmt.Println("replace", strings.Replace(s, "l", "L", 2) == s,
		len(strings.Split(s, " ")), strings.TrimSpace("  x  "))

	ints := []string{"0", "-1", "2147483647", "-2147483648", "9223372036854775807",
		"-9223372036854775808", "0x1f", "0b1010", "0o17", "999999999999999999999", "x"}
	for _, t := range ints {
		v, err := strconv.ParseInt(t, 0, 64)
		fmt.Printf("int %q -> %d %v\n", t, v, err != nil)
	}
	floats := []string{"0", "-0", "1", "0.1", "1e308", "1e-308", "1e309",
		"3.141592653589793", "inf", "-inf", "nan", "0x1p-1", "z"}
	for _, t := range floats {
		v, err := strconv.ParseFloat(t, 64)
		fmt.Printf("flt %q -> %s %v\n", t, strconv.FormatFloat(v, 'g', -1, 64), err != nil)
	}
	for _, f := range []float64{0, 1, 0.1, 1.0 / 3.0, 1e21, 1e-7, 255.5} {
		fmt.Println("fmt", strconv.FormatFloat(f, 'e', 6, 64),
			strconv.FormatFloat(f, 'f', 3, 64), strconv.FormatFloat(f, 'g', -1, 32))
	}
}
