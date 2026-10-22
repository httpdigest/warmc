// defer.  Deferred calls run last-in first-out when the function returns, by
// any path; their arguments (and the receiver of a deferred method value) are
// evaluated when the defer statement runs, while a deferred closure sees the
// variables as they are at return.  A deferred closure can modify named
// results after the return statement has assigned them.  Defers inside a
// loop all wait for the function to end, defers in a called function run
// before the caller continues, and recover() outside a panic returns nil.
// TinyGo keeps a per-frame linked list of deferred calls, so the frames
// here have zero, one, a constant number and a loop-dependent number of
// them.
package main

import (
	"fmt"
	"os"
	"strings"
)

var log []string

func note(s string) { log = append(log, s) }

func flush(title string) {
	fmt.Println(title+":", strings.Join(log, " "))
	log = log[:0]
}

var depth = 3

func init() {
	if len(os.Args) > 1000 {
		depth = 4
	}
}

func lifo() {
	for i := 0; i < 5; i++ {
		defer note(fmt.Sprint("d", i))
	}
	note("body")
}

func argsNow() {
	x := 1
	defer note(fmt.Sprint("arg=", x)) // evaluated now
	defer func() { note(fmt.Sprint("closure=", x)) }()
	defer func(v int) { note(fmt.Sprint("param=", v)) }(x * 10)
	x = 2
	note("set")
	x = 3
}

func namedResult() (n int, s string) {
	defer func() {
		n *= 2
		s += "!"
	}()
	n, s = 5, "hi"
	return n + 1, s + "?" // assigns 6, "hi?" then the defer runs
}

func earlyReturn(k int) (r int) {
	defer func() { r += 100 }()
	if k < 0 {
		return -1
	}
	for i := 0; i < k; i++ {
		defer func() { r += i }()
		if i == 3 {
			return r + 1000
		}
	}
	return k
}

type res struct {
	name string
	open bool
}

func (r *res) Close() { note("close " + r.name); r.open = false }
func (r res) Report() { note(fmt.Sprintf("report %s open=%v", r.name, r.open)) }

func useResources(n int) int {
	opened := 0
	for i := 0; i < n; i++ {
		r := &res{name: fmt.Sprint("r", i), open: true}
		opened++
		defer r.Close()
		defer r.Report() // receiver copied now, while open
	}
	note(fmt.Sprint("using ", opened))
	return opened
}

func nested(level int) int {
	defer note(fmt.Sprint("exit ", level))
	note(fmt.Sprint("enter ", level))
	if level == 0 {
		return 0
	}
	v := nested(level-1) + 1
	note(fmt.Sprint("back ", level, " got ", v))
	return v
}

func noPanic() (r any) {
	defer func() {
		r = recover() // nothing is panicking: nil
	}()
	return "unchanged"
}

func deferInClosure() int {
	total := 0
	f := func(k int) {
		defer func() { total += k }()
		total *= 2
	}
	for i := 1; i <= 4; i++ {
		f(i)
	}
	return total
}

type stack struct{ items []int }

func (s *stack) push(v int) { s.items = append(s.items, v) }

func methodValueDefer() []int {
	s := &stack{}
	push := s.push
	for i := 0; i < 4; i++ {
		defer push(i * i)
	}
	defer func() { s.push(-1) }()
	s.push(99)
	return s.items // the defers append after this slice header was taken
}

func deferredFuncValue() {
	fns := []func(){
		func() { note("f0") },
		func() { note("f1") },
	}
	f := fns[0]
	defer f() // the function value is evaluated now
	f = fns[1]
	defer f()
	note("swap")
}

func main() {
	lifo()
	flush("lifo")
	argsNow()
	flush("args")
	n, s := namedResult()
	fmt.Println("named", n, s)
	for k := -2; k <= 12; k++ {
		fmt.Println("early", k, earlyReturn(k))
	}
	fmt.Println("opened", useResources(depth))
	flush("resources")
	fmt.Println("nested", nested(depth))
	flush("nested")
	fmt.Println("recover", noPanic())
	fmt.Println("in closure", deferInClosure())
	fmt.Println("method value", methodValueDefer())
	deferredFuncValue()
	flush("func value")
	for i := 0; i < 3; i++ {
		func() {
			defer note(fmt.Sprint("iter ", i, " done"))
			note(fmt.Sprint("iter ", i))
		}()
	}
	flush("per iteration")
	for n := 0; n <= 5; n++ {
		fmt.Println("resources", n, useResources(n))
		flush("  log")
	}
	for k := 0; k < 6; k++ {
		fmt.Println("chain", k, deferChain(k), counted(k))
		flush("  log")
	}
	sum := 0
	for i := 0; i < 1000; i++ {
		sum += earlyReturn(i % 7)
	}
	fmt.Println("sum", sum)
}

// deferChain defers a closure per level that wraps the result of the levels
// below it, so the unwinding order shows in the string.
func deferChain(k int) (s string) {
	defer func() { s = "<" + s + ">" }()
	if k == 0 {
		return "core"
	}
	defer func(level int) { s = fmt.Sprint(level, s, level) }(k)
	return deferChain(k - 1)
}

// counted mixes deferred argument evaluation with a loop whose body returns
// early part of the way through.
func counted(k int) (n int) {
	for i := 0; i < 10; i++ {
		defer func(v int) {
			n = n*3 + v
			note(fmt.Sprint(v))
		}(i * k)
		if i == k+2 {
			return 1
		}
	}
	return -1
}
