// Range-over-func iterators (Go 1.23+): iter.Seq and iter.Seq2 producers,
// early break / return / continue / goto out of a range-over-func loop
// (which the compiler rewrites into a closure plus a state variable and
// checks that the iterator honours the yield result), defer inside the loop
// body (deferred to the enclosing function, not the closure), panics crossing
// the iterator and recovered outside, nested iterators, adapters (Map,
// Filter, Take, Zip, Chain, Enumerate, Window) composed lazily, iter.Pull and
// iter.Pull2 with early stop, infinite generators, tree walks, slices/maps/
// strings iterator helpers, and range over integers.
//
// iter.Pull is implemented with coroutines in the runtime (coroswitch), which
// switch goroutine stacks without the scheduler -- a distinct control
// transfer from both goroutine switches and panics.
package main

import (
	"fmt"
	"iter"
	"maps"
	"slices"
	"strings"
)

func Count(start, step int) iter.Seq[int] {
	return func(yield func(int) bool) {
		for i := start; ; i += step {
			if !yield(i) {
				return
			}
		}
	}
}

func Map[T, U any](s iter.Seq[T], f func(T) U) iter.Seq[U] {
	return func(yield func(U) bool) {
		for v := range s {
			if !yield(f(v)) {
				return
			}
		}
	}
}

func Filter[T any](s iter.Seq[T], keep func(T) bool) iter.Seq[T] {
	return func(yield func(T) bool) {
		for v := range s {
			if keep(v) && !yield(v) {
				return
			}
		}
	}
}

func Take[T any](s iter.Seq[T], n int) iter.Seq[T] {
	return func(yield func(T) bool) {
		if n <= 0 {
			return
		}
		i := 0
		for v := range s {
			if !yield(v) {
				return
			}
			i++
			if i == n {
				return
			}
		}
	}
}

func Enumerate[T any](s iter.Seq[T]) iter.Seq2[int, T] {
	return func(yield func(int, T) bool) {
		i := 0
		for v := range s {
			if !yield(i, v) {
				return
			}
			i++
		}
	}
}

func Zip[A, B any](a iter.Seq[A], b iter.Seq[B]) iter.Seq2[A, B] {
	return func(yield func(A, B) bool) {
		nextB, stopB := iter.Pull(b)
		defer stopB()
		for va := range a {
			vb, ok := nextB()
			if !ok || !yield(va, vb) {
				return
			}
		}
	}
}

func Chain[T any](seqs ...iter.Seq[T]) iter.Seq[T] {
	return func(yield func(T) bool) {
		for _, s := range seqs {
			for v := range s {
				if !yield(v) {
					return
				}
			}
		}
	}
}

func Window[T any](s iter.Seq[T], n int) iter.Seq[[]T] {
	return func(yield func([]T) bool) {
		var buf []T
		for v := range s {
			buf = append(buf, v)
			if len(buf) > n {
				buf = buf[1:]
			}
			if len(buf) == n && !yield(slices.Clone(buf)) {
				return
			}
		}
	}
}

type Tree struct {
	Left, Right *Tree
	Val         int
}

func (t *Tree) Insert(v int) *Tree {
	if t == nil {
		return &Tree{Val: v}
	}
	if v < t.Val {
		t.Left = t.Left.Insert(v)
	} else {
		t.Right = t.Right.Insert(v)
	}
	return t
}

func (t *Tree) All() iter.Seq[int] {
	return func(yield func(int) bool) { t.walk(yield) }
}

func (t *Tree) walk(yield func(int) bool) bool {
	if t == nil {
		return true
	}
	return t.Left.walk(yield) && yield(t.Val) && t.Right.walk(yield)
}

var log []string

func trace(f string, a ...any) { log = append(log, fmt.Sprintf(f, a...)) }

// Iterator that records its lifecycle.
func Traced(name string, n int) iter.Seq[int] {
	return func(yield func(int) bool) {
		trace("%s start", name)
		defer trace("%s cleanup", name)
		for i := 0; i < n; i++ {
			if !yield(i) {
				trace("%s stopped at %d", name, i)
				return
			}
		}
		trace("%s exhausted", name)
	}
}

