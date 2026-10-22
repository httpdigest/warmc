// Generics: a binary heap, a key-value pair and a handful of reductions
// written once over constrained type parameters, and used at int, uint32,
// float64 and int64.  TinyGo does not share one body between instantiations
// the way Go's own compiler does with its dictionaries: each instantiation is
// a wasm function of its own (`main.sum[basic:int]`, `main.maxOf[basic:float64]`
// and so on), so the `<` in the heap's sift loops is `i32.lt_s`, `i32.lt_u` or
// `f64.lt` inline, with no call and no boxing.  The heap's methods and the
// generic heapsort that drives them are inlined into main once per element
// type, so the module carries three copies of the same sift-up and sift-down
// loops that differ only in the comparison and the element width.  The
// function value given to mapInto has a single caller, and LLVM folds it into
// the loop, so no `call_indirect` is left.
//
// Generic code is ordinary Go now, and this is what it turns into: the same
// compare-and-swap loop several times over at different types, plus
// append's growth path filling the heaps.
package main

type ordered interface {
	~int | ~int32 | ~int64 | ~uint32 | ~float64
}

type number interface {
	~int | ~int32 | ~int64 | ~uint32 | ~float64
}

// A binary min-heap over any ordered element type.
type heap[T ordered] struct{ data []T }

func (h *heap[T]) push(x T) {
	h.data = append(h.data, x)
	i := len(h.data) - 1
	for i > 0 {
		p := (i - 1) / 2
		if !(h.data[i] < h.data[p]) {
			break
		}
		h.data[i], h.data[p] = h.data[p], h.data[i]
		i = p
	}
}

func (h *heap[T]) pop() T {
	top := h.data[0]
	n := len(h.data) - 1
	h.data[0] = h.data[n]
	h.data = h.data[:n]
	i := 0
	for {
		l := 2*i + 1
		if l >= n {
			break
		}
		m := l
		if r := l + 1; r < n && h.data[r] < h.data[l] {
			m = r
		}
		if !(h.data[m] < h.data[i]) {
			break
		}
		h.data[i], h.data[m] = h.data[m], h.data[i]
		i = m
	}
	return top
}

func (h *heap[T]) size() int { return len(h.data) }

// A pair of any two types, and functions generic over two type parameters.
type pair[K comparable, V any] struct {
	key K
	val V
}

//go:noinline
func sum[T number](xs []T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

//go:noinline
func mapInto[T, U any](dst []U, xs []T, f func(T) U) []U {
	dst = dst[:0]
	for _, x := range xs {
		dst = append(dst, f(x))
	}
	return dst
}

//go:noinline
func maxOf[T ordered](xs []T) T {
	m := xs[0]
	for _, x := range xs[1:] {
		if x > m {
			m = x
		}
	}
	return m
}

//go:noinline
func index[K comparable, V any](ps []pair[K, V], k K) int {
	for i, p := range ps {
		if p.key == k {
			return i
		}
	}
	return -1
}

// heapsort drains a heap that was filled from xs; one generic body, three
// instantiations below.
func heapsort[T ordered](h *heap[T], xs []T, out []T) []T {
	for _, x := range xs {
		h.push(x)
	}
	out = out[:0]
	for h.size() > 0 {
		out = append(out, h.pop())
	}
	return out
}

func main() {
	const n = 1024
	ints := make([]int, n)
	u32s := make([]uint32, n)
	f64s := make([]float64, n)
	x := uint32(2463534242)
	for i := 0; i < n; i++ {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		ints[i] = int(x % 100000)
		u32s[i] = x
		f64s[i] = float64(x%10000) * 0.125
	}
	ps := make([]pair[uint32, int], 64)
	for i := range ps {
		ps[i] = pair[uint32, int]{u32s[i*7%n] & 1023, i}
	}

	hi := &heap[int]{}
	hu := &heap[uint32]{}
	hf := &heap[float64]{}
	oi := make([]int, 0, n)
	ou := make([]uint32, 0, n)
	of := make([]float64, 0, n)
	sq := make([]int64, 0, n)

	total := 0
	for r := 0; r < 950; r++ {
		ints[r&(n-1)] = r * 31
		u32s[(r*5)&(n-1)] = uint32(r) * 2654435761
		f64s[(r*3)&(n-1)] = float64(r) * 0.5

		oi = heapsort(hi, ints, oi)
		ou = heapsort(hu, u32s, ou)
		of = heapsort(hf, f64s, of)
		total += oi[0] + oi[n/2] + oi[n-1]
		total += int(ou[0]>>8) + int(ou[n-1]>>8)
		total += int(of[n/3] * 8)

		sq = mapInto(sq, ints, func(v int) int64 { return int64(v) * int64(v) })
		total += int(sum(sq) >> 20)
		total += sum(ints) + int(sum(u32s)>>4) + int(sum(f64s))
		total += maxOf(ints) + int(maxOf(u32s)>>16) + int(maxOf(f64s))
		total += index(ps, uint32(r)&1023)
	}
	println("total =", total)
}
