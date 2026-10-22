// bufio.Scanner over a strings.Reader: the text split into lines, each line
// split into words with bufio.ScanWords, and the words counted in a
// map[string]int that is then read back in sorted order.
//
// The standard way Go reads text.  Scanner.Scan is a loop around two
// interface calls, both call_indirect: the split function, and Read on the
// io.Reader underneath.  ScanLines finds each newline with bytealg.IndexByte,
// which on wasm is memchr written directly in wasm assembly, a word-at-a-time
// loop that takes ordinary wasm parameters, unlike the Go code around it.
// ScanWords tests each rune with an inlined isSpace, and the text is ASCII, so
// its call to utf8's slow decoder never fires.  Each word is a string
// allocation and a mapassign_faststr.  Map order is random by design, so the
// keys are sorted before they reach the checksum.
package main

import (
	"bufio"
	"fmt"
	"slices"
	"strings"
)

var vocab = []string{"the", "quick", "brown", "fox", "jumps", "over", "lazy",
	"dog", "and", "a", "wasm", "module", "compiles", "to", "native", "code",
	"with", "no", "interpreter", "in", "between", "goroutine", "scheduler",
	"garbage", "collector", "runs", "here", "too"}

func main() {
	var sb strings.Builder
	x := uint32(1)
	for line := 0; line < 2000; line++ {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		words := int(x%12) + 1
		for w := 0; w < words; w++ {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			if w > 0 {
				sb.WriteString(" \t"[x>>31:][:1])
			}
			sb.WriteString(vocab[x%uint32(len(vocab))])
			if x&15 == 0 {
				sb.WriteByte(',')
			}
		}
		sb.WriteByte('\n')
	}
	text := sb.String()

	var acc uint64
	wbuf := make([]byte, 256)
	for r := 0; r < 64; r++ {
		counts := make(map[string]int)
		lines := 0
		ls := bufio.NewScanner(strings.NewReader(text))
		for ls.Scan() {
			lines++
			// The word scanner reuses one buffer rather than allocating its
			// own 4 KiB one per line.
			ws := bufio.NewScanner(strings.NewReader(ls.Text()))
			ws.Buffer(wbuf, len(wbuf))
			ws.Split(bufio.ScanWords)
			for ws.Scan() {
				counts[strings.TrimRight(ws.Text(), ",")]++
			}
		}
		// Map order is random by design, so the keys are sorted first.
		keys := make([]string, 0, len(counts))
		for k := range counts {
			keys = append(keys, k)
		}
		slices.Sort(keys)
		for i, k := range keys {
			acc = acc*31 + uint64(counts[k]*(i+1)) + uint64(len(k))
		}
		acc += uint64(lines)
	}
	fmt.Println("acc =", acc)
}
