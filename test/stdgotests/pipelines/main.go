// Concurrency patterns built from goroutines and channels: a worker pool
// whose jobs carry their index, a multi-stage pipeline (generate, square,
// filter, sum), fan-out to several workers and fan-in through a merge,
// bounded parallelism with a semaphore, an error-group that keeps the first
// error, the classic concurrent prime sieve (a chain of filter goroutines
// that grows by one per prime), and a parallel merge sort.
//
// The sieve is the stress case: by the end there is one goroutine per prime
// found, each blocked on its predecessor, so every value crosses dozens of
// unbuffered hand-offs.  All output is ordered by index or sorted.
package main

import (
	"errors"
	"fmt"
	"sort"
	"strings"
	"sync"
)

type job struct {
	idx  int
	text string
}

type outcome struct {
	idx    int
	words  int
	upper  string
	vowels int
}

func worker(jobs <-chan job, results chan<- outcome, wg *sync.WaitGroup) {
	defer wg.Done()
	for j := range jobs {
		v := 0
		for _, r := range j.text {
			if strings.ContainsRune("aeiouAEIOU", r) {
				v++
			}
		}
		results <- outcome{j.idx, len(strings.Fields(j.text)), strings.ToUpper(j.text), v}
	}
}

func gen(nums ...int) <-chan int {
	out := make(chan int)
	go func() {
		for _, n := range nums {
			out <- n
		}
		close(out)
	}()
	return out
}

func sq(in <-chan int) <-chan int {
	out := make(chan int)
	go func() {
		for n := range in {
			out <- n * n
		}
		close(out)
	}()
	return out
}

func filter(in <-chan int, keep func(int) bool) <-chan int {
	out := make(chan int)
	go func() {
		for n := range in {
			if keep(n) {
				out <- n
			}
		}
		close(out)
	}()
	return out
}

func merge(cs ...<-chan int) <-chan int {
	var wg sync.WaitGroup
	out := make(chan int)
	for _, c := range cs {
		wg.Add(1)
		go func(c <-chan int) {
			defer wg.Done()
			for v := range c {
				out <- v
			}
		}(c)
	}
	go func() { wg.Wait(); close(out) }()
	return out
}

// Prime sieve.
func generate(ch chan<- int, limit int) {
	for i := 2; i <= limit; i++ {
		ch <- i
	}
	close(ch)
}

func sieveFilter(in <-chan int, out chan<- int, prime int) {
	for i := range in {
		if i%prime != 0 {
			out <- i
		}
	}
	close(out)
}

type group struct {
	wg   sync.WaitGroup
	once sync.Once
	err  error
}

func (g *group) Go(f func() error) {
	g.wg.Add(1)
	go func() {
		defer g.wg.Done()
		if err := f(); err != nil {
			g.once.Do(func() { g.err = err })
		}
	}()
}

func (g *group) Wait() error { g.wg.Wait(); return g.err }

func psort(xs []int, depth int) {
	if len(xs) < 16 || depth == 0 {
		sort.Ints(xs)
		return
	}
	mid := len(xs) / 2
	var wg sync.WaitGroup
	wg.Add(2)
	go func() { defer wg.Done(); psort(xs[:mid], depth-1) }()
	go func() { defer wg.Done(); psort(xs[mid:], depth-1) }()
	wg.Wait()
	tmp := make([]int, 0, len(xs))
	i, j := 0, mid
	for i < mid && j < len(xs) {
		if xs[i] <= xs[j] {
			tmp = append(tmp, xs[i])
			i++
		} else {
			tmp = append(tmp, xs[j])
			j++
		}
	}
	tmp = append(tmp, xs[i:mid]...)
	tmp = append(tmp, xs[j:]...)
	copy(xs, tmp)
}

