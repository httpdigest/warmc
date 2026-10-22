// A switch over string values, and strings compared with < and >.  A Go
// switch on strings is not a jump table: TinyGo emits its cases in source
// order as a chain of `call $runtime.stringEqual` against constant (pointer,
// length) pairs -- 34 calls for classify's 34 case labels -- and stringEqual
// compares the lengths first and then loops byte by byte.  Ordering is
// runtime.stringLess, another byte loop behind a call.  An insertion sort
// into a short slice of strings and a pairwise comparison of two token
// streams supply those.
//
// Keyword lookup, command dispatch and option parsing are written exactly
// like this.  What it measures is many calls to a tiny callee whose answer
// is usually decided by the length check, so call overhead and the
// prologue of a leaf function are most of the cost.
package main

var words = []string{
	"break", "case", "chan", "const", "continue", "default", "defer", "else",
	"fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
	"map", "package", "range", "return", "select", "struct", "switch", "type",
	"var", "append", "len", "cap", "make", "new", "nil", "true", "false",
	"iota", "x", "value", "result", "counter", "buffer", "index", "count",
}

//go:noinline
func classify(w string) int {
	switch w {
	case "break", "continue", "goto", "fallthrough":
		return 1
	case "case", "default", "switch", "select":
		return 2
	case "chan", "map", "struct", "interface", "func":
		return 3
	case "const", "var", "type", "import", "package":
		return 4
	case "defer", "go", "return":
		return 5
	case "else", "if", "for", "range":
		return 6
	case "append", "len", "cap", "make", "new":
		return 7
	case "nil", "true", "false", "iota":
		return 8
	}
	return 0
}

// insertion into a sorted slice of strings, by string comparison.
//
//go:noinline
func insert(xs []string, w string) []string {
	i := len(xs)
	xs = append(xs, w)
	for i > 0 && xs[i-1] > w {
		xs[i] = xs[i-1]
		i--
	}
	xs[i] = w
	return xs
}

//go:noinline
func compareAll(a, b []string) int {
	lt, eq := 0, 0
	for i := range a {
		if a[i] < b[i] {
			lt++
		} else if a[i] == b[i] {
			eq++
		}
	}
	return lt*3 + eq
}

func main() {
	const n = 2048
	toks := make([]string, n)
	other := make([]string, n)
	x := uint32(1)
	for i := range toks {
		x = x*1664525 + 1013904223
		toks[i] = words[(x>>8)%uint32(len(words))]
		other[i] = words[(x>>20)%uint32(len(words))]
	}
	sorted := make([]string, 0, 128)
	total := 0
	for r := 0; r < 2700; r++ {
		toks[r&(n-1)] = words[r%len(words)]
		kinds := 0
		for _, t := range toks {
			kinds += classify(t)
		}
		total += kinds + compareAll(toks, other)
		sorted = sorted[:0]
		for i := 0; i < 96; i++ {
			sorted = insert(sorted, toks[(r*96+i)&(n-1)])
		}
		total += len(sorted[0]) + len(sorted[95])*7
	}
	println("total =", total)
}