func findFirst(seq iter.Seq[int], pred func(int) bool) (int, bool) {
	for v := range seq {
		if pred(v) {
			return v, true // return from inside range-over-func
		}
	}
	return 0, false
}

func deferInLoop() (out []string) {
	defer func() { out = append(out, "function returns") }()
	for v := range Traced("defer-src", 3) {
		defer func() { out = append(out, fmt.Sprint("deferred from body ", v)) }()
	}
	out = append(out, "loop done")
	return out
}

func panicThrough() (msg string) {
	defer func() {
		if r := recover(); r != nil {
			msg = fmt.Sprint("recovered: ", r)
		}
	}()
	for v := range Traced("panic-src", 10) {
		if v == 2 {
			panic(fmt.Sprintf("boom at %d", v))
		}
	}
	return "no panic"
}

func gotoOut() string {
	for v := range Traced("goto-src", 10) {
		for w := range Traced("goto-inner", 10) {
			if v*w == 6 {
				goto found
			}
		}
	}
	return "not found"
found:
	return "found via goto"
}

func labeled() []string {
	var out []string
outer:
	for a := range Count(1, 1) {
		for b := range Count(1, 1) {
			if b > a {
				continue outer
			}
			if a*b > 12 {
				break outer
			}
			out = append(out, fmt.Sprintf("%dx%d", a, b))
		}
	}
	return out
}

