// Go maps (Swiss tables since Go 1.24) checked against a reference: a long
// random sequence of inserts, overwrites, deletes and lookups is applied both
// to a built-in map and to a sorted-slice reference implementation, and the
// two are compared after every phase, over key types that take different
// hashing and comparison paths (int64, int32, string, a struct of mixed
// fields, [2]uint64, float64, interface{}); plus deleting during iteration
// (deleted entries must not be produced later), inserting during iteration
// (the iteration must still terminate and see every pre-existing key once),
// maps growing through several table splits, clear, len after every
// operation, maps of maps, and the comma-ok forms.
//
// Iteration order is randomised, so every iteration result is sorted or
// reduced to an order-independent value before it is printed.
package main

import (
	"fmt"
	"math"
	"sort"
	"strings"
)

type rng struct{ s uint64 }

func (r *rng) next() uint64 {
	r.s ^= r.s << 13
	r.s ^= r.s >> 7
	r.s ^= r.s << 17
	return r.s
}

type skey struct {
	A int8
	B string
	C uint32
}

// Sorted-slice reference map.
type ref[K comparable, V any] struct {
	keys []K
	vals []V
	less func(a, b K) bool
}

func (r *ref[K, V]) find(k K) (int, bool) {
	i := sort.Search(len(r.keys), func(i int) bool { return !r.less(r.keys[i], k) })
	return i, i < len(r.keys) && r.keys[i] == k
}

func (r *ref[K, V]) set(k K, v V) {
	i, ok := r.find(k)
	if ok {
		r.vals[i] = v
		return
	}
	r.keys = append(r.keys, k)
	r.vals = append(r.vals, v)
	copy(r.keys[i+1:], r.keys[i:])
	copy(r.vals[i+1:], r.vals[i:])
	r.keys[i] = k
	r.vals[i] = v
}

func (r *ref[K, V]) del(k K) {
	if i, ok := r.find(k); ok {
		r.keys = append(r.keys[:i], r.keys[i+1:]...)
		r.vals = append(r.vals[:i], r.vals[i+1:]...)
	}
}

func (r *ref[K, V]) get(k K) (V, bool) {
	if i, ok := r.find(k); ok {
		return r.vals[i], true
	}
	var z V
	return z, false
}

func run[K comparable](name string, mk func(uint64) K, less func(a, b K) bool, ops int, keyspace uint64) {
	r := &rng{s: 0x9E3779B97F4A7C15 ^ uint64(len(name))}
	m := map[K]int{}
	rf := &ref[K, int]{less: less}
	mismatches := 0
	hits, misses := 0, 0
	for i := 0; i < ops; i++ {
		x := r.next()
		k := mk(x % keyspace)
		switch (x >> 40) % 10 {
		case 0, 1, 2, 3:
			m[k] = i
			rf.set(k, i)
		case 4, 5:
			delete(m, k)
			rf.del(k)
		case 6:
			m[k] += 7
			v, _ := rf.get(k)
			rf.set(k, v+7)
		default:
			v1, ok1 := m[k]
			v2, ok2 := rf.get(k)
			if v1 != v2 || ok1 != ok2 {
				mismatches++
			}
			if ok1 {
				hits++
			} else {
				misses++
			}
		}
		if len(m) != len(rf.keys) {
			mismatches++
		}
		if i%(ops/4) == ops/4-1 {
			// Full comparison.
			full := 0
			for k, v := range m {
				if rv, ok := rf.get(k); !ok || rv != v {
					full++
				}
			}
			sum := 0
			for _, v := range rf.vals {
				sum += v
			}
			fmt.Printf("%-8s after %6d ops: len=%5d mismatches=%d full-diff=%d hits=%d misses=%d valsum=%d\n", name, i+1, len(m), mismatches, full, hits, misses, sum)
		}
	}
}

