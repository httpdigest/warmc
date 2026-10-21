// A type switch over a slice of interfaces, the same source as
// test/gobench/typeswitch.  TinyGo turns it into a switch on a type id; Go
// compares the type hash in the value's itab against each case's in turn,
// confirms a match by the itab's address, and then makes the area() call
// indirectly through the itab.
package main

import "fmt"

type shape interface{ area() int }

type sq struct{ s int }
type re struct{ w, h int }
type tr struct{ b, h int }

func (x sq) area() int { return x.s * x.s }
func (x re) area() int { return x.w * x.h }
func (x tr) area() int { return x.b * x.h / 2 }

//go:noinline
func classify(xs []shape) (int, int) {
	kinds, sum := 0, 0
	for _, x := range xs {
		switch v := x.(type) {
		case sq:
			kinds += 1
			sum += v.s
		case re:
			kinds += 2
			sum += v.w + v.h
		case tr:
			kinds += 3
			sum += v.b
		default:
			kinds += 7
		}
		sum += x.area()
	}
	return kinds, sum
}

func main() {
	const n = 512
	xs := make([]shape, n)
	for i := 0; i < n; i++ {
		switch i % 3 {
		case 0:
			xs[i] = sq{i % 11}
		case 1:
			xs[i] = re{i % 7, i % 5}
		default:
			xs[i] = tr{i % 9, i % 4}
		}
	}
	total := 0
	for r := 0; r < 80000; r++ {
		xs[r&(n-1)] = sq{r % 11}
		k, s := classify(xs)
		total += k + s
	}
	fmt.Println("total =", total)
}
