// Counting bytes equal to a value: a comparison used as a value, summed into
// a wider accumulator.  A byte scanner's whole inner loop.
package main

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
	for r := 0; r < 40000; r++ {
		a[r&32767] = byte(r)
		s += count(a, 42)
	}
	println("s =", s)
}
