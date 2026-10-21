// panic and recover, one call in five.
//
// The way out of a panic is the one control transfer in Go that crosses
// frames without returning through them.  On wasm the runtime has no way to
// reset a hardware stack pointer, so recovery unwinds: every Go function on
// wasm returns a flag saying whether to keep unwinding, and the recovered
// frame is resumed through the same br_table that resumes a goroutine.  This
// measures that path -- gopanic running the deferred closure, recover, and the
// unwind back to `safe` -- next to the ordinary return it replaces.
package main

import "fmt"

type bad struct{ n int }

//go:noinline
func check(n int) int {
	if n%5 == 0 {
		panic(bad{n})
	}
	return n * 3
}

//go:noinline
func safe(n int) (r int) {
	defer func() {
		if e := recover(); e != nil {
			r = -e.(bad).n
		}
	}()
	return check(n)
}

func main() {
	total := 0
	for r := 0; r < 600000; r++ {
		total += safe(r)
	}
	fmt.Println("total =", total)
}
