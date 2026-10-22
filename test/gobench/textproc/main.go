// bytes.Buffer, strings.Builder and the strings package over CSV-like text:
// Split, TrimSpace, Contains, HasPrefix, HasSuffix, Index, Join, ToUpper,
// Fields, Count, LastIndex, Replace, ReplaceAll and a strings.Replacer.  All
// of it is Go's own library compiled by TinyGo, reached by `call`s, with
// runtime.alloc behind every Split and Fields and the Builder or Buffer
// growth behind every write.  NewReplacer builds a trie (`trieNode.add`),
// and Replace walks it inlined into main, writing each piece through a
// `$invoke` thunk for io.StringWriter that picks the writer by type code.
//
// Text munging is what a large share of real Go does, and unlike strsum or
// utf8scan it is not one loop: it is many short calls, each with its own
// small loop and its own allocation, so the cost is spread across the
// library and the allocator.
package main

import (
	"bytes"
	"strings"
)

var rows = []string{
	"alpha,  12, red  ,north",
	"beta,7,green,south",
	"gamma , 1999 ,blue,  east",
	"delta,42,red,west",
	"epsilon,  3,   yellow ,north",
	"zeta,88,green,  south",
}

//go:noinline
func process(csv string, buf *bytes.Buffer) int {
	buf.Reset()
	score := 0
	for _, line := range strings.Split(csv, "\n") {
		if line == "" {
			continue
		}
		cols := strings.Split(line, ",")
		for i := range cols {
			cols[i] = strings.TrimSpace(cols[i])
		}
		if strings.Contains(cols[2], "re") {
			score += len(cols[0])
		}
		if strings.HasPrefix(cols[3], "no") || strings.HasSuffix(cols[3], "st") {
			score++
		}
		score += strings.Index(line, cols[1])
		buf.WriteString(strings.ToUpper(cols[0][:1]))
		buf.WriteString(cols[0][1:])
		buf.WriteByte('=')
		buf.WriteString(strings.Join(cols[1:], "|"))
		buf.WriteByte('\n')
	}
	return score
}

//go:noinline
func words(text string) int {
	fs := strings.Fields(text)
	s := len(fs)
	for _, f := range fs {
		s += len(f) * int(f[0]&7)
	}
	return s
}

func main() {
	var sb strings.Builder
	for i := 0; i < 160; i++ {
		sb.WriteString(rows[(i*5)%len(rows)])
		sb.WriteByte('\n')
	}
	csv := sb.String()
	r := strings.NewReplacer("red", "crimson", "north", "N", "|", " | ")
	var buf bytes.Buffer
	total := 0
	for k := 0; k < 1200; k++ {
		total += process(csv, &buf)
		out := buf.String()
		total += len(out) + strings.Count(out, "|")
		rep := r.Replace(out)
		total += len(rep) + words(rep)
		rep = strings.ReplaceAll(rep, " | ", "/")
		total += strings.LastIndex(rep, "Zeta") + len(rep)
		if k%50 == 0 {
			csv = strings.Replace(csv, "beta", "Beta", 1)
		}
	}
	println("total =", total)
}
