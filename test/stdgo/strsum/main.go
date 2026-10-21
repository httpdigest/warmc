// Counting bytes equal to a value, the same source as test/gobench/strsum.
//
// TinyGo's row is at 0.07x because LLVM vectorises this loop before warmc sees
// it.  Go's compiler does not, so here it is the loop as written: a load, a
// compare and a conditional add per byte, behind a bounds check.
package main

import "fmt"

//go:noinline
func count(b []byte, c byte) int {
	n := 0
	for i := 0; i < len(b); i++ {
		if b[i] == c {
			n++
		}
	}
	return n
}

func main() {
	a := make([]byte, 32768)
	for i := range a {
		a[i] = byte(i * 7)
	}
	s := 0
	for r := 0; r < 6000; r++ {
		a[r&32767] = byte(r)
		s += count(a, 42)
	}
	fmt.Println("s =", s)
}
