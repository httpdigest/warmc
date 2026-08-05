// The status a component's run returns.
//
// wasi:cli/run's `run: func() -> result` lifts to a single i32, and the whole
// of what a command reports is whether that is 0 or 1 -- there is no exit
// code in it.  A non-zero os.Exit goes the other way, through
// wasi:cli/exit's `exit`, whose argument is the same one-bit result.  So a
// component cannot exit 2, and both engines have to agree that it exits 1.
package main

import (
	"fmt"
	"os"
)

func main() {
	fmt.Println("before")
	os.Exit(1)
}
