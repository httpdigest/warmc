// The strings and bytes packages: searching (Index, LastIndex, IndexAny,
// IndexFunc, IndexByte, Contains*, Count, HasPrefix/Suffix), splitting
// (Split, SplitN, SplitAfter, Fields, FieldsFunc, Cut, CutPrefix, CutSuffix,
// and the Lines/SplitSeq/FieldsSeq iterators), transforming (Replace,
// ReplaceAll, Replacer with overlapping patterns, Map, Repeat, Title-free
// case mapping, ToValidUTF8, Trim family), comparison (Compare, EqualFold),
// Builder growth, strings.Reader seeking and ReadAt, bytes.Buffer as a reader
// and writer with Next/ReadBytes/UnreadRune/Truncate/Grow, and the bytes
// versions of the same functions over byte slices that alias each other.
//
// Index and friends are the vectorised core of these packages on other
// architectures; on wasm they are portable Go loops (Rabin-Karp for long
// needles, a byte loop for short ones), with the switch between strategies
// at fixed lengths -- so needles of many lengths are used.
package main

import (
	"bytes"
	"fmt"
	"io"
	"strings"
	"unicode"
)

func main() {
	text := "the quick brown fox jumps over the lazy dog; THE END. the-end"
	needles := []string{"", "t", "the", "The", "fox", "dog;", "z", "end", "o", "the quick brown fox jumps over", "xyz", "the-end", "e"}
	for _, n := range needles {
		fmt.Printf("%-34q Index=%3d Last=%3d Count=%2d Contains=%-5v Fold=%v\n", n,
			strings.Index(text, n), strings.LastIndex(text, n), strings.Count(text, n), strings.Contains(text, n),
			strings.Contains(strings.ToLower(text), strings.ToLower(n)))
	}
	// Long haystacks to reach Rabin-Karp.
	hay := strings.Repeat("abcabcabd", 200) + "NEEDLEinHaystack" + strings.Repeat("xyz", 100)
	for _, n := range []string{"NEEDLE", "NEEDLEinHaystack", "abd" + "NEEDLE", "abcabd", strings.Repeat("abcabcabd", 20) + "N", "zzz", "xyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyzxyz"} {
		fmt.Printf("long Index(len %3d) = %5d Last=%5d Count=%d\n", len(n), strings.Index(hay, n), strings.LastIndex(hay, n), strings.Count(hay, n))
	}
	fmt.Println("IndexByte", strings.IndexByte(text, 'q'), strings.IndexByte(text, '!'), "IndexRune", strings.IndexRune("chicken ☺ smile", '☺'), strings.IndexRune("abc", -1))
	fmt.Println("IndexAny", strings.IndexAny(text, "zyx"), strings.LastIndexAny(text, "zyx"), strings.IndexAny("", "a"), strings.ContainsAny("hi", ""))
	fmt.Println("IndexFunc", strings.IndexFunc(text, unicode.IsUpper), strings.LastIndexFunc(text, unicode.IsPunct), strings.ContainsFunc(text, unicode.IsDigit))
	fmt.Println("Prefix/Suffix", strings.HasPrefix(text, "the q"), strings.HasSuffix(text, "end"), strings.HasPrefix("", ""), strings.HasSuffix("a", "ab"))

	// Splitting.
	csv := "a,b,,c,"
	fmt.Printf("Split %q\n", strings.Split(csv, ","))
	fmt.Printf("SplitN2 %q SplitN0 %q SplitN-1 %q\n", strings.SplitN(csv, ",", 2), strings.SplitN(csv, ",", 0), strings.SplitN(csv, ",", -1))
	fmt.Printf("SplitAfter %q\n", strings.SplitAfter(csv, ","))
	fmt.Printf("Split empty sep %q\n", strings.Split("héllo", ""))
	fmt.Printf("Split empty string %q %d\n", strings.Split("", ","), len(strings.Split("", ",")))
	fmt.Printf("Fields %q\n", strings.Fields("  lots   of\t\nspace \u0085here\u00a0nbsp  "))
	fmt.Printf("FieldsFunc %q\n", strings.FieldsFunc("a1b22c333d;e", func(r rune) bool { return unicode.IsDigit(r) || r == ';' }))
	for _, s := range []string{"key=value", "key=", "=value", "novalue", "a=b=c"} {
		before, after, found := strings.Cut(s, "=")
		fmt.Printf("Cut(%q) = %q %q %v\n", s, before, after, found)
	}
	r1, ok1 := strings.CutPrefix("prefix-body", "prefix-")
	r2, ok2 := strings.CutSuffix("body.go", ".rs")
	fmt.Println("CutPrefix", r1, ok1, "CutSuffix", r2, ok2)
	var lines []string
	for l := range strings.Lines("one\ntwo\r\nthree\n\nlast") {
		lines = append(lines, fmt.Sprintf("%q", l))
	}
	fmt.Println("Lines", strings.Join(lines, " "))
	var parts []string
	for p := range strings.SplitSeq("x|y||z", "|") {
		parts = append(parts, p)
	}
	for p := range strings.FieldsSeq(" f1  f2\tf3 ") {
		parts = append(parts, strings.ToUpper(p))
	}
	fmt.Printf("SplitSeq+FieldsSeq %q\n", parts)

	// Transforming.
	fmt.Println(strings.Replace("oink oink oink", "k", "ky", 2), "|", strings.Replace("oink oink", "oink", "moo", -1), "|", strings.ReplaceAll("aaaa", "aa", "b"), "|", strings.Replace("abc", "", "-", -1))
	rep := strings.NewReplacer("a", "1", "aa", "2", "aaa", "3", "<", "&lt;", ">", "&gt;", "", "")
	fmt.Println("Replacer", rep.Replace("aaaa <b> aa a"))
	rep2 := strings.NewReplacer("cat", "dog", "dog", "cat", "c", "C")
	fmt.Println("Replacer swap", rep2.Replace("cat dog cow catdog"))
	rep3 := strings.NewReplacer("a", "b")
	var rb strings.Builder
	n, _ := rep3.WriteString(&rb, "banana")
	fmt.Println("Replacer single byte", rb.String(), n)
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
	fmt.Println("Map rot13", strings.Map(rot13, "Hello, World! xyz"))
	fmt.Println("Repeat", strings.Repeat("ab", 5), strings.Repeat("", 100) == "", len(strings.Repeat("x", 1000)))
	func() {
		defer func() { fmt.Println("Repeat negative:", recover()) }()
		strings.Repeat("x", -1)
	}()
	for _, s := range []string{"hello world", "HELLO", "ǆemo", "straße", "İstanbul", "ΣΑΣ", "ﬁne"} {
		fmt.Printf("case %q upper=%q lower=%q title=%q\n", s, strings.ToUpper(s), strings.ToLower(s), strings.ToTitle(s))
	}
	fmt.Printf("ToValidUTF8 %q\n", strings.ToValidUTF8("a\xffb\xc0\xafc\xed\xa0\x80d", "?"))
	fmt.Printf("Trim %q TrimLeft %q TrimRight %q TrimSpace %q TrimPrefix %q TrimSuffix %q TrimFunc %q\n",
		strings.Trim("xxhixyx", "xy"), strings.TrimLeft("xxhixx", "x"), strings.TrimRight("xxhixx", "x"),
		strings.TrimSpace("\t\n hi there \r\n"), strings.TrimPrefix("prefix", "pre"), strings.TrimSuffix("suffix", "fix"),
		strings.TrimFunc("123abc456", unicode.IsDigit))

	// Comparison.
	words := []string{"", "a", "A", "ab", "b", "abc", "Abc", "ß", "SS", "ǅ", "ǆ", "K", "\u212a"}
	for _, a := range words {
		var row []string
		for _, b := range words {
			row = append(row, fmt.Sprintf("%2d", strings.Compare(a, b)))
		}
		fmt.Printf("Compare %-4q %s\n", a, strings.Join(row, ""))
	}
	for _, p := range [][2]string{{"Go", "GO"}, {"ǅ", "ǆ"}, {"K", "\u212a"}, {"ß", "SS"}, {"σ", "ς"}, {"abc", "abd"}, {"", ""}} {
		fmt.Printf("EqualFold(%q,%q)=%v\n", p[0], p[1], strings.EqualFold(p[0], p[1]))
	}

	// Builder.
	var sb strings.Builder
	fmt.Println("builder initial", sb.Len(), sb.Cap())
	sb.Grow(10)
	fmt.Println("after Grow(10) cap>=10:", sb.Cap() >= 10)
	for i := 0; i < 50; i++ {
		sb.WriteString(fmt.Sprint(i))
		sb.WriteByte(',')
		sb.WriteRune('→')
	}
	fmt.Println("builder len", sb.Len(), "prefix", sb.String()[:30])
	sb.Reset()
	fmt.Println("after reset", sb.Len(), sb.String() == "")

	// strings.Reader.
	sr := strings.NewReader("0123456789abcdef")
	b4 := make([]byte, 4)
	sr.Read(b4)
	fmt.Println("read", string(b4), "len", sr.Len(), "size", sr.Size())
	pos, _ := sr.Seek(-3, io.SeekEnd)
	sr.Read(b4[:2])
	fmt.Println("seek end-3", pos, string(b4[:2]))
	sr.Seek(5, io.SeekStart)
	c, _ := sr.ReadByte()
	sr.UnreadByte()
	c2, _ := sr.ReadByte()
	fmt.Println("readbyte/unread", string(c), string(c2))
	nat, err := sr.ReadAt(b4, 14)
	fmt.Println("ReadAt 14", nat, string(b4[:nat]), err)
	_, err = sr.Seek(-100, io.SeekCurrent)
	fmt.Println("negative seek:", err)
	ru := strings.NewReader("é☺x")
	for {
		r, size, err := ru.ReadRune()
		if err != nil {
			fmt.Println("readrune end", err)
			break
		}
		fmt.Println("rune", string(r), size)
	}

	// bytes.Buffer.
	var buf bytes.Buffer
	buf.WriteString("hello ")
	buf.Write([]byte("buffer "))
	buf.WriteByte('!')
	buf.WriteRune('☺')
	fmt.Fprintf(&buf, " %d\nline2\nline3", 42)
	fmt.Printf("buffer %q len=%d\n", buf.String(), buf.Len())
	fmt.Printf("Next(6) %q\n", buf.Next(6))
	line, err := buf.ReadBytes('\n')
	fmt.Printf("ReadBytes %q %v\n", line, err)
	s, err := buf.ReadString('\n')
	fmt.Printf("ReadString %q %v\n", s, err)
	r, sz, _ := buf.ReadRune()
	fmt.Println("ReadRune", string(r), sz, "unread", buf.UnreadRune())
	rest, err := buf.ReadString('\n')
	fmt.Printf("rest %q %v\n", rest, err)
	buf.Reset()
	buf.WriteString("truncate me please")
	buf.Truncate(11)
	fmt.Printf("Truncate %q\n", buf.String())
	buf.Grow(1000)
	fmt.Println("after Grow, cap >= 1011:", buf.Cap() >= 1011, "len", buf.Len())
	var dst bytes.Buffer
	nn, _ := io.Copy(&dst, &buf)
	fmt.Println("copied", nn, dst.String(), "src empty", buf.Len())
	ab := bytes.NewBufferString("available")
	fmt.Println("Available>=0", ab.Available() >= 0, "AvailableBuffer len", len(ab.AvailableBuffer()))

	// bytes functions and aliasing.
	data := []byte("  Mixed Case, with: Punctuation; and   spaces  ")
	fmt.Printf("bytes.Fields %q\n", bytes.Fields(data))
	fmt.Printf("bytes.ToUpper %q\n", bytes.ToUpper(data))
	fmt.Printf("bytes.TrimSpace %q\n", bytes.TrimSpace(data))
	fmt.Printf("bytes.Split %q\n", bytes.Split([]byte("a-b-c"), []byte("-")))
	fmt.Println("bytes.Equal", bytes.Equal([]byte("a"), []byte("a")), bytes.Equal(nil, []byte{}), bytes.Compare([]byte("a"), []byte("b")))
	fmt.Println("bytes.Index", bytes.Index(data, []byte("with")), bytes.LastIndexByte(data, 'n'), bytes.Count(data, []byte(" ")))
	fmt.Printf("bytes.Title-free map %q\n", bytes.Map(func(r rune) rune {
		if unicode.IsSpace(r) {
			return '_'
		}
		if unicode.IsUpper(r) {
			return unicode.ToLower(r)
		}
		return unicode.ToUpper(r)
	}, data))
	whole := []byte("0123456789")
	a := whole[2:5]
	a = append(a, 'X') // overwrites whole[5]
	fmt.Println("append aliases:", string(whole), string(a))
	b := bytes.Clone(whole[:3])
	b = append(b, 'Y')
	fmt.Println("clone does not alias:", string(whole), string(b))
	fmt.Printf("ReplaceAll %q Repeat %q Join %q\n", bytes.ReplaceAll([]byte("aXbXc"), []byte("X"), []byte("--")),
		bytes.Repeat([]byte("ab"), 3), bytes.Join([][]byte{[]byte("x"), []byte("y"), []byte("z")}, []byte(", ")))
	bb, ba, bf := bytes.Cut([]byte("k:v"), []byte(":"))
	fmt.Printf("bytes.Cut %q %q %v ContainsRune %v HasPrefix %v\n", bb, ba, bf, bytes.ContainsRune(data, ';'), bytes.HasPrefix(data, []byte("  M")))
	fmt.Printf("bytes.Runes %q ToValidUTF8 %q EqualFold %v\n", bytes.Runes([]byte("a☺")), bytes.ToValidUTF8([]byte("a\xffb"), []byte("<>")), bytes.EqualFold([]byte("Straße"), []byte("STRASSE")))
	var bl []string
	for l := range bytes.Lines([]byte("l1\nl2\n")) {
		bl = append(bl, string(l))
	}
	fmt.Printf("bytes.Lines %q\n", bl)

	// strings.Join and big concatenations.
	var many []string
	for i := 0; i < 300; i++ {
		many = append(many, strings.Repeat(string(rune('a'+i%26)), i%5+1))
	}
	joined := strings.Join(many, "/")
	h := 0
	for i := 0; i < len(joined); i++ {
		h = h*31 + int(joined[i])
		h &= 0xffffffff
	}
	fmt.Println("joined length", len(joined), "hash", h, "count /", strings.Count(joined, "/"))
	fmt.Println("Clone equal:", strings.Clone(joined) == joined)
}
