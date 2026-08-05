// Ranging over a string, which decodes UTF-8 one rune at a time -- a branchy
// loop over bytes that no vectoriser will touch.
package main

func main() {
	s := ""
	for i := 0; i < 400; i++ {
		s += "ascii-é中\U0001f600-mixed;"
	}
	total := 0
	for r := 0; r < 15000; r++ {
		for _, c := range s {
			total += int(c) & 7
		}
	}
	println("total =", total)
}
