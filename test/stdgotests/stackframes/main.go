// The runtime's view of its own stack: runtime.Caller and runtime.Callers
// with CallersFrames (function names, base file names and line numbers),
// FuncForPC, frames through closures, methods, generic functions, deferred
// calls and recovered panics, inlined frames being reported as their source
// functions, debug.Stack and runtime.Stack reduced to function names, a deep
// recursion's frame count, and runtime facts that are fixed for a wasip1
// build (GOOS, GOARCH, Compiler).
//
// Go on wasm keeps its own call stack in linear memory and encodes a PC as a
// function index and a resume point within it; traceback walks those frames
// using the pclntab.  Every frame is printed by name and line rather than by
// address, so the output is the same on any engine and any machine.
package main

import (
	"fmt"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"strings"
)

func where(skip int) string {
	pc, file, line, ok := runtime.Caller(skip + 1)
	if !ok {
		return "?"
	}
	return fmt.Sprintf("%s %s:%d", runtime.FuncForPC(pc).Name(), filepath.Base(file), line)
}

func stack(skip int, max int) []string {
	pcs := make([]uintptr, 64)
	n := runtime.Callers(skip+2, pcs)
	frames := runtime.CallersFrames(pcs[:n])
	var out []string
	for {
		f, more := frames.Next()
		if strings.HasPrefix(f.Function, "main.") {
			out = append(out, fmt.Sprintf("%s:%d", f.Function, f.Line))
		}
		if !more || len(out) >= max {
			break
		}
	}
	return out
}

type T struct{ name string }

//go:noinline
func (t T) Value() []string { return stack(0, 3) }

//go:noinline
func (t *T) Pointer() []string { return stack(0, 3) }

//go:noinline
func Generic[X any](x X) []string { return stack(0, 3) }

//go:noinline
func level3() []string { return stack(0, 10) }

//go:noinline
func level2() []string { return level3() }

//go:noinline
func level1() []string { return level2() }

func inlinable() string { return where(0) }

//go:noinline
func recurse(n int) int {
	if n == 0 {
		pcs := make([]uintptr, 512)
		return runtime.Callers(1, pcs)
	}
	return recurse(n-1) + 0
}

//go:noinline
func panicker() {
	var m map[string]int
	m["boom"] = 1
}

func stackNames(buf []byte) []string {
	var names []string
	for _, line := range strings.Split(string(buf), "\n") {
		if strings.HasPrefix(line, "main.") || strings.HasPrefix(line, "runtime.gopanic") || strings.HasPrefix(line, "panic(") {
			name := line
			if i := strings.IndexByte(name, '('); i > 0 {
				name = name[:i]
			}
			names = append(names, name)
		}
	}
	return names
}

func main() {
	fmt.Println("GOOS", runtime.GOOS, "GOARCH", runtime.GOARCH, "Compiler", runtime.Compiler)
	fmt.Println("here:", where(0))
	fmt.Println("inlined helper reports:", inlinable())
	fmt.Println("chain:", level1())
	t := T{"t"}
	fmt.Println("value method:", t.Value())
	fmt.Println("pointer method:", t.Pointer())
	fmt.Println("generic int:", Generic(1))
	fmt.Println("generic string:", Generic("s"))
	fn := func() []string { return stack(0, 2) }
	fmt.Println("closure:", fn())
	func() {
		defer func() { fmt.Println("in defer:", stack(0, 3)) }()
	}()

	// Recovered panic: stack inside the deferred function shows the panic path.
	func() {
		defer func() {
			r := recover()
			fmt.Println("recovered:", r)
			fmt.Println("debug.Stack frames:", stackNames(debug.Stack()))
			fmt.Println("callers in recover:", stack(0, 6))
		}()
		panicker()
	}()

	// runtime.Stack of the current goroutine only.
	buf := make([]byte, 1<<16)
	n := runtime.Stack(buf, false)
	fmt.Println("runtime.Stack frames:", stackNames(buf[:n]))

	// Deep recursion.
	for _, d := range []int{0, 10, 100, 300} {
		fmt.Printf("recursion %d -> callers %d\n", d, recurse(d))
	}

	// FuncForPC details.
	pc, _, _, _ := runtime.Caller(0)
	f := runtime.FuncForPC(pc)
	file, line := f.FileLine(pc)
	fmt.Println("FuncForPC", f.Name(), filepath.Base(file), line > 0, f.Entry() <= pc)
	fmt.Println("FuncForPC(0) is nil:", runtime.FuncForPC(0) == nil)

	// Caller beyond the stack.
	_, _, _, ok := runtime.Caller(1000)
	fmt.Println("Caller(1000) ok:", ok)

	// Goroutine stacks are separate.
	done := make(chan []string)
	go func() { done <- stack(0, 5) }()
	fmt.Println("goroutine stack:", <-done)
}
