// Generic functions, instantiated the way Go's compiler does it.
//
// TinyGo, like LLVM-based Rust, makes one copy of a generic function per type
// argument.  Go's compiler makes one per GC shape instead, and every pointer
// type has the same shape: best[*small] and best[*wide] below are one function,
// which is handed a dictionary and finds the key method in it.  So a method
// call on a type parameter is an indirect call here even though both call
// sites name a concrete type.  sumOf is the contrast: int64 and float64 are
// different shapes, so those two are stenciled apart and their loops are
// ordinary arithmetic.
package main

import "fmt"

type keyed interface{ key() uint64 }

type small struct{ v uint64 }
type wide struct {
	v uint64
	w uint32
}

func (x *small) key() uint64 { return x.v * 3 }
func (x *wide) key() uint64  { return x.v ^ uint64(x.w) }

//go:noinline
func best[T keyed](xs []T) (T, uint64) {
	var bt T
	var bk uint64
	for _, x := range xs {
		if k := x.key(); k >= bk {
			bt, bk = x, k
		}
	}
	return bt, bk
}

type number interface{ ~int64 | ~float64 }

//go:noinline
func sumOf[T number](xs []T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

func main() {
	const n = 512
	ss := make([]*small, n)
	ws := make([]*wide, n)
	is := make([]int64, n)
	fs := make([]float64, n)
	for i := 0; i < n; i++ {
		ss[i] = &small{uint64(i*2654435761) & 0xffffff}
		ws[i] = &wide{uint64(i) * 977, uint32(i * 31)}
		is[i] = int64(i) * 3
		fs[i] = float64(i) / 2
	}
	var total uint64
	for r := 0; r < 40000; r++ {
		ss[r&(n-1)].v = uint64(r)
		is[r&(n-1)] = int64(r)
		_, k1 := best(ss)
		_, k2 := best(ws)
		total += k1 + k2 + uint64(sumOf(is)) + uint64(sumOf(fs))
	}
	fmt.Println("total =", total)
}