func main() {
	run("int64", func(x uint64) int64 { return int64(x) - 500 }, func(a, b int64) bool { return a < b }, 20000, 1000)
	run("int32", func(x uint64) int32 { return int32(x * 2654435761) }, func(a, b int32) bool { return a < b }, 20000, 3000)
	run("string", func(x uint64) string { return fmt.Sprintf("k%x-%s", x, strings.Repeat("z", int(x%5))) }, func(a, b string) bool { return a < b }, 20000, 800)
	run("struct", func(x uint64) skey { return skey{int8(x), fmt.Sprint(x % 7), uint32(x / 7)} }, func(a, b skey) bool {
		if a.A != b.A {
			return a.A < b.A
		}
		if a.B != b.B {
			return a.B < b.B
		}
		return a.C < b.C
	}, 20000, 900)
	run("[2]u64", func(x uint64) [2]uint64 { return [2]uint64{x >> 3, x & 7} }, func(a, b [2]uint64) bool {
		return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1])
	}, 20000, 1200)
	run("float64", func(x uint64) float64 { return float64(x)/8 - 50 }, func(a, b float64) bool { return a < b }, 20000, 1000)
	run("iface", func(x uint64) any {
		switch x % 3 {
		case 0:
			return int(x)
		case 1:
			return fmt.Sprint(x)
		}
		return float64(x)
	}, func(a, b any) bool { return fmt.Sprintf("%T%v", a, a) < fmt.Sprintf("%T%v", b, b) }, 6000, 600)

	// Delete during iteration: deleted entries must not be produced.
	m := map[int]int{}
	for i := 0; i < 1000; i++ {
		m[i] = i * i
	}
	seen := map[int]bool{}
	deleted := map[int]bool{}
	bad := 0
	for k := range m {
		if deleted[k] {
			bad++
		}
		seen[k] = true
		// Delete some other keys that may or may not have been visited yet.
		for _, d := range []int{(k * 7) % 1000, (k + 500) % 1000} {
			if !seen[d] {
				delete(m, d)
				deleted[d] = true
			}
		}
	}
	fmt.Println("delete during iteration: produced-after-delete", bad, "seen+deleted covers all:", len(seen)+len(deleted) >= 1000, "remaining", len(m) == len(seen)-countDeletedSeen(seen, deleted))

	// Insert during iteration: terminates, every original key seen exactly once.
	m2 := map[int]bool{}
	for i := 0; i < 200; i++ {
		m2[i] = true
	}
	counts := map[int]int{}
	iters := 0
	for k := range m2 {
		counts[k]++
		iters++
		if k < 200 {
			m2[k+10000] = true
		}
	}
	once := true
	for i := 0; i < 200; i++ {
		if counts[i] != 1 {
			once = false
		}
	}
	fmt.Println("insert during iteration: originals once", once, "len", len(m2), "iterations in range", iters >= 200 && iters <= 400)

	// Growth through splits, then shrink by deletion, then clear.
	g := map[string][]int{}
	var lens []int
	for i := 0; i < 5000; i++ {
		k := fmt.Sprintf("key-%05d", i*7919%10007)
		g[k] = append(g[k], i)
		if i&(i-1) == 0 {
			lens = append(lens, len(g))
		}
	}
	fmt.Println("growth lens at powers of two", lens)
	for i := 0; i < 5000; i += 2 {
		delete(g, fmt.Sprintf("key-%05d", i*7919%10007))
	}
	total := 0
	for _, v := range g {
		total += v[0]
	}
	fmt.Println("after deleting evens: len", len(g), "sum", total)
	clear(g)
	g["again"] = []int{1}
	fmt.Println("after clear", len(g), g["again"])

	// Maps of maps.
	mm := map[string]map[string]int{}
	words := strings.Fields("a b c a b a d e d a b c f g a")
	for i, w := range words {
		inner, ok := mm[w]
		if !ok {
			inner = map[string]int{}
			mm[w] = inner
		}
		if i+1 < len(words) {
			inner[words[i+1]]++
		}
	}
	outer := make([]string, 0, len(mm))
	for k := range mm {
		outer = append(outer, k)
	}
	sort.Strings(outer)
	for _, k := range outer {
		inner := make([]string, 0)
		for k2, c := range mm[k] {
			inner = append(inner, fmt.Sprintf("%s:%d", k2, c))
		}
		sort.Strings(inner)
		fmt.Println("bigram", k, "->", inner)
	}

	// Special float keys.
	fk := map[float64]int{}
	for i := 0; i < 5; i++ {
		fk[math.NaN()] = i
	}
	fk[math.Inf(1)] = 1
	fk[math.Inf(-1)] = 2
	fk[0] = 3
	fk[math.Copysign(0, -1)] = 4
	nanCount := 0
	for k := range fk {
		if k != k {
			nanCount++
		}
	}
	_, nanOk := fk[math.NaN()]
	fmt.Println("float keys len", len(fk), "NaN entries", nanCount, "NaN lookup ok", nanOk, "zero", fk[0])
	delete(fk, math.NaN())
	fmt.Println("delete NaN is no-op, len", len(fk))
	clear(fk)
	fmt.Println("clear removes NaNs, len", len(fk))

	// Order-independent fingerprint of a large map built two ways.
	a := map[uint32]uint32{}
	b := map[uint32]uint32{}
	for i := uint32(0); i < 4000; i++ {
		a[i*2654435761] = i
	}
	for i := uint32(3999); ; i-- {
		b[i*2654435761] = i
		if i == 0 {
			break
		}
	}
	var fa, fb uint64
	for k, v := range a {
		fa += uint64(k) * uint64(v+1)
	}
	for k, v := range b {
		fb += uint64(k) * uint64(v+1)
	}
	fmt.Println("fingerprints equal", fa == fb, fa)

	// Zero-size values and pointer values.
	set := map[string]struct{}{}
	for _, w := range words {
		set[w] = struct{}{}
	}
	pm := map[int]*int{}
	for i := 0; i < 10; i++ {
		v := i * i
		pm[i] = &v
	}
	*pm[3] = -9
	ptotal := 0
	for _, p := range pm {
		ptotal += *p
	}
	fmt.Println("set size", len(set), "pointer values total", ptotal)
	_, has := set["zzz"]
	v, ok := pm[42]
	fmt.Println("comma-ok", has, v == nil, ok)
}

func countDeletedSeen(seen, deleted map[int]bool) int {
	n := 0
	for k := range deleted {
		if seen[k] {
			n++
		}
	}
	return n
}
