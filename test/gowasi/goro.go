// Goroutines and channels, which need TinyGo's asyncify scheduler.
//
// tinygo: -scheduler=asyncify
//
// Worth its own case because asyncify does not just add a runtime: it rewrites
// every function that can suspend into a state machine with an unwind and a
// rewind path, so the module warmc sees is a shape nothing else here produces
// -- deep switch dispatch, indirect calls, and locals saved and restored
// across them.
package main

import (
	"fmt"
	"sync"
)

func main() {
	ch := make(chan int, 4)
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func(i int) { defer wg.Done(); ch <- i * i }(i)
	}
	go func() { wg.Wait(); close(ch) }()
	sum := 0
	n := 0
	for v := range ch {
		sum += v
		n++
	}
	fmt.Println("goroutines", n, sum)
}
