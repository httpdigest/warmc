// The bytes package and []byte values: bytes.Buffer as a growable queue
// (writes of every kind, Read/ReadByte/ReadRune/ReadString/Next with Unread,
// Truncate, Grow, Reset, and the wraparound copy when the read side has
// consumed most of the buffer), and the slice functions -- Equal, Compare,
// Index family, Split/Fields/Join, case mapping, trimming, Replace, Map,
// EqualFold, Cut, Clone and Runes.  The functions must never alias their
// input in a way that shows: results are mutated and the inputs printed
// again.  Also bytes.Reader with Seek and ReadAt.
package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"unicode"
)

var inputs = [][]byte{
	nil,
	{},
	[]byte("abc"),
	[]byte("  Hello,  Bytes World  "),
	[]byte("a-b--c---d"),
	[]byte("ÜBER straße"),
	{0, 1, 2, 0xff, 0xfe, '\n', 'x'},
	[]byte("banana bandana"),
}

func init() {
	if len(os.Args) > 1000 {
		inputs[0] = []byte("x")
	}
}

func main() {
	fmt.Println("-- comparisons")
	for _, a := range inputs {
		line := fmt.Sprintf("%q:", a)
		for _, b := range inputs {
			line += fmt.Sprintf(" %v/%d", bytes.Equal(a, b), bytes.Compare(a, b))
		}
		fmt.Println(line)
	}
	fmt.Println(bytes.Equal(nil, []byte{}), inputs[0] == nil, inputs[1] == nil)
	fmt.Println("-- searching and splitting")
	seps := [][]byte{[]byte("-"), []byte("an"), []byte(" "), nil}
	for _, in := range inputs {
		fmt.Printf("%q: idx=%d last=%d byte=%d any=%d rune=%d count=%d contains=%v\n", in,
			bytes.Index(in, []byte("an")), bytes.LastIndex(in, []byte("a")),
			bytes.IndexByte(in, 'a'), bytes.IndexAny(in, "zbß"),
			bytes.IndexRune(in, 'ß'), bytes.Count(in, []byte("a")),
			bytes.Contains(in, []byte("ll")))
		for _, sep := range seps {
			fmt.Printf("  %q %q %q\n", bytes.Split(in, sep), bytes.SplitN(in, sep, 2),
				bytes.SplitAfter(in, sep))
		}
		fmt.Printf("  fields %q\n", bytes.Fields(in))
		fmt.Printf("  fieldsfunc %q\n", bytes.FieldsFunc(in, func(r rune) bool { return r == 'a' || r == '-' }))
	}
	fmt.Println("-- transforms")
	for _, in := range inputs {
		orig := bytes.Clone(in)
		up := bytes.ToUpper(in)
		lo := bytes.ToLower(in)
		tr := bytes.TrimSpace(in)
		t2 := bytes.Trim(in, "ab ")
		rp := bytes.ReplaceAll(in, []byte("a"), []byte("<A>"))
		r1 := bytes.Replace(in, []byte("an"), []byte(""), 1)
		mp := bytes.Map(func(r rune) rune {
			if unicode.IsLetter(r) {
				return unicode.ToUpper(r) + 1
			}
			if r == ' ' {
				return -1
			}
			return r
		}, in)
		rep := bytes.Repeat(in, 2)
		// Mutate every result; the input must not change.
		for _, x := range [][]byte{up, lo, rp, r1, mp, rep} {
			for i := range x {
				x[i] = '#'
			}
		}
		fmt.Printf("%q %v %d %d %q %q\n", in, bytes.Equal(in, orig), len(up), len(rp), tr, t2)
		fmt.Printf("  %q %q\n", bytes.TrimLeft(in, " Hab"), bytes.TrimRight(in, " ax"))
		fmt.Printf("  %q %q %v %v\n", bytes.TrimPrefix(in, []byte("ba")),
			bytes.TrimSuffix(in, []byte("na")), bytes.HasPrefix(in, []byte("ba")),
			bytes.HasSuffix(in, []byte("na")))
		fmt.Printf("  %q\n", bytes.Runes(in))
		before, after, found := bytes.Cut(in, []byte(" "))
		fmt.Printf("  cut %q %q %v\n", before, after, found)
	}
	fmt.Println(bytes.EqualFold([]byte("Straße"), []byte("STRASSE")),
		bytes.EqualFold([]byte("Gopher"), []byte("GOPHER")),
		string(bytes.Title([]byte("hello world"))),
		string(bytes.Join([][]byte{[]byte("x"), nil, []byte("yz")}, []byte(", "))))
	fmt.Println("-- buffer as a queue")
	var buf bytes.Buffer
	total := 0
	for round := 0; round < 30; round++ {
		for i := 0; i <= round%7; i++ {
			buf.WriteString(fmt.Sprintf("r%d.%d;", round, i))
		}
		buf.WriteByte('|')
		buf.WriteRune('é')
		buf.Write([]byte{'0' + byte(round%10)})
		chunk := make([]byte, 5+round%4)
		n, err := buf.Read(chunk)
		total += n
		c, _ := buf.ReadByte()
		fmt.Printf("%d %d %v %q %q %d\n", round, n, err, chunk[:n], c, buf.Len())
		if round%3 == 0 {
			_ = buf.UnreadByte()
			r, size, _ := buf.ReadRune()
			fmt.Printf("  rune %q %d\n", r, size)
		}
		if round%5 == 4 {
			line, err := buf.ReadString(';')
			fmt.Printf("  readstring %q %v\n", line, err)
			fmt.Printf("  next %q\n", buf.Next(3))
		}
	}
	fmt.Println("total read", total, "left", buf.Len())
	rest := buf.String()
	fmt.Println(len(rest), rest[:20])
	buf.Truncate(10)
	fmt.Printf("%q\n", buf.String())
	buf.Reset()
	buf.Grow(100)
	buf.WriteString("after reset")
	fmt.Printf("%q %d\n", buf.Bytes(), buf.Len())
	var empty bytes.Buffer
	b := make([]byte, 4)
	n, err := empty.Read(b)
	fmt.Println(n, err == io.EOF, empty.String() == "")
	_, err = empty.ReadByte()
	fmt.Println(err)
	fmt.Println("-- reader")
	rd := bytes.NewReader([]byte("0123456789abcdef"))
	p := make([]byte, 3)
	for i := 0; i < 4; i++ {
		n, _ := rd.Read(p)
		fmt.Print(string(p[:n]), " ", rd.Len(), " ")
	}
	fmt.Println()
	pos, _ := rd.Seek(-4, io.SeekEnd)
	n, _ = rd.Read(p)
	fmt.Println(pos, string(p[:n]))
	pos, _ = rd.Seek(2, io.SeekStart)
	n, _ = rd.ReadAt(p, 10)
	fmt.Println(pos, string(p[:n]), rd.Size())
	pos, _ = rd.Seek(1, io.SeekCurrent)
	r, size, _ := rd.ReadRune()
	fmt.Println(pos, string(r), size)
}
