// Ranging over a string, the same source as test/gobench/utf8scan.  Go decodes
// an ASCII byte inline and calls runtime.decoderune for anything wider: three
// runes in every sixteen here, so the loop is mostly the inline byte path with
// a call on the rest.
package main

import "fmt"

func main() {
	s := ""
	for i := 0; i < 400; i++ {
		s += "ascii-é中\U0001f600-mixed;"
	}
	total := 0
	for r := 0; r < 8000; r++ {
		for _, c := range s {
			total += int(c) & 7
		}
	}
	fmt.Println("total =", total)
}
