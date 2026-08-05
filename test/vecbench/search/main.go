// A search: the first byte equal to a target, and stop.
//
// This is `memchr`, and it is the shape the vectoriser refuses most often --
// "body branches out of the loop", 341 of the genuine rejections across the Go
// corpus.  A counting loop next door in `count` vectorises because it visits
// every element; this one must not, because it has to stop at the first match
// and report *which* element it was.
//
// Vectorising it means processing a vector at a time, testing whether any lane
// matched, and only then going back to find which -- so the exit test moves
// from every element to every sixteen, and the scalar work happens once.  The
// cost of getting it wrong is reading past the match, which is why the buffer
// here has a match in the middle rather than at the end: a version that ran to
// the end of the array and picked the lowest match would still print the right
// answer, and would be doing far more work than it looks.
package main

//go:noinline
func indexByte(b []byte, c byte) int {
	for i := 0; i < len(b); i++ {
		if b[i] == c {
			return i
		}
	}
	return -1
}

func main() {
	const n = 4096
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i%251) + 1
	}
	total := 0
	for r := 0; r < 200000; r++ {
		// The match moves, so the loop trip count varies and nothing can be
		// hoisted or predicted into always taking the same path.
		at := (r * 37) % n
		old := b[at]
		b[at] = 0
		total += indexByte(b, 0)
		b[at] = old
	}
	println("total =", total)
}
