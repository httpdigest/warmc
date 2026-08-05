// map[string]int: hashing, probing and growth.  Go's map is a runtime data
// structure, so this measures the code TinyGo's runtime emits as much as the
// program's own.
package main

func main() {
	keys := make([]string, 512)
	for i := range keys {
		keys[i] = "key-" + string(rune('a'+i%26)) + string(rune('a'+(i/26)%26)) +
			string(rune('0'+(i/676)%10))
	}
	total := 0
	for r := 0; r < 1500; r++ {
		m := make(map[string]int, 64)
		for i, k := range keys {
			m[k] = i + r
		}
		for _, k := range keys {
			total += m[k]
		}
	}
	println("total =", total)
}
