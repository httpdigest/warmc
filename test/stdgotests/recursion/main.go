// Deep recursion and goroutine stack growth.  A goroutine starts with a small
// stack in linear memory and the runtime copies it to one twice the size
// whenever a function prologue finds too little room; on wasm the copy also
// has to unwind every wasm frame out to the scheduler and rewind them again
// through each function's resume-point dispatch, so a deep call chain that
// keeps growing its stack crosses that path at every doubling.  This runs
// recursions thousands of frames deep: plain, with large frames (arrays on
// the stack), mutual recursion, recursion through interfaces and closures,
// recursion with a defer at every level and a panic at the bottom recovered
// at the top, pointers into the stack that must be adjusted when it moves,
// and many goroutines each growing their own stack at the same time.
//
// Depths stay below ten thousand wasm frames because wasmtime's default wasm
// stack is 512 KiB and the reference must not overflow.
package main

import (
	"fmt"
	"sort"
	"strings"
	"sync"
)

//go:noinline
func sumTo(n int) int {
	if n == 0 {
		return 0
	}
	return n + sumTo(n-1)
}

//go:noinline
func bigFrame(n int, seed uint64) uint64 {
	var buf [128]uint64 // 1 KiB of stack per frame
	for i := range buf {
		seed = seed*6364136223846793005 + 1442695040888963407
		buf[i] = seed >> 7
	}
	if n == 0 {
		return buf[0]
	}
	r := bigFrame(n-1, seed)
	// Use buf after the call so it has to survive every stack copy.
	var x uint64
	for i := range buf {
		x ^= buf[i] * uint64(i+1)
	}
	return r ^ x
}

//go:noinline
func isEven(n int) bool {
	if n == 0 {
		return true
	}
	return isOdd(n - 1)
}

//go:noinline
func isOdd(n int) bool {
	if n == 0 {
		return false
	}
	return isEven(n - 1)
}

type node interface{ depth() int }
type leaf struct{}
type branch struct{ child node }

func (leaf) depth() int     { return 0 }
func (b branch) depth() int { return 1 + b.child.depth() }

// Pointers into the current frame must be fixed up when the stack moves.
//
//go:noinline
func stackPointers(n int, prev *int, acc *[]int) {
	local := n * 3
	p := &local
	if prev != nil && n%500 == 0 {
		*acc = append(*acc, *prev-*p)
	}
	if n == 0 {
		return
	}
	stackPointers(n-1, p, acc)
	// p still points at this frame's local after children grew the stack.
	*p += 1
	if n%1000 == 0 {
		*acc = append(*acc, *p)
	}
}

var unwound int

//go:noinline
func deferChain(n int, log *[]string) int {
	defer func() {
		unwound++
		if n%1000 == 0 {
			*log = append(*log, fmt.Sprintf("unwind %d", n))
		}
	}()
	if n == 0 {
		panic(fmt.Sprintf("bottom of %d-frame chain", unwound))
	}
	return deferChain(n-1, log) + 1
}

func recoverChain(n int) (msg string, log []string) {
	defer func() {
		if r := recover(); r != nil {
			msg = fmt.Sprint(r)
		}
	}()
	deferChain(n, &log)
	return "no panic", log
}

//go:noinline
func ackermann(m, n int) int {
	switch {
	case m == 0:
		return n + 1
	case n == 0:
		return ackermann(m-1, 1)
	}
	return ackermann(m-1, ackermann(m, n-1))
}

//go:noinline
func buildString(n int) string {
	if n == 0 {
		return ""
	}
	s := buildString(n - 1)
	if n%10 == 0 {
		return s + string(rune('a'+n/10%26))
	}
	return s
}

// Recursive descent parser over a deeply nested expression.
type parser struct {
	s   string
	pos int
}

func (p *parser) expr() int {
	v := p.term()
	for p.pos < len(p.s) && (p.s[p.pos] == '+' || p.s[p.pos] == '-') {
		op := p.s[p.pos]
		p.pos++
		t := p.term()
		if op == '+' {
			v += t
		} else {
			v -= t
		}
	}
	return v
}

