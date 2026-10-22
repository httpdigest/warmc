// Control flow.  Labeled break and continue out of nested for loops, out of
// a switch inside a loop and out of a select; goto forwards and backwards
// (a loop built from goto alone); switch with fallthrough chains, with no
// tag, with an init statement, with several values per case and with
// non-constant cases evaluated in order; for with only a condition, with
// range over an integer, a string, a map (sorted), a function and a closed
// buffered channel; and select with a default on buffered channels.  With
// -scheduler=none nothing may block, so the channels are only ever used
// within their capacity.  These shapes become br_table, nested blocks and
// irreducible-looking branch webs in wasm; the printed traces fix the
// order every branch was taken in.
package main

import (
	"fmt"
	"os"
	"sort"
)

var limit = 10

func init() {
	if len(os.Args) > 1000 {
		limit = 11
	}
}

func grade(score int) string {
	switch {
	case score >= 90:
		return "A"
	case score >= 80:
		return "B"
	case score >= 70:
		return "C"
	case score < 0, score > 100:
		return "invalid"
	default:
		return "F"
	}
}

func fall(n int) []string {
	var out []string
	switch n % 5 {
	case 0:
		out = append(out, "zero")
		fallthrough
	case 1:
		out = append(out, "one")
		fallthrough
	case 2:
		out = append(out, "two")
	case 3:
		out = append(out, "three")
		fallthrough
	default:
		out = append(out, "default")
	}
	return out
}

func kind(r rune) string {
	switch r {
	case 'a', 'e', 'i', 'o', 'u':
		return "vowel"
	case ' ', '\t', '\n':
		return "space"
	case '0', '1', '2', '3', '4', '5', '6', '7', '8', '9':
		return "digit"
	}
	if r >= 'a' && r <= 'z' {
		return "consonant"
	}
	return "other"
}

var evals []int

func val(n int) int { evals = append(evals, n); return n }

func firstMatch(x int) string {
	evals = evals[:0]
	switch x {
	case val(1), val(2):
		return fmt.Sprint("one-or-two ", evals)
	case val(3):
		return fmt.Sprint("three ", evals)
	case val(4), val(5), val(6):
		return fmt.Sprint("four-to-six ", evals)
	}
	return fmt.Sprint("none ", evals)
}

func gotoLoop(n int) (sum int) {
	i := 0
loop:
	if i >= n {
		goto done
	}
	sum += i
	i++
	goto loop
done:
	return
}

func gotoSkip(xs []int) []int {
	var out []int
	for _, x := range xs {
		if x < 0 {
			goto skip
		}
		out = append(out, x*2)
		continue
	skip:
		out = append(out, -1)
	}
	return out
}

func findPair(grid [][]int, target int) (int, int, int) {
	checks := 0
outer:
	for i, row := range grid {
		for j, v := range row {
			checks++
			if v < 0 {
				continue outer
			}
			if v == target {
				return i, j, checks
			}
			if v > 100 {
				break outer
			}
		}
	}
	return -1, -1, checks
}

func primes(n int) []int {
	var ps []int
next:
	for c := 2; len(ps) < n; c++ {
		for _, p := range ps {
			if p*p > c {
				break
			}
			if c%p == 0 {
				continue next
			}
		}
		ps = append(ps, c)
	}
	return ps
}

func main() {
	fmt.Println("-- switch")
	for _, s := range []int{95, 85, 75, 65, -3, 101, 90, 80} {
		fmt.Print(grade(s), " ")
	}
	fmt.Println()
	for n := 0; n < limit; n++ {
		fmt.Println(n, fall(n))
	}
	for _, r := range "hi 42!\tok" {
		fmt.Print(kind(r), " ")
	}
	fmt.Println()
	for x := 0; x <= 7; x++ {
		fmt.Println(x, firstMatch(x))
	}
	switch x := limit * 2; {
	case x > 15:
		fmt.Println("init stmt big", x)
	default:
		fmt.Println("init stmt small", x)
	}
	fmt.Println("-- break inside switch inside for")
	n := 0
	for i := 0; i < limit; i++ {
		switch {
		case i%4 == 3:
			break // leaves the switch only
		case i == 8:
			n += 100
		default:
			n += i
		}
	}
	fmt.Println(n)
	n = 0
loop:
	for i := 0; ; i++ {
		switch {
		case i > 6:
			break loop
		case i%2 == 0:
			continue loop
		}
		n += i
	}
	fmt.Println(n)
	fmt.Println("-- goto")
	fmt.Println(gotoLoop(limit), gotoLoop(0), gotoSkip([]int{1, -2, 3, -4, 5}))
	fmt.Println("-- labeled loops")
	grid := [][]int{{1, 2, 3}, {4, -1, 99}, {7, 8, 9}, {10, 200, 11}, {12, 13}}
	for _, t := range []int{3, 99, 8, 11, 13, 500} {
		i, j, c := findPair(grid, t)
		fmt.Println(t, i, j, c)
	}
	fmt.Println(primes(25))
	triples := 0
	for a := 1; a < 30; a++ {
		for b := a; b < 30; b++ {
			for c := b; c < 40; c++ {
				if a*a+b*b == c*c {
					fmt.Print("(", a, b, c, ") ")
					triples++
					break
				}
				if a*a+b*b < c*c {
					break
				}
			}
		}
	}
	fmt.Println(triples)
	fmt.Println("-- for forms")
	k := 1
	for k < 1000 {
		k *= 3
	}
	fmt.Println(k)
	total := 0
	for i := range limit {
		total += i * i
	}
	for range 3 {
		total++
	}
	fmt.Println(total)
	for i, r := range "aé日" {
		fmt.Print(i, ":", string(r), " ")
	}
	fmt.Println()
	m := map[string]int{"x": 1, "y": 2, "z": 3, "w": 4}
	keys := []string{}
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	fmt.Println(keys)
	fmt.Println("-- channels without goroutines")
	ch := make(chan int, 8)
	for i := 0; i < 5; i++ {
		ch <- i * i
	}
	fmt.Println(len(ch), cap(ch))
	fmt.Println(<-ch, <-ch)
	ch <- 100
	close(ch)
	for v := range ch {
		fmt.Print(v, " ")
	}
	v, ok := <-ch
	fmt.Println(v, ok, len(ch))
	sc := make(chan string, 2)
	for i := 0; i < 5; i++ {
		select {
		case sc <- fmt.Sprint("msg", i):
			fmt.Println("sent", i)
		default:
			fmt.Println("full at", i)
			s := <-sc
			fmt.Println("drained", s)
		}
	}
	var nilCh chan int
	for i := 0; i < 3; i++ {
		select {
		case v := <-nilCh:
			fmt.Println("impossible", v)
		case s := <-sc:
			fmt.Println("got", s)
		default:
			fmt.Println("empty")
		}
	}
	done := make(chan struct{}, 1)
	done <- struct{}{}
	count := 0
wait:
	for {
		select {
		case <-done:
			count++
		default:
			break wait
		}
	}
	fmt.Println("done count", count)
	type job struct {
		id   int
		cost float64
	}
	jobs := make(chan job, 16)
	for i := 0; i < 10; i++ {
		jobs <- job{i, float64(i) * 1.5}
	}
	close(jobs)
	cost := 0.0
	for j := range jobs {
		if j.id%3 == 0 {
			continue
		}
		cost += j.cost
	}
	fmt.Println("cost", cost)
	var ro <-chan int = ch
	_, open := <-ro
	fmt.Println("receive-only closed", !open)
}
