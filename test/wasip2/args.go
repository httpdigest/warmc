// What the CLI hands a component: its arguments and its environment.
//
// The gowasi programs are shared with the wasip1 suite and take neither, so
// wasi:cli/environment's three functions -- get-arguments, get-environment
// and initial-cwd -- were reached by nothing.  Each returns a list the host
// has to build inside the guest's memory through its cabi_realloc, which is
// the part with no equivalent in preview1: there the guest passed a buffer in.
//
// argv[0] is dropped because it is not the same string in the two engines --
// wasmtime uses the module path and warmc the executable's -- and that is a
// difference between two ways of naming the same program, not a defect.
package main

import (
	"fmt"
	"os"
	"sort"
)

func main() {
	fmt.Println("args", os.Args[1:])
	env := os.Environ()
	sort.Strings(env)
	fmt.Println("env", env)
	fmt.Println("lookup", os.Getenv("PROBE"), os.Getenv("ABSENT") == "")
	wd, err := os.Getwd()
	fmt.Println("cwd", wd, err == nil)
}
