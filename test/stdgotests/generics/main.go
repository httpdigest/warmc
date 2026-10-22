// Generics as Go's compiler implements them -- GC-shape stenciling with a
// dictionary argument -- rather than TinyGo's full monomorphisation: generic
// functions over constraint interfaces (cmp.Ordered, ~int underlying-type
// sets, unions, method constraints, comparable), type inference from
// arguments and from function-typed parameters, generic containers with
// methods (Stack, Queue, linked List with an iterator, a binary search tree,
// an LRU cache, a Set), generic types instantiated with pointer, struct,
// interface and function type arguments that share a GC shape, a generic
// type with a method calling another generic function, generic type aliases,
// and Map/Filter/Reduce pipelines.
//
// Instantiations that share a GC shape share one compiled body and pass a
// dictionary of the type-specific operations, so a method call on a type
// parameter is an indirect call through that dictionary: many shapes are
// instantiated so the shared bodies see several dictionaries.
package main

import (
	"cmp"
	"errors"
	"fmt"
	"iter"
	"slices"
	"strconv"
	"strings"
)

type Number interface {
	~int | ~int8 | ~int16 | ~int32 | ~int64 | ~uint | ~uint8 | ~uint16 | ~uint32 | ~uint64 | ~float32 | ~float64
}

type MyInt int
type Score float32

