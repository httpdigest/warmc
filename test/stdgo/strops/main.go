// The strings package over a generated 4 KB text: Split, Fields, IndexByte,
// Index, LastIndex, Count, ToUpper, EqualFold, ReplaceAll and a Replacer.
//
// Most of strings bottoms out in internal/bytealg, and on wasm IndexByte is
// the one routine Go writes in wasm by hand: memchr, a word-at-a-time loop over
// i32 loads using the 0x01010101 trick, which takes ordinary wasm parameters
// rather than Go's stack in linear memory.  Index finds candidates for the
// first byte with it and confirms with runtime.memequal, switching to
// Rabin-Karp if too many fail; Count calls Index repeatedly, and LastIndex is
// Rabin-Karp outright.  NewReplacer with multi-byte patterns builds the generic
// replacer: a byte-indexed table skips positions no pattern starts at, and a
// trie is walked at the rest.  Split and Fields allocate slices of substrings
// that share the text rather than copying it.
package main

import (
	"fmt"
	"strings"
)

var parts = []string{"Alpha", "beta", "GAMMA", "delta", "Epsilon", "zeta",
	"ETA", "theta", "iota", "Kappa"}

func main() {
	var sb strings.Builder
	x := uint32(99)
	for i := 0; i < 600; i++ {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		sb.WriteString(parts[x%10])
		switch x >> 28 {
		case 0, 1:
			sb.WriteString("  ")
		case 2:
			sb.WriteString("=>")
		default:
			sb.WriteByte(',')
		}
	}
	text := sb.String()
	rep := strings.NewReplacer("=>", " -> ", "beta", "BETA", ",,", ",")
	var acc uint64
	for r := 0; r < 1350; r++ {
		o := r % 1000
		fields := strings.Split(text, ",")
		for _, f := range fields {
			for _, w := range strings.Fields(f) {
				if strings.EqualFold(w, parts[r%10]) {
					acc += 3
				}
				acc += uint64(strings.IndexByte(w, 'a') + 1)
			}
			acc += uint64(strings.Index(f, "ta=>") + 2)
		}
		up := strings.ToUpper(text[o : o+400])
		acc += uint64(strings.Count(up, "ETA"))
		acc += uint64(strings.LastIndex(text, parts[r%10]))
		s := strings.ReplaceAll(text, parts[r%10], "<>")
		acc += uint64(len(s))
		s = rep.Replace(text)
		acc = acc*31 + uint64(len(s)) + uint64(s[len(s)/3])
		if strings.HasPrefix(s[o:], "eta") || strings.Contains(s[o:o+20], "Kap") {
			acc++
		}
	}
	fmt.Println("acc =", acc)
}
