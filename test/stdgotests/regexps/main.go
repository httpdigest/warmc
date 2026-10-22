// regexp (RE2 syntax): MatchString, FindString/Index, FindAll with limits,
// submatches with named groups (SubexpNames, SubexpIndex), FindAllSubmatch
// over a log file, ReplaceAllString with $1/${name} expansion,
// ReplaceAllLiteralString, ReplaceAllStringFunc, Expand with a template,
// Split, Longest (leftmost-longest vs leftmost-first), case-insensitive and
// multi-line flags, Unicode classes, anchors and word boundaries, empty
// matches, QuoteMeta, LiteralPrefix, NumSubexp, and the parse errors for bad
// patterns.
//
// regexp compiles to a program that runs on one of three engines -- one-pass,
// backtracking with a bit-state visited set, or the Pike NFA -- chosen by the
// pattern and input size, so the patterns here are chosen to reach all three.
package main

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

func main() {
	// Simple matching.
	pats := []string{`^\d+$`, `^[a-z]+\d*$`, `(?i)^hello`, `colou?r`, `^$`, `a{2,3}`, `\bfoo\b`, `[[:alpha:]]+`, `\p{Greek}+`, `.`, `(?s)a.b`, `x*`}
	inputs := []string{"12345", "abc", "abc123", "Hello World", "color", "colour", "", "caaaat", "a foo b", "foobar", "αβγ", "a\nb", "\n"}
	for _, p := range pats {
		re := regexp.MustCompile(p)
		var row []string
		for _, in := range inputs {
			if re.MatchString(in) {
				row = append(row, "1")
			} else {
				row = append(row, ".")
			}
		}
		fmt.Printf("%-16s %s\n", p, strings.Join(row, ""))
	}

	// Find, FindIndex, FindAll with n.
	re := regexp.MustCompile(`[A-Z][a-z]+`)
	text := "Alice met Bob and Carol; then Dave, Eve and Frank left. ZZ"
	fmt.Printf("Find %q Index %v\n", re.FindString(text), re.FindStringIndex(text))
	for _, n := range []int{-1, 0, 1, 3, 100} {
		fmt.Printf("FindAll n=%d %q\n", n, re.FindAllString(text, n))
	}
	fmt.Println("FindAllIndex", re.FindAllStringIndex(text, -1))
	fmt.Printf("no match %q %v %v\n", re.FindString("lower only"), re.FindStringIndex("lower only"), re.FindAllString("lower", -1) == nil)

	// Submatches and named groups.
	date := regexp.MustCompile(`(?P<year>\d{4})-(?P<month>\d{2})-(?P<day>\d{2})(?:T(?P<hour>\d{2}):(\d{2}))?`)
	fmt.Println("NumSubexp", date.NumSubexp(), "names", fmt.Sprintf("%q", date.SubexpNames()), "SubexpIndex(day)", date.SubexpIndex("day"), date.SubexpIndex("nope"))
	for _, s := range []string{"on 2024-02-29 we", "2023-12-31T23:59 end", "1999-1-1", "dates 2001-01-01 and 2002-02-02T10:30"} {
		m := date.FindStringSubmatch(s)
		idx := date.FindStringSubmatchIndex(s)
		fmt.Printf("submatch %q -> %q %v\n", s, m, idx)
		all := date.FindAllStringSubmatch(s, -1)
		for _, a := range all {
			fmt.Printf("   all: year=%s month=%s day=%s hour=%q\n", a[1], a[2], a[3], a[4])
		}
	}

	// Log parsing.
	logText := `2024-01-01 10:00:01 INFO  [auth] user=alice action=login ok
2024-01-01 10:00:05 WARN  [db] slow query ms=1503
2024-01-01 10:01:00 ERROR [auth] user=bob action=login failed attempts=3
garbage line
2024-01-01 10:02:30 INFO  [api] user=carol action=fetch items=42
2024-01-01 10:03:00 ERROR [db] connection reset`
	lineRe := regexp.MustCompile(`(?m)^(\S+) (\S+) (\w+)\s+\[(\w+)\] (.*)$`)
	kvRe := regexp.MustCompile(`(\w+)=(\w+)`)
	for _, m := range lineRe.FindAllStringSubmatch(logText, -1) {
		kvs := kvRe.FindAllStringSubmatch(m[5], -1)
		var pairs []string
		for _, kv := range kvs {
			pairs = append(pairs, kv[1]+":"+kv[2])
		}
		fmt.Printf("log %s level=%-5s comp=%-4s kv=%v\n", m[2], m[3], m[4], pairs)
	}
	counts := map[string]int{}
	for _, m := range regexp.MustCompile(`(?m)^\S+ \S+ (\w+)`).FindAllStringSubmatch(logText, -1) {
		counts[m[1]]++
	}
	fmt.Println("level counts", counts)

	// Replace.
	fmt.Println(date.ReplaceAllString("from 2024-02-29 to 2025-03-01", "${day}/${month}/${year}"))
	fmt.Println(date.ReplaceAllString("2024-02-29", "$1x"))   // $1x is the group named "1x": empty
	fmt.Println(date.ReplaceAllString("2024-02-29", "${1}x")) // explicit
	fmt.Println(date.ReplaceAllLiteralString("2024-02-29 and 2023-01-01", "$DATE"))
	words := regexp.MustCompile(`\w+`)
	fmt.Println(words.ReplaceAllStringFunc("the quick brown fox", func(w string) string {
		return strings.ToUpper(w[:1]) + w[1:] + "(" + strconv.Itoa(len(w)) + ")"
	}))
	nums := regexp.MustCompile(`-?\d+`)
	fmt.Println(nums.ReplaceAllStringFunc("a1 b-22 c333 d4444", func(s string) string {
		n, _ := strconv.Atoi(s)
		return strconv.Itoa(n * n)
	}))
	fmt.Println(string(nums.ReplaceAllFunc([]byte("x=10,y=-3"), func(b []byte) []byte { return append([]byte("<"), append(b, '>')...) })))
	fmt.Println(regexp.MustCompile(`a*`).ReplaceAllString("baaac", "-"))
	fmt.Println(regexp.MustCompile(``).ReplaceAllString("abc", "|"))

	// Expand.
	tmpl := "$month.$day.$year (hour=${hour})\n"
	var out []byte
	for _, sm := range date.FindAllStringSubmatchIndex("2020-05-06 and 2021-07-08T09:10", -1) {
		out = date.ExpandString(out, tmpl, "2020-05-06 and 2021-07-08T09:10", sm)
	}
	fmt.Print(string(out))

	// Split.
	sp := regexp.MustCompile(`\s*[,;]\s*`)
	for _, n := range []int{-1, 0, 1, 2} {
		fmt.Printf("Split n=%d %q\n", n, sp.Split("a , b;c ;  d,e", n))
	}
	fmt.Printf("Split empty pattern %q\n", regexp.MustCompile(``).Split("abc", -1))
	fmt.Printf("Split x* %q\n", regexp.MustCompile(`x*`).Split("axxbxc", -1))

	// Leftmost-first vs leftmost-longest.
	alt := regexp.MustCompile(`a|ab|abc`)
	fmt.Println("leftmost-first", alt.FindString("abcd"))
	alt.Longest()
	fmt.Println("leftmost-longest", alt.FindString("abcd"))
	lazy := regexp.MustCompile(`<.+?>`)
	greedy := regexp.MustCompile(`<.+>`)
	fmt.Println("lazy", lazy.FindAllString("<a><b><c>", -1), "greedy", greedy.FindAllString("<a><b><c>", -1))
	posix := regexp.MustCompilePOSIX(`a+|a+b`)
	fmt.Println("POSIX", posix.FindString("aaab"))

	// Flags, anchors, Unicode.
	ml := regexp.MustCompile(`(?m)^(\w+):`)
	fmt.Printf("multiline %q\n", ml.FindAllStringSubmatch("key1: v\nkey2: w\n  notkey: x\nkey3:", -1))
	fmt.Printf("\\A vs ^ %q %q\n", regexp.MustCompile(`(?m)\Aa`).FindAllString("a\na", -1), regexp.MustCompile(`(?m)^a`).FindAllString("a\na", -1))
	fmt.Printf("$ vs \\z %q %q\n", regexp.MustCompile(`(?m)a$`).FindAllString("a\na", -1), regexp.MustCompile(`a\z`).FindAllString("a\na", -1))
	fmt.Printf("word boundaries %q\n", regexp.MustCompile(`\b\w`).FindAllString("hello big-world_x 9lives", -1))
	fmt.Printf("non-boundary %q\n", regexp.MustCompile(`\B\w\B`).FindAllString("abcd ef", -1))
	fmt.Printf("unicode classes %q\n", regexp.MustCompile(`\p{Lu}\p{Ll}*`).FindAllString("Ünïcode Ελληνικά Кириллица lower ÄÖÜ", -1))
	fmt.Printf("script %q\n", regexp.MustCompile(`\p{Han}+|\p{Hiragana}+`).FindAllString("漢字とひらがなとカタカナ", -1))
	fmt.Printf("case-insensitive unicode %v %v\n", regexp.MustCompile(`(?i)straße`).MatchString("STRAßE"), regexp.MustCompile(`(?i)σ`).MatchString("Σς"))
	fmt.Printf("dot and newline %q %q\n", regexp.MustCompile(`a.c`).FindAllString("abc a\nc", -1), regexp.MustCompile(`(?s)a.c`).FindAllString("abc a\nc", -1))
	fmt.Printf("repetition %q\n", regexp.MustCompile(`(ab){2,}`).FindAllString("ab abab ababab abababab", -1))
	fmt.Printf("empty matches %q\n", regexp.MustCompile(`b*`).FindAllString("abbcb", -1))
	fmt.Printf("byte classes %q\n", regexp.MustCompile(`[^\x00-\x7f]+`).FindAllString("ascii ünï 日本 ok", -1))

	// Big input to force the NFA (too long for bit-state backtracking).
	big := strings.Repeat("ab", 3000) + "needle-123" + strings.Repeat("cd", 3000) + "needle-456"
	nre := regexp.MustCompile(`(a|b)*?needle-(\d+)`)
	m := nre.FindAllStringSubmatchIndex(big, -1)
	for _, x := range m {
		fmt.Println("big match", x[0], x[1], big[x[4]:x[5]])
	}
	fmt.Println("big count", len(regexp.MustCompile(`ab`).FindAllStringIndex(big, -1)), len(regexp.MustCompile(`(?:cd)+`).FindAllString(big, -1)))

	// Metadata.
	fmt.Println("QuoteMeta", regexp.QuoteMeta(`1+1=2? [yes] (no) {x} ^$ \ | .*`))
	for _, p := range []string{`abc+`, `hello.*`, `^prefix`, `(?i)abc`, `xyz`} {
		lp, complete := regexp.MustCompile(p).LiteralPrefix()
		fmt.Printf("LiteralPrefix(%q) = %q %v String=%s\n", p, lp, complete, regexp.MustCompile(p).String())
	}
	for _, bad := range []string{`(`, `a)`, `[z-a]`, `a**`, `\8`, `(?P<n>a)(?P<n>b)`, `x{1001}`, `(?<name`, `\p{Bogus}`} {
		_, err := regexp.Compile(bad)
		fmt.Printf("Compile(%q): %v\n", bad, err)
	}
	ok, err := regexp.MatchString(`^[a-z]+\[[0-9]+\]$`, "adam[23]")
	fmt.Println("MatchString", ok, err)
	func() {
		defer func() { fmt.Println("MustCompile panic:", recover()) }()
		regexp.MustCompile(`*`)
	}()
}
