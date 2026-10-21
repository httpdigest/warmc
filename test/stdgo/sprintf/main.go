// fmt.Sprintf with a handful of verbs, a struct among them.
//
// The most common way real Go turns values into text, and nothing like the
// hand-rolled strconv.AppendInt in strbuild: every argument is boxed into an
// interface, the format string is parsed on every call, the verbs dispatch on
// the dynamic type, and %v on a struct goes through reflect to walk its
// fields.  So this is a large amount of library code reached through type
// switches and indirect calls, with an allocation for the result.
package main

import "fmt"

type point struct {
	X, Y int
	Tag  string
}

func main() {
	names := []string{"alpha", "beta", "gamma", "delta"}
	total := 0
	for r := 0; r < 120000; r++ {
		p := point{r, r * 2, names[r&3]}
		s := fmt.Sprintf("%d:%x:%s:%6.2f:%v|", r, r*7, names[(r>>2)&3],
			float64(r%1000)/8, p)
		total += len(s) + int(s[len(s)/2])
	}
	fmt.Println("total =", total)
}
