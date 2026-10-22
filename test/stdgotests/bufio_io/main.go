// bufio and io composition: bufio.Scanner over a strings.Reader with
// ScanLines (including CRLF and a missing final newline), ScanWords,
// ScanRunes (invalid UTF-8), ScanBytes, a custom SplitFunc for comma-separated
// tokens and one that returns an error, Buffer limits and ErrTooLong;
// bufio.Reader with tiny buffers (Peek, ReadSlice, ReadLine with isPrefix,
// ReadString, ReadRune/UnreadRune, Discard, Buffered, WriteTo); bufio.Writer
// (Available, Buffered, Flush, ReadFrom) and ReadWriter; and io's plumbing:
// io.Pipe between goroutines, io.Copy/CopyN/CopyBuffer, io.MultiWriter,
// io.MultiReader, io.TeeReader, io.LimitReader, io.SectionReader,
// io.ReadAtLeast/ReadFull errors, io.ReadAll, io.NopCloser, io.OffsetWriter
// and io.WriteString, with a writer that only takes a few bytes at a time.
//
// Every read returns at a boundary that depends on buffer sizes, so tiny
// buffers and short-write writers are used to put boundaries everywhere.
package main

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"strings"
	"unicode/utf8"
)

// slowWriter accepts at most n bytes per Write and reports short writes
// honestly via a loop in Write itself.
type slowWriter struct {
	buf   bytes.Buffer
	n     int
	calls int
}

func (w *slowWriter) Write(p []byte) (int, error) {
	w.calls++
	total := 0
	for len(p) > 0 {
		k := min(w.n, len(p))
		w.buf.Write(p[:k])
		p = p[k:]
		total += k
	}
	return total, nil
}

// oneByteReader returns one byte per Read.
type oneByteReader struct{ r io.Reader }

func (o oneByteReader) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	return o.r.Read(p[:1])
}

type upperWriter struct{ w io.Writer }

func (u upperWriter) Write(p []byte) (int, error) { return u.w.Write(bytes.ToUpper(p)) }