func Sum[T Number](xs ...T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

func Map[T, U any](xs []T, f func(T) U) []U {
	out := make([]U, 0, len(xs))
	for _, x := range xs {
		out = append(out, f(x))
	}
	return out
}

func Filter[T any](xs []T, keep func(T) bool) []T {
	var out []T
	for _, x := range xs {
		if keep(x) {
			out = append(out, x)
		}
	}
	return out
}

func Reduce[T, A any](xs []T, init A, f func(A, T) A) A {
	acc := init
	for _, x := range xs {
		acc = f(acc, x)
	}
	return acc
}

func MaxOf[T cmp.Ordered](first T, rest ...T) T {
	m := first
	for _, x := range rest {
		if x > m {
			m = x
		}
	}
	return m
}

type Stringer interface{ String() string }

type StringableNumber interface {
	Number
	String() string
}

func (m MyInt) String() string { return "MyInt(" + strconv.Itoa(int(m)) + ")" }

func DescribeAll[T StringableNumber](xs []T) string {
	parts := Map(xs, func(x T) string { return x.String() })
	return strings.Join(parts, "+") + "=" + Sum(xs...).String()
}

func Keys[K comparable, V any](m map[K]V) []K {
	ks := make([]K, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	return ks
}

func IndexOf[T comparable](xs []T, x T) int {
	for i, v := range xs {
		if v == x {
			return i
		}
	}
	return -1
}

// Stack.
type Stack[T any] struct{ items []T }

func (s *Stack[T]) Push(v T) { s.items = append(s.items, v) }
func (s *Stack[T]) Pop() (T, bool) {
	var zero T
	if len(s.items) == 0 {
		return zero, false
	}
	v := s.items[len(s.items)-1]
	s.items = s.items[:len(s.items)-1]
	return v, true
}
func (s *Stack[T]) Len() int { return len(s.items) }

// Queue as a ring buffer.
type Queue[T any] struct {
	buf        []T
	head, size int
}

func NewQueue[T any](capacity int) *Queue[T] { return &Queue[T]{buf: make([]T, capacity)} }

func (q *Queue[T]) Push(v T) {
	if q.size == len(q.buf) {
		nb := make([]T, len(q.buf)*2+1)
		for i := 0; i < q.size; i++ {
			nb[i] = q.buf[(q.head+i)%len(q.buf)]
		}
		q.buf, q.head = nb, 0
	}
	q.buf[(q.head+q.size)%len(q.buf)] = v
	q.size++
}

func (q *Queue[T]) Pop() (T, error) {
	var zero T
	if q.size == 0 {
		return zero, errors.New("empty queue")
	}
	v := q.buf[q.head]
	q.buf[q.head] = zero
	q.head = (q.head + 1) % len(q.buf)
	q.size--
	return v, nil
}

// Linked list with an iterator.
type List[T any] struct {
	head, tail *elem[T]
	n          int
}

type elem[T any] struct {
	val  T
	next *elem[T]
}

func (l *List[T]) Append(vs ...T) *List[T] {
	for _, v := range vs {
		e := &elem[T]{val: v}
		if l.tail == nil {
			l.head = e
		} else {
			l.tail.next = e
		}
		l.tail = e
		l.n++
	}
	return l
}

func (l *List[T]) All() iter.Seq2[int, T] {
	return func(yield func(int, T) bool) {
		i := 0
		for e := l.head; e != nil; e = e.next {
			if !yield(i, e.val) {
				return
			}
			i++
		}
	}
}

// Binary search tree keyed by an ordered type.
type Tree[K cmp.Ordered, V any] struct {
	root *tnode[K, V]
	size int
}

type tnode[K cmp.Ordered, V any] struct {
	key         K
	val         V
	left, right *tnode[K, V]
}

func (t *Tree[K, V]) Put(k K, v V) {
	p := &t.root
	for *p != nil {
		switch c := cmp.Compare(k, (*p).key); {
		case c < 0:
			p = &(*p).left
		case c > 0:
			p = &(*p).right
		default:
			(*p).val = v
			return
		}
	}
	*p = &tnode[K, V]{key: k, val: v}
	t.size++
}

func (t *Tree[K, V]) Get(k K) (V, bool) {
	n := t.root
	for n != nil {
		switch c := cmp.Compare(k, n.key); {
		case c < 0:
			n = n.left
		case c > 0:
			n = n.right
		default:
			return n.val, true
		}
	}
	var zero V
	return zero, false
}

func (t *Tree[K, V]) InOrder(visit func(K, V)) {
	var walk func(*tnode[K, V])
	walk = func(n *tnode[K, V]) {
		if n == nil {
			return
		}
		walk(n.left)
		visit(n.key, n.val)
		walk(n.right)
	}
	walk(t.root)
}

func (t *Tree[K, V]) Height() int {
	var h func(*tnode[K, V]) int
	h = func(n *tnode[K, V]) int {
		if n == nil {
			return 0
		}
		return 1 + max(h(n.left), h(n.right))
	}
	return h(t.root)
}

// Set.
type Set[T comparable] map[T]struct{}

func SetOf[T comparable](xs ...T) Set[T] {
	s := Set[T]{}
	for _, x := range xs {
		s[x] = struct{}{}
	}
	return s
}

func (s Set[T]) Has(x T) bool { _, ok := s[x]; return ok }

func (s Set[T]) Union(o Set[T]) Set[T] {
	r := Set[T]{}
	for k := range s {
		r[k] = struct{}{}
	}
	for k := range o {
		r[k] = struct{}{}
	}
	return r
}

func (s Set[T]) Intersect(o Set[T]) Set[T] {
	r := Set[T]{}
	for k := range s {
		if o.Has(k) {
			r[k] = struct{}{}
		}
	}
	return r
}

func SortedMembers[T cmp.Ordered](s Set[T]) []T {
	out := Keys(s)
	slices.Sort(out)
	return out
}

// LRU cache.
type LRU[K comparable, V any] struct {
	cap   int
	order []K
	data  map[K]V
	evict func(K, V)
}

func NewLRU[K comparable, V any](n int, evict func(K, V)) *LRU[K, V] {
	return &LRU[K, V]{cap: n, data: map[K]V{}, evict: evict}
}

func (c *LRU[K, V]) touch(k K) {
	if i := IndexOf(c.order, k); i >= 0 {
		c.order = slices.Delete(c.order, i, i+1)
	}
	c.order = append(c.order, k)
}

func (c *LRU[K, V]) Put(k K, v V) {
	c.data[k] = v
	c.touch(k)
	if len(c.order) > c.cap {
		old := c.order[0]
		c.order = c.order[1:]
		c.evict(old, c.data[old])
		delete(c.data, old)
	}
}

func (c *LRU[K, V]) Get(k K) (V, bool) {
	v, ok := c.data[k]
	if ok {
		c.touch(k)
	}
	return v, ok
}

// Pair and generic alias.
type Pair[A, B any] struct {
	First  A
	Second B
}

func (p Pair[A, B]) Swap() Pair[B, A] { return Pair[B, A]{p.Second, p.First} }

func Zip[A, B any](as []A, bs []B) []Pair[A, B] {
	n := min(len(as), len(bs))
	out := make([]Pair[A, B], n)
	for i := 0; i < n; i++ {
		out[i] = Pair[A, B]{as[i], bs[i]}
	}
	return out
}

type IntPair = Pair[int, int]
type Named[T any] = Pair[string, T]

// Constraint with a method on a pointer receiver.
type Resetter[T any] interface {
	*T
	Reset()
}

type Counter struct{ n int }

func (c *Counter) Reset() { c.n = 0 }

func ResetAll[T any, PT Resetter[T]](xs []T) {
	for i := range xs {
		PT(&xs[i]).Reset()
	}
}

type Shape interface{ Area() float64 }
type Sq struct{ S float64 }
type Circ struct{ R float64 }

func (s Sq) Area() float64   { return s.S * s.S }
func (c Circ) Area() float64 { return 3 * c.R * c.R }

func TotalArea[S Shape](shapes []S) float64 {
	t := 0.0
	for _, s := range shapes {
		t += s.Area()
	}
	return t
}

func Memo[K comparable, V any](f func(K) V) (func(K) V, *int) {
	cache := map[K]V{}
	misses := 0
	return func(k K) V {
		if v, ok := cache[k]; ok {
			return v
		}
		misses++
		v := f(k)
		cache[k] = v
		return v
	}, &misses
}

func Ptr[T any](v T) *T { return &v }

func Zero[T any]() T { var z T; return z }

func TypeName[T any]() string { return fmt.Sprintf("%T", Zero[T]()) }

func main() {
	fmt.Println("Sum ints", Sum(1, 2, 3, 4), "floats", Sum(1.5, 2.25), "MyInt", Sum[MyInt](5, 6), "Score", Sum(Score(0.5), Score(0.25)))
	fmt.Println("Sum uint8 wraps", Sum[uint8](200, 100), "int8 wraps", Sum[int8](100, 100))
	fmt.Println("MaxOf", MaxOf(3, 9, 2), MaxOf("pear", "apple", "zoo"), MaxOf(2.5, -1), MaxOf[MyInt](7))
	fmt.Println("DescribeAll", DescribeAll([]MyInt{1, 2, 3}))
	words := []string{"alpha", "beta", "gamma", "delta", "epsilon"}
	lens := Map(words, func(s string) int { return len(s) })
	fmt.Println("Map lens", lens)
	fmt.Println("Filter long", Filter(words, func(s string) bool { return len(s) > 4 }))
	fmt.Println("Reduce concat", Reduce(words, "", func(a string, s string) string { return a + s[:1] }))
	fmt.Println("Reduce sum of lens", Reduce(lens, 0.0, func(a float64, n int) float64 { return a + float64(n)/2 }))
	fmt.Println("chain", Reduce(Filter(Map([]int{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, func(x int) int { return x * x }), func(x int) bool { return x%2 == 1 }), 0, func(a, x int) int { return a + x }))
	fmt.Println("IndexOf", IndexOf(words, "gamma"), IndexOf([]float64{1, 2}, 3), IndexOf([]any{1, "a", nil}, nil))

	// Stack of several types.
	var si Stack[int]
	var ss Stack[string]
	var sp Stack[*Counter]
	var sf Stack[func() string]
	for i := 0; i < 5; i++ {
		si.Push(i * i)
		ss.Push(strconv.Itoa(i))
		sp.Push(&Counter{i})
		sf.Push(func() string { return fmt.Sprint("fn", i) })
	}
	for si.Len() > 0 {
		a, _ := si.Pop()
		b, _ := ss.Pop()
		c, _ := sp.Pop()
		d, _ := sf.Pop()
		fmt.Println("pop", a, b, c.n, d())
	}
	_, ok := si.Pop()
	fmt.Println("pop empty ok:", ok)

	// Queue growth.
	q := NewQueue[Pair[string, int]](2)
	for i := 0; i < 7; i++ {
		q.Push(Pair[string, int]{fmt.Sprint("job", i), i})
		if i%3 == 2 {
			v, _ := q.Pop()
			fmt.Println("queue pop", v.First, v.Second)
		}
	}
	for {
		v, err := q.Pop()
		if err != nil {
			fmt.Println("queue:", err)
			break
		}
		fmt.Printf("queue drain %+v\n", v)
	}

	// List iterator with early exit.
	l := (&List[float64]{}).Append(1.5, 2.5, 3.5, 4.5, 5.5)
	for i, v := range l.All() {
		if i == 3 {
			break
		}
		fmt.Println("list", i, v)
	}
	ls := (&List[[]string]{}).Append([]string{"a"}, nil, []string{"b", "c"})
	for i, v := range ls.All() {
		fmt.Println("list of slices", i, v, len(v))
	}

	// Tree.
	t := &Tree[string, int]{}
	for i, w := range strings.Fields("the quick brown fox jumps over the lazy dog and the end") {
		old, _ := t.Get(w)
		t.Put(w, old+i)
	}
	t.InOrder(func(k string, v int) { fmt.Printf("tree %s=%d\n", k, v) })
	fmt.Println("tree size", t.size, "height", t.Height())
	ti := &Tree[float64, []int]{}
	for i := 0; i < 20; i++ {
		k := float64((i * 7) % 11)
		v, _ := ti.Get(k)
		ti.Put(k, append(v, i))
	}
	ti.InOrder(func(k float64, v []int) { fmt.Println("float tree", k, v) })

	// Sets.
	a := SetOf(1, 2, 3, 4, 5)
	b := SetOf(4, 5, 6, 7)
	fmt.Println("union", SortedMembers(a.Union(b)), "intersect", SortedMembers(a.Intersect(b)))
	sa := SetOf(strings.Split("a b c a b", " ")...)
	fmt.Println("string set", SortedMembers(sa), sa.Has("c"), sa.Has("z"))
	type pt struct{ X, Y int }
	ps := SetOf(pt{1, 2}, pt{1, 2}, pt{2, 1})
	fmt.Println("struct set size", len(ps), ps.Has(pt{2, 1}))

	// LRU.
	var evicted []string
	c := NewLRU(3, func(k string, v int) { evicted = append(evicted, fmt.Sprintf("%s:%d", k, v)) })
	for i, k := range []string{"a", "b", "c", "a", "d", "b", "e", "a", "f"} {
		if _, ok := c.Get(k); !ok {
			c.Put(k, i)
		}
	}
	fmt.Println("LRU order", c.order, "evicted", evicted)

	// Pairs, Zip, Swap, aliases.
	z := Zip([]string{"x", "y", "z"}, []float64{1.5, 2.5})
	fmt.Printf("zip %+v swapped %+v\n", z, z[1].Swap())
	ip := IntPair{1, 2}
	nm := Named[[]int]{"nums", []int{1, 2, 3}}
	fmt.Printf("alias %+v %T | %+v %T\n", ip, ip, nm, nm)

	// Pointer-method constraint.
	cs := []Counter{{1}, {2}, {3}}
	ResetAll(cs)
	fmt.Println("reset all", cs)

	// Interface constraints with concrete types.
	fmt.Println("areas", TotalArea([]Sq{{1}, {2}}), TotalArea([]Circ{{1}, {2}}), TotalArea([]Shape{Sq{3}, Circ{1}}))

	// Memoised recursive function.
	var fib func(int) int
	fibm, misses := Memo(func(n int) int {
		if n < 2 {
			return n
		}
		return fib(n-1) + fib(n-2)
	})
	fib = fibm
	fmt.Println("memo fib(80)", fib(80), "misses", *misses)
	lenm, lm := Memo(func(s string) int { return len(s) })
	lenm("a")
	lenm("a")
	lenm("bb")
	fmt.Println("memo len misses", *lm)

	// Type names of instantiations.
	fmt.Println(TypeName[int](), TypeName[Pair[string, []int]](), TypeName[*Stack[MyInt]](), TypeName[map[string]Set[int]](), TypeName[Tree[int, string]]())
	fmt.Println("Ptr", *Ptr(42), *Ptr("s"), Ptr(Pair[int, int]{}).First)
	fmt.Printf("Zero %v %q %v %v\n", Zero[int](), Zero[string](), Zero[*int]() == nil, Zero[[]int]() == nil)
}
