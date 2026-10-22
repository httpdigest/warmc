// Channels: unbuffered hand-offs, buffered queues with len and cap observed
// as they fill and drain, close and the comma-ok receive after it, range over
// a closed channel, directional channel types, channels of channels as reply
// addresses, a buffered channel as a counting semaphore, and the zero values a
// closed channel yields for every element type.
//
// Each channel operation is a call into the runtime (chansend, chanrecv,
// closechan) that may park the goroutine; this prints every value moved and
// every observed length so a lost wake-up, a reordered queue or a wrong
// zero value shows up as a diff.
package main

import (
	"fmt"
	"sync"
)

type point struct {
	X, Y int
	Name string
}

func producer(n int, out chan<- int) {
	for i := 0; i < n; i++ {
		out <- i * i
	}
	close(out)
}

func doubler(in <-chan int, out chan<- int) {
	for v := range in {
		out <- v * 2
	}
	close(out)
}

type request struct {
	n     int
	reply chan int
}

func server(reqs <-chan request) {
	for r := range reqs {
		s := 0
		for i := 1; i <= r.n; i++ {
			s += i
		}
		r.reply <- s
	}
}

func main() {
	// Unbuffered hand-off.
	ch := make(chan int)
	go producer(10, ch)
	for v := range ch {
		fmt.Println("unbuffered recv", v)
	}

	// Buffered: fill, observe len/cap, drain.
	b := make(chan string, 5)
	fmt.Println("empty len/cap", len(b), cap(b))
	for i := 0; i < 5; i++ {
		b <- fmt.Sprint("item", i)
		fmt.Println("after send", i, "len", len(b))
	}
	for i := 0; i < 3; i++ {
		fmt.Println("recv", <-b, "len", len(b))
	}
	b <- "late1"
	b <- "late2"
	close(b)
	for s := range b {
		fmt.Println("drain", s)
	}
	v, ok := <-b
	fmt.Printf("after close: %q %v\n", v, ok)

	// Zero values from closed channels of different element types.
	ci := make(chan int, 1)
	cf := make(chan float64, 1)
	cp := make(chan *point, 1)
	cs := make(chan point, 1)
	cl := make(chan []int, 1)
	cm := make(chan map[string]int, 1)
	ce := make(chan error, 1)
	cs <- point{1, 2, "a"}
	close(ci)
	close(cf)
	close(cp)
	close(cs)
	close(cl)
	close(cm)
	close(ce)
	x1, ok1 := <-ci
	x2, ok2 := <-cf
	x3, ok3 := <-cp
	x4, ok4 := <-cs
	x4b, ok4b := <-cs
	x5, ok5 := <-cl
	x6, ok6 := <-cm
	x7, ok7 := <-ce
	fmt.Println("closed int", x1, ok1)
	fmt.Println("closed float", x2, ok2)
	fmt.Println("closed ptr", x3 == nil, ok3)
	fmt.Printf("closed struct buffered %+v %v\n", x4, ok4)
	fmt.Printf("closed struct empty %+v %v\n", x4b, ok4b)
	fmt.Println("closed slice", x5 == nil, ok5)
	fmt.Println("closed map", x6 == nil, ok6)
	fmt.Println("closed error", x7 == nil, ok7)

	// Pipeline of directional channels.
	src := make(chan int)
	dst := make(chan int, 3)
	go producer(8, src)
	go doubler(src, dst)
	var got []int
	for v := range dst {
		got = append(got, v)
	}
	fmt.Println("doubled", got)

	// Channels of channels: request/reply.
	reqs := make(chan request)
	go server(reqs)
	for _, n := range []int{0, 1, 5, 10, 100, 1000} {
		r := request{n, make(chan int)}
		reqs <- r
		fmt.Println("server sum", n, "=", <-r.reply)
	}
	close(reqs)

	// Semaphore: at most 3 holders; record the maximum concurrency observed.
	sem := make(chan struct{}, 3)
	var mu sync.Mutex
	cur, peak := 0, 0
	var wg sync.WaitGroup
	results := make([]int, 12)
	for i := 0; i < 12; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			sem <- struct{}{}
			mu.Lock()
			cur++
			if cur > peak {
				peak = cur
			}
			mu.Unlock()
			results[i] = i * 11
			mu.Lock()
			cur--
			mu.Unlock()
			<-sem
		}()
	}
	wg.Wait()
	fmt.Println("semaphore results", results, "peak<=3", peak <= 3, "peak>=1", peak >= 1)

	// Ping-pong with a single unbuffered channel pair, strictly alternating.
	ping, pong := make(chan int), make(chan int)
	go func() {
		for v := range ping {
			pong <- v + 1
		}
		close(pong)
	}()
	v2 := 0
	for i := 0; i < 20; i++ {
		ping <- v2
		v2 = <-pong
		v2 *= 2
		fmt.Println("pingpong", i, v2)
	}
	close(ping)
	_, okp := <-pong
	fmt.Println("pong closed", !okp)

	// Channel of functions.
	fc := make(chan func(int) int, 4)
	fc <- func(x int) int { return x + 1 }
	fc <- func(x int) int { return x * x }
	fc <- func(x int) int { return -x }
	close(fc)
	acc := 7
	for f := range fc {
		acc = f(acc)
		fmt.Println("func chan acc", acc)
	}

	// Large struct elements through a buffered channel.
	type big struct {
		A [16]int64
		S string
	}
	bc := make(chan big, 4)
	go func() {
		for i := 0; i < 6; i++ {
			var x big
			for j := range x.A {
				x.A[j] = int64(i*100 + j)
			}
			x.S = fmt.Sprint("big", i)
			bc <- x
		}
		close(bc)
	}()
	for x := range bc {
		s := int64(0)
		for _, a := range x.A {
			s += a
		}
		fmt.Println("big elem", x.S, s, x.A[0], x.A[15])
	}

	// nil channel in a len/cap context.
	var nc chan int
	fmt.Println("nil chan len/cap", len(nc), cap(nc))

	// Multiple producers with fixed counts, sum by producer id.
	type tagged struct{ id, v int }
	mc := make(chan tagged, 2)
	var pwg sync.WaitGroup
	for id := 0; id < 4; id++ {
		pwg.Add(1)
		go func() {
			defer pwg.Done()
			for k := 1; k <= 25; k++ {
				mc <- tagged{id, k * (id + 1)}
			}
		}()
	}
	go func() { pwg.Wait(); close(mc) }()
	sums := make([]int, 4)
	counts := make([]int, 4)
	for t := range mc {
		sums[t.id] += t.v
		counts[t.id]++
	}
	fmt.Println("per-producer sums", sums, "counts", counts)
}
