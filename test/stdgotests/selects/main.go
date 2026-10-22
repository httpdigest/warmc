// select: non-blocking sends and receives through `default`, select over nil
// channels (which never fire), a multiplexing loop that disables each input
// by setting it to nil once it is closed, a quit channel, select used as a
// try-lock, and select in a loop that both sends and receives.
//
// When several cases are ready select picks one at random, so the program
// never prints which case won a race: it prints what is deterministic --
// totals, the sets of values seen (sorted), and the outcome of selects where
// exactly one case can proceed.  selectgo itself is a large runtime function
// with a randomised poll order, a lock order sort, and parking on several
// channels at once; a miscompile in it loses or duplicates values.
package main

import (
	"fmt"
	"sort"
	"sync"
)

func trySend(c chan int, v int) bool {
	select {
	case c <- v:
		return true
	default:
		return false
	}
}

func tryRecv(c chan int) (int, bool, bool) {
	select {
	case v, ok := <-c:
		return v, ok, true
	default:
		return 0, false, false
	}
}

func main() {
	// Non-blocking send into a buffer of 3.
	c := make(chan int, 3)
	for i := 0; i < 5; i++ {
		fmt.Println("trySend", i, trySend(c, i*10), "len", len(c))
	}
	for i := 0; i < 5; i++ {
		v, ok, got := tryRecv(c)
		fmt.Println("tryRecv", i, v, ok, got)
	}
	close(c)
	v, ok, got := tryRecv(c)
	fmt.Println("tryRecv closed", v, ok, got)

	// Unbuffered with no partner: default always.
	u := make(chan int)
	fmt.Println("unbuffered trySend", trySend(u, 1))
	_, _, got = tryRecv(u)
	fmt.Println("unbuffered tryRecv got", got)

	// nil channels never fire.
	var nilc chan int
	select {
	case nilc <- 1:
		fmt.Println("BUG: sent on nil")
	case <-nilc:
		fmt.Println("BUG: received from nil")
	default:
		fmt.Println("nil channels: default taken")
	}

	// Exactly one ready case among several.
	a, b, d := make(chan string, 1), make(chan string, 1), make(chan string, 1)
	for round := 0; round < 6; round++ {
		switch round % 3 {
		case 0:
			a <- fmt.Sprint("a", round)
		case 1:
			b <- fmt.Sprint("b", round)
		case 2:
			d <- fmt.Sprint("d", round)
		}
		select {
		case s := <-a:
			fmt.Println("one-ready got from a:", s)
		case s := <-b:
			fmt.Println("one-ready got from b:", s)
		case s := <-d:
			fmt.Println("one-ready got from d:", s)
		}
	}

	// Multiplex three producers; disable each when closed.
	mk := func(base, n int) chan int {
		ch := make(chan int)
		go func() {
			for i := 0; i < n; i++ {
				ch <- base + i
			}
			close(ch)
		}()
		return ch
	}
	x, y, z := mk(100, 10), mk(200, 15), mk(300, 5)
	var seen []int
	closedOrder := 0
	for x != nil || y != nil || z != nil {
		select {
		case v, ok := <-x:
			if !ok {
				x = nil
				closedOrder++
				continue
			}
			seen = append(seen, v)
		case v, ok := <-y:
			if !ok {
				y = nil
				closedOrder++
				continue
			}
			seen = append(seen, v)
		case v, ok := <-z:
			if !ok {
				z = nil
				closedOrder++
				continue
			}
			seen = append(seen, v)
		}
	}
	sort.Ints(seen)
	fmt.Println("multiplexed", len(seen), "values, closes", closedOrder)
	fmt.Println(seen)

	// Quit channel: worker loops with select until told to stop, then reports.
	work := make(chan int)
	quit := make(chan struct{})
	result := make(chan int)
	go func() {
		total := 0
		for {
			select {
			case w := <-work:
				total += w * w
			case <-quit:
				result <- total
				return
			}
		}
	}()
	for i := 1; i <= 30; i++ {
		work <- i
	}
	close(quit)
	fmt.Println("worker total", <-result)

	// select as a try-lock with a 1-buffered channel.
	lock := make(chan struct{}, 1)
	tryLock := func() bool {
		select {
		case lock <- struct{}{}:
			return true
		default:
			return false
		}
	}
	unlock := func() { <-lock }
	fmt.Println("trylock1", tryLock())
	fmt.Println("trylock2", tryLock())
	unlock()
	fmt.Println("trylock3", tryLock())
	unlock()

	// Send and receive in the same select loop: a bounded relay.
	in := make(chan int)
	out := make(chan int)
	go func() {
		for i := 0; i < 20; i++ {
			in <- i
		}
		close(in)
	}()
	var collected []int
	var cwg sync.WaitGroup
	cwg.Add(1)
	go func() {
		defer cwg.Done()
		for v := range out {
			collected = append(collected, v)
		}
	}()
	var queue []int
	inC := in
	for inC != nil || len(queue) > 0 {
		var sendC chan int
		var next int
		if len(queue) > 0 {
			sendC = out
			next = queue[0]
		}
		select {
		case v, ok := <-inC:
			if !ok {
				inC = nil
				continue
			}
			queue = append(queue, v*v)
		case sendC <- next:
			queue = queue[1:]
		}
	}
	close(out)
	cwg.Wait()
	fmt.Println("relay preserved order:", collected)

	// Randomised choice among two always-ready cases: only totals printed.
	r1 := make(chan int, 100)
	r2 := make(chan int, 100)
	for i := 0; i < 100; i++ {
		r1 <- 1
		r2 <- 2
	}
	sum, n := 0, 0
	for n < 150 {
		select {
		case v := <-r1:
			sum += v
		case v := <-r2:
			sum += v
		}
		n++
	}
	left := len(r1) + len(r2)
	fmt.Println("random choice: taken", n, "left", left, "sum in range", sum >= 150 && sum <= 300)
	// Drain the rest and check the grand total is exact.
	for len(r1) > 0 {
		sum += <-r1
	}
	for len(r2) > 0 {
		sum += <-r2
	}
	fmt.Println("grand total", sum)

	// Empty select-with-default in a loop counts iterations.
	spins := 0
	for i := 0; i < 1000; i++ {
		select {
		default:
			spins++
		}
	}
	fmt.Println("spins", spins)
}