func main() {
	// Worker pool.
	texts := []string{
		"the quick brown fox", "jumps over", "the lazy dog",
		"go wasm on a native compiler", "channels and goroutines",
		"a", "", "lorem ipsum dolor sit amet consectetur",
		"pack my box with five dozen liquor jugs", "sphinx of black quartz judge my vow",
		"how vexingly quick daft zebras jump", "the five boxing wizards jump quickly",
	}
	jobs := make(chan job)
	results := make(chan outcome, 4)
	var wg sync.WaitGroup
	for w := 0; w < 4; w++ {
		wg.Add(1)
		go worker(jobs, results, &wg)
	}
	go func() {
		for i, t := range texts {
			jobs <- job{i, t}
		}
		close(jobs)
	}()
	go func() { wg.Wait(); close(results) }()
	outs := make([]outcome, len(texts))
	for r := range results {
		outs[r.idx] = r
	}
	for _, o := range outs {
		fmt.Printf("pool %2d words=%d vowels=%2d %q\n", o.idx, o.words, o.vowels, o.upper)
	}

	// Pipeline: generate -> square -> keep odd -> collect (order preserved).
	nums := make([]int, 30)
	for i := range nums {
		nums[i] = i + 1
	}
	var odd []int
	for v := range filter(sq(gen(nums...)), func(n int) bool { return n%2 == 1 }) {
		odd = append(odd, v)
	}
	fmt.Println("pipeline odd squares", odd)

	// Fan-out / fan-in: one source, 5 squarers, merged; sorted for printing.
	src := gen(nums...)
	var workers []<-chan int
	for i := 0; i < 5; i++ {
		workers = append(workers, sq(src))
	}
	var merged []int
	for v := range merge(workers...) {
		merged = append(merged, v)
	}
	sort.Ints(merged)
	fmt.Println("fan-in", len(merged), merged)

	// Prime sieve.
	ch := make(chan int)
	go generate(ch, 600)
	var primes []int
	var in <-chan int = ch
	for {
		p, ok := <-in
		if !ok {
			break
		}
		primes = append(primes, p)
		next := make(chan int)
		go sieveFilter(in, next, p)
		in = next
	}
	fmt.Println("sieve found", len(primes), "primes")
	for i := 0; i < len(primes); i += 10 {
		end := min(i+10, len(primes))
		fmt.Println("primes", primes[i:end])
	}

	// Bounded parallelism collecting per-index results.
	sem := make(chan struct{}, 2)
	digits := make([]string, 25)
	var bwg sync.WaitGroup
	for i := range digits {
		bwg.Add(1)
		sem <- struct{}{}
		go func() {
			defer bwg.Done()
			defer func() { <-sem }()
			n := 1
			for k := 0; k < i; k++ {
				n = n * 3 % 1000003
			}
			digits[i] = fmt.Sprint(n)
		}()
	}
	bwg.Wait()
	fmt.Println("bounded", strings.Join(digits, ","))

	// Error group: the first error kept (only one task fails).
	var g group
	partial := make([]int, 10)
	for i := 0; i < 10; i++ {
		g.Go(func() error {
			if i == 7 {
				return fmt.Errorf("task %d: %w", i, errors.ErrUnsupported)
			}
			partial[i] = i * i * i
			return nil
		})
	}
	err := g.Wait()
	fmt.Println("group err:", err, errors.Is(err, errors.ErrUnsupported))
	fmt.Println("group partial", partial)

	// Parallel merge sort.
	xs := make([]int, 500)
	seed := uint32(12345)
	for i := range xs {
		seed = seed*1103515245 + 12345
		xs[i] = int(seed>>16) % 1000
	}
	psort(xs, 4)
	fmt.Println("psort sorted:", sort.IntsAreSorted(xs), "min", xs[0], "max", xs[len(xs)-1])
	for i := 0; i < len(xs); i += 50 {
		fmt.Println("psort", i, xs[i:i+10])
	}

	// Pipeline of string stages with a done channel to stop early.
	done := make(chan struct{})
	words := make(chan string)
	go func() {
		defer close(words)
		for i := 0; ; i++ {
			select {
			case words <- fmt.Sprintf("w%03d", i):
			case <-done:
				return
			}
		}
	}()
	var firstWords []string
	for w := range words {
		firstWords = append(firstWords, strings.ToUpper(w))
		if len(firstWords) == 15 {
			close(done)
			break
		}
	}
	fmt.Println("early stop", firstWords)
}
