// Generics.  Type parameters with the constraints Go offers: any,
// comparable, cmp.Ordered, unions with ~ so that named types qualify, and
// interfaces that require methods.  Generic functions (Map, Filter, Reduce,
// Keys, Max, Sum, Index, Uniq), generic types with methods (Stack, Queue as a
// ring buffer, an ordered binary search tree, a binary heap with a
// caller-supplied less, Set, Pair, an LRU list), type inference from
// arguments, explicit instantiation stored in a variable, zero values of a
// type parameter, and %T of instantiated types.  TinyGo instantiates every
// combination separately, so the same source becomes many functions that
// differ only in widths and calling conventions.
package main

import (
	"cmp"
	"fmt"
	"os"
	"strings"
)

type Number interface {
	~int | ~int8 | ~int16 | ~int32 | ~int64 | ~uint8 | ~uint32 | ~uint64 |
		~float32 | ~float64
}

type Celsius float64
type ID int16

type Stringer interface{ String() string }

func (c Celsius) String() string { return fmt.Sprintf("%.1fC", float64(c)) }
func (i ID) String() string      { return fmt.Sprintf("#%d", int(i)) }

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
	for _, x := range xs {
		init = f(init, x)
	}
	return init
}

func Sum[T Number](xs []T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

func MaxOf[T cmp.Ordered](xs ...T) (T, bool) {
	var zero T
	if len(xs) == 0 {
		return zero, false
	}
	m := xs[0]
	for _, x := range xs[1:] {
		if x > m {
			m = x
		}
	}
	return m, true
}

func Index[T comparable](xs []T, v T) int {
	for i, x := range xs {
		if x == v {
			return i
		}
	}
	return -1
}

func Uniq[T comparable](xs []T) []T {
	seen := map[T]bool{}
	var out []T
	for _, x := range xs {
		if !seen[x] {
			seen[x] = true
			out = append(out, x)
		}
	}
	return out
}

func JoinAll[T Stringer](xs []T, sep string) string {
	parts := make([]string, len(xs))
	for i, x := range xs {
		parts[i] = x.String()
	}
	return strings.Join(parts, sep)
}

type Pair[K comparable, V any] struct {
	Key K
	Val V
}

func (p Pair[K, V]) String() string { return fmt.Sprintf("%v=>%v", p.Key, p.Val) }

func MakePair[K comparable, V any](k K, v V) Pair[K, V] { return Pair[K, V]{k, v} }

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

// Queue is a ring buffer that doubles when full.
type Queue[T any] struct {
	buf        []T
	head, size int
}

func (q *Queue[T]) Push(v T) {
	if q.size == len(q.buf) {
		nb := make([]T, max(4, 2*len(q.buf)))
		for i := 0; i < q.size; i++ {
			nb[i] = q.buf[(q.head+i)%len(q.buf)]
		}
		q.buf, q.head = nb, 0
	}
	q.buf[(q.head+q.size)%len(q.buf)] = v
	q.size++
}

func (q *Queue[T]) Pop() (T, bool) {
	var zero T
	if q.size == 0 {
		return zero, false
	}
	v := q.buf[q.head]
	q.buf[q.head] = zero
	q.head = (q.head + 1) % len(q.buf)
	q.size--
	return v, true
}

type Tree[K cmp.Ordered, V any] struct {
	root *node[K, V]
	n    int
}

type node[K cmp.Ordered, V any] struct {
	k           K
	v           V
	left, right *node[K, V]
}

func (t *Tree[K, V]) Put(k K, v V) {
	p := &t.root
	for *p != nil {
		switch c := cmp.Compare(k, (*p).k); {
		case c < 0:
			p = &(*p).left
		case c > 0:
			p = &(*p).right
		default:
			(*p).v = v
			return
		}
	}
	*p = &node[K, V]{k: k, v: v}
	t.n++
}

func (t *Tree[K, V]) Get(k K) (V, bool) {
	n := t.root
	for n != nil {
		if k < n.k {
			n = n.left
		} else if k > n.k {
			n = n.right
		} else {
			return n.v, true
		}
	}
	var zero V
	return zero, false
}

func (t *Tree[K, V]) Walk(f func(K, V)) {
	var rec func(*node[K, V])
	rec = func(n *node[K, V]) {
		if n == nil {
			return
		}
		rec(n.left)
		f(n.k, n.v)
		rec(n.right)
	}
	rec(t.root)
}

func (t *Tree[K, V]) Height() int {
	var h func(*node[K, V]) int
	h = func(n *node[K, V]) int {
		if n == nil {
			return 0
		}
		return 1 + max(h(n.left), h(n.right))
	}
	return h(t.root)
}

type Heap[T any] struct {
	data []T
	less func(a, b T) bool
}

func (h *Heap[T]) Push(v T) {
	h.data = append(h.data, v)
	i := len(h.data) - 1
	for i > 0 {
		p := (i - 1) / 2
		if !h.less(h.data[i], h.data[p]) {
			break
		}
		h.data[i], h.data[p] = h.data[p], h.data[i]
		i = p
	}
}

func (h *Heap[T]) Pop() T {
	top := h.data[0]
	last := len(h.data) - 1
	h.data[0] = h.data[last]
	h.data = h.data[:last]
	i := 0
	for {
		l, r, m := 2*i+1, 2*i+2, i
		if l < len(h.data) && h.less(h.data[l], h.data[m]) {
			m = l
		}
		if r < len(h.data) && h.less(h.data[r], h.data[m]) {
			m = r
		}
		if m == i {
			return top
		}
		h.data[i], h.data[m] = h.data[m], h.data[i]
		i = m
	}
}

type Set[T comparable] map[T]struct{}

func (s Set[T]) Add(vs ...T) {
	for _, v := range vs {
		s[v] = struct{}{}
	}
}
func (s Set[T]) Has(v T) bool { _, ok := s[v]; return ok }
func Intersect[T comparable](a, b Set[T]) Set[T] {
	out := Set[T]{}
	for k := range a {
		if b.Has(k) {
			out.Add(k)
		}
	}
	return out
}

func SortedKeys[T cmp.Ordered](s Set[T]) []T {
	var t Tree[T, struct{}]
	for k := range s {
		t.Put(k, struct{}{})
	}
	var out []T
	t.Walk(func(k T, _ struct{}) { out = append(out, k) })
	return out
}

type List[T any] struct {
	head *elem[T]
	size int
}

type elem[T any] struct {
	val  T
	next *elem[T]
}

func (l *List[T]) PushFront(v T) { l.head = &elem[T]{v, l.head}; l.size++ }
func (l *List[T]) Reverse() {
	var prev *elem[T]
	for cur := l.head; cur != nil; {
		next := cur.next
		cur.next = prev
		prev, cur = cur, next
	}
	l.head = prev
}
func (l *List[T]) All() []T {
	var out []T
	for e := l.head; e != nil; e = e.next {
		out = append(out, e.val)
	}
	return out
}

var scale = 1

func init() {
	if len(os.Args) > 1000 {
		scale = 2
	}
}

func main() {
	fmt.Println("-- functions")
	ints := []int{5, 3, 8, 1, 9, 2, 7, 3, 5, 5 * scale}
	floats := []float64{1.5, -2.25, 3.125}
	temps := []Celsius{21.5, 19, 30.25}
	ids := []ID{3, 1, 2}
	bytes8 := []uint8{200, 100, 50}
	fmt.Println(Map(ints, func(x int) string { return strings.Repeat("|", x%4) }))
	fmt.Println(Map(temps, func(c Celsius) float64 { return float64(c)*9/5 + 32 }))
	fmt.Println(Filter(ints, func(x int) bool { return x > 4 }), Filter(ids, func(i ID) bool { return i > 1 }))
	fmt.Println(Reduce(ints, "", func(a string, x int) string { return a + fmt.Sprint(x) }))
	fmt.Println(Sum(ints), Sum(floats), Sum(temps), Sum(ids), Sum(bytes8), Sum([]int8{100, 100}))
	m1, ok1 := MaxOf(ints...)
	m2, ok2 := MaxOf("pear", "apple", "zoo", "fig")
	m3, ok3 := MaxOf[float64]()
	m4, _ := MaxOf(temps...)
	fmt.Println(m1, ok1, m2, ok2, m3, ok3, m4)
	fmt.Println(Index(ints, 9), Index([]string{"a", "b"}, "b"), Index(temps, 30.25), Index(ids, 7))
	fmt.Println(Uniq(ints), Uniq(strings.Split("a b a c b d", " ")))
	fmt.Println(JoinAll(temps, ", "), JoinAll(ids, "/"))
	explicit := Map[int, Pair[int, bool]]
	fmt.Println(explicit(ints[:4], func(x int) Pair[int, bool] { return MakePair(x, x%2 == 0) }))
	fmt.Println("-- types")
	p := MakePair("k", []int{1, 2})
	q := MakePair(ID(4), Celsius(-1))
	fmt.Printf("%v %v %T %T\n", p, q, p, q)
	var st Stack[string]
	for _, w := range strings.Fields("to be or not to be") {
		st.Push(w)
	}
	for st.Len() > 0 {
		v, _ := st.Pop()
		fmt.Print(v, " ")
	}
	_, ok := st.Pop()
	fmt.Println(ok)
	var ps Stack[Pair[string, int]]
	ps.Push(MakePair("x", 1))
	ps.Push(MakePair("y", 2))
	top, _ := ps.Pop()
	fmt.Println(top, ps.Len())
	var qu Queue[int]
	out := []int{}
	for i := 0; i < 40; i++ {
		qu.Push(i * scale)
		if i%3 == 2 {
			v, _ := qu.Pop()
			out = append(out, v)
		}
	}
	for {
		v, ok := qu.Pop()
		if !ok {
			break
		}
		out = append(out, v)
	}
	fmt.Println(out, len(qu.buf))
	var tr Tree[string, int]
	for i, w := range strings.Fields("m d s a f p x b e g n q y c") {
		tr.Put(w, i)
	}
	tr.Put("d", 100)
	tr.Walk(func(k string, v int) { fmt.Print(k, ":", v, " ") })
	fmt.Println(tr.n, tr.Height())
	v, found := tr.Get("p")
	v2, found2 := tr.Get("zz")
	fmt.Println(v, found, v2, found2)
	var nt Tree[float64, []string]
	for i := 0; i < 30; i++ {
		k := float64((i*7)%30) / 4
		got, _ := nt.Get(k)
		nt.Put(k, append(got, fmt.Sprint(i)))
	}
	fmt.Println(nt.n, nt.Height())
	nt.Walk(func(k float64, v []string) {
		if k < 2 {
			fmt.Print(k, v, " ")
		}
	})
	fmt.Println()
	h := &Heap[Pair[string, int]]{less: func(a, b Pair[string, int]) bool {
		if a.Val != b.Val {
			return a.Val < b.Val
		}
		return a.Key < b.Key
	}}
	for i, w := range strings.Fields("delta alpha echo bravo charlie foxtrot golf hotel") {
		h.Push(MakePair(w, (i*5)%4))
	}
	for len(h.data) > 0 {
		fmt.Print(h.Pop(), " ")
	}
	fmt.Println()
	ih := &Heap[int]{less: func(a, b int) bool { return a > b }}
	for i := 0; i < 50; i++ {
		ih.Push((i * 37) % 101)
	}
	for i := 0; i < 10; i++ {
		fmt.Print(ih.Pop(), " ")
	}
	fmt.Println()
	a, b := Set[int]{}, Set[int]{}
	for i := 0; i < 40; i++ {
		a.Add(i * 2)
		b.Add(i * 3)
	}
	fmt.Println(SortedKeys(Intersect(a, b)), len(a), a.Has(10), a.Has(11))
	ws := Set[string]{}
	ws.Add("x", "y", "x", "z")
	fmt.Println(SortedKeys(ws))
	var l List[Celsius]
	for _, t := range temps {
		l.PushFront(t)
	}
	fmt.Println(l.All(), l.size)
	l.Reverse()
	fmt.Println(l.All())
	fmt.Printf("%T %T %T\n", st, tr, l)
}