func main() {
	// Scanner: lines.
	text := "first line\r\nsecond line\n\nfourth after empty\nno newline at end"
	sc := bufio.NewScanner(strings.NewReader(text))
	for n := 1; sc.Scan(); n++ {
		fmt.Printf("line %d: %q\n", n, sc.Text())
	}
	fmt.Println("lines err:", sc.Err())

	// Words over a one-byte reader.
	sc = bufio.NewScanner(oneByteReader{strings.NewReader("  the\tquick  brown\n\nfox  jumps over ")})
	sc.Split(bufio.ScanWords)
	var words []string
	for sc.Scan() {
		words = append(words, sc.Text())
	}
	fmt.Printf("words %q err=%v\n", words, sc.Err())

	// Runes, including invalid bytes.
	sc = bufio.NewScanner(strings.NewReader("aé\xff☺\xe2\x82z"))
	sc.Split(bufio.ScanRunes)
	var runes []string
	for sc.Scan() {
		r, _ := utf8.DecodeRune(sc.Bytes())
		runes = append(runes, fmt.Sprintf("%q/%U", sc.Text(), r))
	}
	fmt.Println("runes", runes)
	sc = bufio.NewScanner(strings.NewReader("xyz"))
	sc.Split(bufio.ScanBytes)
	var bs []byte
	for sc.Scan() {
		bs = append(bs, sc.Bytes()[0]+1)
	}
	fmt.Printf("bytes+1 %q\n", bs)

	// Custom split: comma-separated, trims spaces, final token without comma.
	commaSplit := func(data []byte, atEOF bool) (int, []byte, error) {
		if i := bytes.IndexByte(data, ','); i >= 0 {
			return i + 1, bytes.TrimSpace(data[:i]), nil
		}
		if atEOF && len(data) > 0 {
			return len(data), bytes.TrimSpace(data), bufio.ErrFinalToken
		}
		return 0, nil, nil
	}
	sc = bufio.NewScanner(oneByteReader{strings.NewReader("alpha, beta,gamma ,, delta ,last one")})
	sc.Split(commaSplit)
	var toks []string
	for sc.Scan() {
		toks = append(toks, sc.Text())
	}
	fmt.Printf("comma tokens %q err=%v\n", toks, sc.Err())

	// Split func that errors.
	errSplit := func(data []byte, atEOF bool) (int, []byte, error) {
		adv, tok, err := bufio.ScanWords(data, atEOF)
		if err == nil && tok != nil && string(tok) == "STOP" {
			return 0, nil, errors.New("stop word found")
		}
		return adv, tok, err
	}
	sc = bufio.NewScanner(strings.NewReader("go go STOP never"))
	sc.Split(errSplit)
	var before []string
	for sc.Scan() {
		before = append(before, sc.Text())
	}
	fmt.Println("error split", before, "err:", sc.Err())

	// Buffer limit.
	long := strings.Repeat("x", 100) + "\nshort\n"
	sc = bufio.NewScanner(strings.NewReader(long))
	sc.Buffer(make([]byte, 16), 64)
	for sc.Scan() {
		fmt.Println("never printed", sc.Text())
	}
	fmt.Println("too long:", sc.Err(), errors.Is(sc.Err(), bufio.ErrTooLong))
	sc = bufio.NewScanner(strings.NewReader(long))
	sc.Buffer(make([]byte, 16), 128)
	cnt := 0
	for sc.Scan() {
		cnt += len(sc.Text())
	}
	fmt.Println("with room: total", cnt, sc.Err())

	// bufio.Reader with the minimum buffer (16).
	br := bufio.NewReaderSize(strings.NewReader("0123456789abcdefghij\nsecond line that is long\nend"), 16)
	pk, err := br.Peek(5)
	fmt.Printf("Peek %q %v Buffered %d Size %d\n", pk, err, br.Buffered(), br.Size())
	_, err = br.Peek(20)
	fmt.Println("Peek too big:", err)
	sl, err := br.ReadSlice('\n')
	fmt.Printf("ReadSlice %q %v\n", sl, err)
	for {
		line, isPrefix, err := br.ReadLine()
		if err != nil {
			fmt.Println("ReadLine end:", err)
			break
		}
		fmt.Printf("ReadLine %q prefix=%v\n", line, isPrefix)
	}
	br = bufio.NewReaderSize(strings.NewReader("héllo wörld\nnext"), 16)
	r, size, _ := br.ReadRune()
	r2, size2, _ := br.ReadRune()
	fmt.Println("ReadRune", string(r), size, string(r2), size2, "UnreadRune", br.UnreadRune(), "UnreadRune again", br.UnreadRune())
	r3, _, _ := br.ReadRune()
	nd, err := br.Discard(3)
	s, err2 := br.ReadString('\n')
	fmt.Printf("after unread %q Discard %d %v ReadString %q %v\n", string(r3), nd, err, s, err2)
	var wt bytes.Buffer
	nw, err := br.WriteTo(&wt)
	fmt.Printf("WriteTo %d %q %v\n", nw, wt.String(), err)
	fmt.Println("UnreadByte at start:", bufio.NewReader(strings.NewReader("x")).UnreadByte())

	// bufio.Writer with a slow underlying writer.
	sw := &slowWriter{n: 3}
	bw := bufio.NewWriterSize(sw, 16)
	fmt.Println("writer Available", bw.Available(), "Buffered", bw.Buffered(), "Size", bw.Size())
	bw.WriteString("hello ")
	fmt.Println("after 6 bytes Buffered", bw.Buffered(), "calls", sw.calls)
	bw.WriteString("world, this exceeds the buffer")
	fmt.Println("after overflow Buffered", bw.Buffered(), "calls", sw.calls, "underlying", sw.buf.Len())
	bw.WriteByte('!')
	bw.WriteRune('☺')
	rf, err := bw.ReadFrom(strings.NewReader(" read-from source"))
	fmt.Println("ReadFrom", rf, err)
	bw.Flush()
	fmt.Printf("flushed %q calls %d\n", sw.buf.String(), sw.calls)

	// ReadWriter.
	var rwBuf bytes.Buffer
	rw := bufio.NewReadWriter(bufio.NewReader(strings.NewReader("in1\nin2\n")), bufio.NewWriter(&rwBuf))
	l1, _ := rw.ReadString('\n')
	rw.WriteString("echo:" + l1)
	l2, _ := rw.ReadString('\n')
	rw.WriteString("echo:" + l2)
	rw.Flush()
	fmt.Printf("ReadWriter %q\n", rwBuf.String())

	// io.Pipe between goroutines, through a TeeReader and MultiWriter.
	pr, pw := io.Pipe()
	go func() {
		w := bufio.NewWriter(pw)
		for i := 0; i < 20; i++ {
			fmt.Fprintf(w, "record %02d: %s\n", i, strings.Repeat("=", i%7))
		}
		w.Flush()
		pw.CloseWithError(errors.New("producer done"))
	}()
	var tee, upper, plain bytes.Buffer
	mw := io.MultiWriter(&plain, upperWriter{&upper})
	n, err := io.Copy(mw, io.TeeReader(pr, &tee))
	fmt.Println("pipe copy", n, "err:", err, "tee equal:", tee.String() == plain.String())
	fmt.Print(upper.String()[:60], "...\n")
	lines := strings.Count(plain.String(), "\n")
	fmt.Println("pipe lines", lines)

	// Pipe with reader closing early.
	pr2, pw2 := io.Pipe()
	done := make(chan error)
	go func() {
		_, err := pw2.Write([]byte("hello"))
		_, err2 := pw2.Write([]byte("world"))
		done <- fmt.Errorf("first=%v second=%w", err, err2)
	}()
	buf5 := make([]byte, 5)
	io.ReadFull(pr2, buf5)
	pr2.Close()
	fmt.Println("pipe read", string(buf5), "writer saw:", <-done)

	// MultiReader, LimitReader, SectionReader, CopyN, ReadAtLeast.
	mr := io.MultiReader(strings.NewReader("abc"), strings.NewReader(""), oneByteReader{strings.NewReader("def")}, bytes.NewReader([]byte("ghi")))
	all, err := io.ReadAll(mr)
	fmt.Printf("MultiReader %q %v\n", all, err)
	lr := io.LimitReader(strings.NewReader("0123456789"), 4)
	lim, _ := io.ReadAll(lr)
	fmt.Printf("LimitReader %q\n", lim)
	sr := io.NewSectionReader(strings.NewReader("0123456789abcdef"), 4, 8)
	sec := make([]byte, 3)
	sr.ReadAt(sec, 2)
	pos, _ := sr.Seek(-2, io.SeekEnd)
	tail, _ := io.ReadAll(sr)
	outer, off, n2 := sr.Outer()
	fmt.Printf("SectionReader ReadAt(2)=%q Seek=%d tail=%q size=%d outer=%v off=%d n=%d\n", sec, pos, tail, sr.Size(), outer != nil, off, n2)
	var cn bytes.Buffer
	copied, err := io.CopyN(&cn, strings.NewReader("short"), 10)
	fmt.Println("CopyN past end", copied, err, cn.String())
	copied, err = io.CopyBuffer(&cn, oneByteReader{strings.NewReader("+buffered")}, make([]byte, 2))
	fmt.Println("CopyBuffer", copied, err, cn.String())
	b8 := make([]byte, 8)
	k, err := io.ReadAtLeast(strings.NewReader("abc"), b8, 5)
	fmt.Println("ReadAtLeast short", k, err)
	k, err = io.ReadAtLeast(strings.NewReader("abcdefgh"), b8[:3], 5)
	fmt.Println("ReadAtLeast small buffer", k, err)
	k, err = io.ReadFull(strings.NewReader(""), b8)
	fmt.Println("ReadFull empty", k, err)
	k, err = io.ReadFull(oneByteReader{strings.NewReader("12345678")}, b8)
	fmt.Println("ReadFull one-byte reader", k, err, string(b8))

	// WriteString, OffsetWriter, NopCloser, Discard.
	var ws bytes.Buffer
	io.WriteString(&ws, "via WriteString")
	fmt.Println(ws.String())
	backing := &writerAtBuf{data: make([]byte, 12)}
	ow := io.NewOffsetWriter(backing, 3)
	ow.Write([]byte("abc"))
	ow.WriteAt([]byte("XY"), 5)
	ow.Seek(1, io.SeekCurrent)
	ow.Write([]byte("z"))
	fmt.Printf("OffsetWriter %q\n", backing.data)
	rc := io.NopCloser(strings.NewReader("closable"))
	fmt.Println("NopCloser close:", rc.Close())
	dn, _ := io.Copy(io.Discard, strings.NewReader(strings.Repeat("d", 12345)))
	fmt.Println("Discard copied", dn)

	// Line-by-line processing of a generated document with word counts.
	var doc strings.Builder
	for i := 0; i < 200; i++ {
		fmt.Fprintf(&doc, "line %d has %s words\n", i, strings.Repeat("w ", i%9))
	}
	sc = bufio.NewScanner(strings.NewReader(doc.String()))
	totalWords, totalLines, longest := 0, 0, 0
	for sc.Scan() {
		totalLines++
		f := strings.Fields(sc.Text())
		totalWords += len(f)
		longest = max(longest, len(sc.Text()))
	}
	fmt.Println("doc lines", totalLines, "words", totalWords, "longest", longest)
}

type writerAtBuf struct{ data []byte }

func (w *writerAtBuf) WriteAt(p []byte, off int64) (int, error) {
	if int(off)+len(p) > len(w.data) {
		return 0, io.ErrShortWrite
	}
	return copy(w.data[off:], p), nil
}