func (p *parser) term() int {
	v := p.factor()
	for p.pos < len(p.s) && p.s[p.pos] == '*' {
		p.pos++
		v *= p.factor()
	}
	return v
}

func (p *parser) factor() int {
	if p.s[p.pos] == '(' {
		p.pos++
		v := p.expr()
		p.pos++ // ')'
		return v
	}
	v := 0
	for p.pos < len(p.s) && p.s[p.pos] >= '0' && p.s[p.pos] <= '9' {
		v = v*10 + int(p.s[p.pos]-'0')
		p.pos++
	}
	return v
}

func main() {
	for _, n := range []int{10, 100, 1000, 3000, 5000} {
		fmt.Printf("sumTo(%d) = %d\n", n, sumTo(n))
	}
	for _, n := range []int{1, 10, 100, 1000, 3000} {
		fmt.Printf("bigFrame(%d) = %016x\n", n, bigFrame(n, uint64(n)))
	}
	for _, n := range []int{0, 1, 3000, 3001} {
		fmt.Printf("isEven(%d) = %v isOdd = %v\n", n, isEven(n), isOdd(n))
	}

	var t node = leaf{}
	for i := 0; i < 3000; i++ {
		t = branch{t}
	}
	fmt.Println("interface recursion depth", t.depth())

	var fact func(n int) float64
	fact = func(n int) float64 {
		if n <= 1 {
			return 1
		}
		return float64(n) * fact(n-1)
	}
	var count func(n int) int
	count = func(n int) int {
		if n == 0 {
			return 0
		}
		return 1 + count(n-1)
	}
	fmt.Println("closure recursion", fact(170), count(4000))

	var acc []int
	stackPointers(2500, nil, &acc)
	fmt.Println("stack pointer diffs and updates", acc)

	msg, log := recoverChain(3000)
	fmt.Println("panic through defers:", msg, "unwound", unwound)
	fmt.Println("unwind log", strings.Join(log, ", "))

	fmt.Println("ackermann(2, 500)", ackermann(2, 500), "ackermann(3, 6)", ackermann(3, 6))
	s := buildString(3000)
	fmt.Println("buildString", len(s), s[:30], s[len(s)-10:])

	// Deeply nested expression: ((((1+2)*2+3)*2+4)...).
	var b strings.Builder
	const depth = 1200
	for i := 0; i < depth; i++ {
		b.WriteByte('(')
	}
	b.WriteString("1")
	for i := 0; i < depth; i++ {
		fmt.Fprintf(&b, "+%d)*1", i%10)
	}
	p := &parser{s: b.String()}
	fmt.Println("nested expression value", p.expr(), "consumed", p.pos == len(p.s))

	// Many goroutines growing their own stacks concurrently, with a hand-off
	// in the middle of each recursion so they interleave while deep.
	const G = 24
	results := make([]string, G)
	var wg sync.WaitGroup
	gate := make(chan struct{})
	for g := 0; g < G; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			var deep func(n int) int
			deep = func(n int) int {
				var pad [16]int
				pad[n%16] = n
				if n == 0 {
					<-gate // park while thousands of frames deep
					return g
				}
				return deep(n-1) + pad[n%16]%3
			}
			d := 500 + g*150
			results[g] = fmt.Sprintf("g%02d depth %d -> %d", g, d, deep(d))
		}()
	}
	close(gate)
	wg.Wait()
	sort.Strings(results)
	for _, r := range results {
		fmt.Println(r)
	}

	// Repeat deep calls to exercise stack shrinking at GC and regrowth.
	total := 0
	for round := 0; round < 20; round++ {
		total += sumTo(4000 + round)
		if round%5 == 0 {
			_ = make([]byte, 1<<20) // encourage a collection
		}
	}
	fmt.Println("repeated deep calls total", total)
}