func main() {
	// Lazy pipeline over an infinite generator.
	evensSquared := Take(Map(Filter(Count(1, 1), func(x int) bool { return x%2 == 0 }), func(x int) int { return x * x }), 8)
	fmt.Println("evens squared", slices.Collect(evensSquared))
	fmt.Println("again (re-iterable)", slices.Collect(evensSquared))
	for i, s := range Enumerate(Map(Take(Count(10, -3), 5), func(x int) string { return fmt.Sprintf("<%d>", x) })) {
		fmt.Println("enumerate", i, s)
	}
	for a, b := range Zip(slices.Values([]string{"a", "b", "c", "d"}), Count(100, 100)) {
		fmt.Println("zip", a, b)
	}
	fmt.Println("chain", slices.Collect(Chain(slices.Values([]int{1, 2}), Take(Count(7, 7), 3), slices.Values([]int(nil)), slices.Values([]int{0}))))
	for w := range Take(Window(Count(1, 1), 3), 4) {
		fmt.Println("window", w)
	}

	// Lifecycle with break.
	for v := range Traced("A", 5) {
		if v == 2 {
			break
		}
		trace("body A %d", v)
	}
	for v := range Traced("B", 3) {
		trace("body B %d", v)
	}
	for v := range Traced("C", 5) {
		if v%2 == 0 {
			continue
		}
		trace("body C %d", v)
	}
	v, ok := findFirst(Traced("D", 10), func(x int) bool { return x*x > 10 })
	trace("findFirst -> %d %v", v, ok)
	fmt.Println("lifecycle:")
	for _, l := range log {
		fmt.Println("  ", l)
	}
	log = nil

	fmt.Println("defer in loop:", deferInLoop())
	fmt.Println("panic through iterator:", panicThrough())
	fmt.Println("goto:", gotoOut())
	fmt.Println("trace:", strings.Join(log, "; "))
	log = nil
	fmt.Println("labeled:", labeled())

	// An iterator that ignores yield's false result: the runtime panics.
	bad := func(yield func(int) bool) {
		yield(1)
		yield(2)
	}
	func() {
		defer func() { fmt.Println("misbehaving iterator:", recover()) }()
		for x := range bad {
			fmt.Println("bad yielded", x)
			break
		}
	}()

	// Pull.
	next, stop := iter.Pull(Traced("P", 5))
	for i := 0; i < 3; i++ {
		v, ok := next()
		fmt.Println("pull", v, ok)
	}
	stop()
	v2, ok2 := next()
	fmt.Println("after stop", v2, ok2)
	fmt.Println("pull trace:", strings.Join(log, "; "))
	log = nil
	next2, stop2 := iter.Pull2(maps.All(map[string]int{"only": 1}))
	k, val, ok3 := next2()
	_, _, ok4 := next2()
	stop2()
	fmt.Println("pull2", k, val, ok3, ok4)

	// Merge two sorted sequences using two Pulls.
	merge := func(a, b iter.Seq[int]) iter.Seq[int] {
		return func(yield func(int) bool) {
			na, sa := iter.Pull(a)
			defer sa()
			nb, sb := iter.Pull(b)
			defer sb()
			va, oka := na()
			vb, okb := nb()
			for oka || okb {
				if !okb || (oka && va <= vb) {
					if !yield(va) {
						return
					}
					va, oka = na()
				} else {
					if !yield(vb) {
						return
					}
					vb, okb = nb()
				}
			}
		}
	}
	fmt.Println("pull merge", slices.Collect(merge(slices.Values([]int{1, 4, 6, 9}), Take(Count(0, 3), 6))))

	// Tree walk with early stop.
	var root *Tree
	for _, x := range []int{50, 30, 70, 20, 40, 60, 80, 35, 45, 65} {
		root = root.Insert(x)
	}
	fmt.Println("tree in order", slices.Collect(root.All()))
	var firstBig []int
	for x := range root.All() {
		if x > 60 {
			break
		}
		firstBig = append(firstBig, x)
	}
	fmt.Println("tree until >60", firstBig)

	// Standard library iterator helpers.
	m := map[string]int{"c": 3, "a": 1, "b": 2}
	for _, k := range slices.Sorted(maps.Keys(m)) {
		fmt.Println("sorted key", k, m[k])
	}
	fmt.Println("sorted values", slices.Sorted(maps.Values(m)))
	for i, s := range slices.All([]string{"x", "y"}) {
		fmt.Println("slices.All", i, s)
	}
	fmt.Println("SortedFunc", slices.SortedFunc(slices.Values([]string{"bb", "a", "ccc"}), func(a, b string) int { return len(b) - len(a) }))
	fmt.Println("SortedStableFunc", slices.SortedStableFunc(slices.Values([]string{"b2", "a1", "b1", "a2"}), func(a, b string) int { return int(a[0]) - int(b[0]) }))
	var words []string
	for w := range strings.FieldsFuncSeq("a,b;;c", func(r rune) bool { return r == ',' || r == ';' }) {
		words = append(words, w)
	}
	fmt.Println("FieldsFuncSeq", words)
	var sa []string
	for p := range strings.SplitAfterSeq("1-2-3", "-") {
		sa = append(sa, p)
	}
	fmt.Println("SplitAfterSeq", sa)

	// Range over ints and functions of various arities.
	total := 0
	for i := range 10 {
		total += i
	}
	for range 3 {
		total *= 2
	}
	fmt.Println("range int total", total)
	var zero func(func() bool) = func(yield func() bool) {
		for i := 0; i < 4; i++ {
			if !yield() {
				return
			}
		}
	}
	n := 0
	for range zero {
		n++
	}
	fmt.Println("zero-arg iterator ran", n)
	type pair struct{ k, v string }
	pairs := func(yield func(string, string) bool) {
		for _, p := range []pair{{"k1", "v1"}, {"k2", "v2"}} {
			if !yield(p.k, p.v) {
				return
			}
		}
	}
	for k := range pairs {
		fmt.Println("key only from Seq2", k)
	}

	// Collatz lengths with a generator per start.
	collatz := func(n int) iter.Seq[int] {
		return func(yield func(int) bool) {
			for n != 1 {
				if !yield(n) {
					return
				}
				if n%2 == 0 {
					n /= 2
				} else {
					n = 3*n + 1
				}
			}
			yield(1)
		}
	}
	best, bestLen := 0, 0
	for s := range Take(Count(1, 1), 300) {
		l := 0
		for range collatz(s) {
			l++
		}
		if l > bestLen {
			best, bestLen = s, l
		}
	}
	fmt.Println("longest collatz under 300:", best, bestLen, slices.Collect(Take(collatz(27), 12)))
}
