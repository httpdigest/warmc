// Goroutines under Go's own scheduler: spawning, joining with sync.WaitGroup
// (Add/Done and the newer WaitGroup.Go), results gathered by index, nested
// spawns, runtime.Gosched hand-offs, runtime.Goexit running its defers, a
// thousand goroutines alive at once, and a recursive divide-and-conquer sum
// that forks at every level.
//
// Everything the scheduler does on wasm happens inside the module: there is
// one thread, and a goroutine switch is the runtime saving its own stack
// pointer in linear memory and unwinding out to the scheduler loop through
// the resumable-function protocol every Go function on wasm is compiled to.
// A miscompile there shows up as a lost or duplicated result, a wrong sum, or
// a deadlock, so every result is printed rather than folded into one number.
//
// Interleaving is not deterministic in general, so nothing is printed from
// inside a goroutine: each writes into its own slot and main prints after the
// join.
package main

import (
	"fmt"
	"runtime"
	"sync"
)

func square(i int) int { return i * i }

//go:noinline
func psum(xs []int, depth int) int {
	if len(xs) <= 4 || depth == 0 {
		s := 0
		for _, x := range xs {
			s += x
		}
		return s
	}
	mid := len(xs) / 2
	var left int
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		left = psum(xs[:mid], depth-1)
	}()
	right := psum(xs[mid:], depth-1)
	wg.Wait()
	return left + right
}

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	// 1. Basic fan-out, results by index.
	res := make([]int, 20)
	var wg sync.WaitGroup
	for i := range res {
		wg.Add(1)
		go func() {
			defer wg.Done()
			res[i] = square(i) + i
		}()
	}
	wg.Wait()
	for i, r := range res {
		fmt.Printf("fanout[%d] = %d\n", i, r)
	}

	// 2. WaitGroup.Go.
	var wg2 sync.WaitGroup
	fibs := make([]int, 16)
	for i := range fibs {
		wg2.Go(func() { fibs[i] = fib(i) })
	}
	wg2.Wait()
	fmt.Println("fibs:", fibs)

	// 3. Nested spawns: each goroutine spawns children; tree of results.
	tree := make([][]string, 5)
	var outer sync.WaitGroup
	for i := range tree {
		outer.Add(1)
		go func() {
			defer outer.Done()
			row := make([]string, i+1)
			var inner sync.WaitGroup
			for j := range row {
				inner.Add(1)
				go func() {
					defer inner.Done()
					row[j] = fmt.Sprintf("%d.%d", i, j)
				}()
			}
			inner.Wait()
			tree[i] = row
		}()
	}
	outer.Wait()
	for i, row := range tree {
		fmt.Println("tree", i, row)
	}

	// 4. Gosched ping-pong over a shared counter guarded by a channel turn.
	turn := make(chan int)
	done := make(chan []int)
	go func() {
		var log []int
		for v := range turn {
			runtime.Gosched()
			log = append(log, v*10)
		}
		done <- log
	}()
	for i := 0; i < 12; i++ {
		runtime.Gosched()
		turn <- i
	}
	close(turn)
	fmt.Println("gosched log:", <-done)

	// 5. Goexit runs deferred calls and stops only that goroutine.
	trace := make([]string, 0)
	var mu sync.Mutex
	add := func(s string) { mu.Lock(); trace = append(trace, s); mu.Unlock() }
	var wg3 sync.WaitGroup
	wg3.Add(1)
	go func() {
		defer wg3.Done()
		defer add("deferred-outer")
		func() {
			defer add("deferred-inner")
			add("before-goexit")
			runtime.Goexit()
			add("unreachable")
		}()
		add("unreachable-2")
	}()
	wg3.Wait()
	fmt.Println("goexit trace:", trace)

	// 6. Many goroutines alive at once, all blocked on one gate.
	const many = 1000
	gate := make(chan struct{})
	out := make([]int32, many)
	var wg4 sync.WaitGroup
	for i := 0; i < many; i++ {
		wg4.Add(1)
		go func(k int) {
			defer wg4.Done()
			<-gate
			out[k] = int32(k*k%997) - 500
		}(i)
	}
	close(gate)
	wg4.Wait()
	var sum int64
	for i, v := range out {
		sum += int64(v) * int64(i%7+1)
		if i%100 == 0 {
			fmt.Printf("many[%d] = %d\n", i, v)
		}
	}
	fmt.Println("many sum:", sum)

	// 7. Recursive parallel sum at several depths.
	xs := make([]int, 1000)
	for i := range xs {
		xs[i] = (i*7919)%1000 - 300
	}
	for d := 0; d <= 8; d++ {
		fmt.Printf("psum depth %d = %d\n", d, psum(xs, d))
	}

	// 8. Goroutines capturing per-iteration loop variables (Go 1.22 semantics).
	caps := make([]int, 10)
	var wg5 sync.WaitGroup
	for i := 0; i < 10; i++ {
		wg5.Add(1)
		go func() { defer wg5.Done(); caps[i] = i * 3 }()
	}
	wg5.Wait()
	fmt.Println("per-iteration captures:", caps)

	// 9. A goroutine returning its result over a channel of results with index.
	type result struct {
		idx int
		val string
	}
	rc := make(chan result)
	for i := 0; i < 8; i++ {
		go func() { rc <- result{i, fmt.Sprint(fib(i + 10))} }()
	}
	collected := make([]string, 8)
	for range 8 {
		r := <-rc
		collected[r.idx] = r.val
	}
	fmt.Println("collected:", collected)
	fmt.Println("GOMAXPROCS(0) >= 1:", runtime.GOMAXPROCS(0) >= 1)
}
