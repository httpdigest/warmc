// Arrays are values.  Assignment, passing and returning copy the whole array
// (the callee's writes must not show in the caller), == compares every
// element, arrays are valid map keys, `range` over an array value iterates a
// copy taken before the loop while `range` over a pointer or a slice sees
// later writes, and multi-dimensional arrays are contiguous values with the
// same rules at every level.  Matrix multiply, transpose, rotation and a
// Game of Life generation on fixed-size arrays exercise the indexing; arrays
// of structs and structs of arrays exercise the copies.
package main

import (
	"fmt"
	"os"
)

type mat [4][4]int

var start = [8]int{5, 3, 8, 1, 9, 2, 7, 4}

func init() {
	if len(os.Args) > 1000 {
		start[0] = 0
	}
}

//go:noinline
func double(a [8]int) [8]int {
	for i := range a {
		a[i] *= 2
	}
	return a
}

//go:noinline
func doubleP(a *[8]int) {
	for i := range a {
		a[i] *= 2
	}
}

//go:noinline
func mul(a, b mat) mat {
	var c mat
	for i := 0; i < 4; i++ {
		for j := 0; j < 4; j++ {
			for k := 0; k < 4; k++ {
				c[i][j] += a[i][k] * b[k][j]
			}
		}
	}
	return c
}

//go:noinline
func transpose(a mat) mat {
	for i := 0; i < 4; i++ {
		for j := i + 1; j < 4; j++ {
			a[i][j], a[j][i] = a[j][i], a[i][j]
		}
	}
	return a
}

//go:noinline
func rotate(a mat) (r mat) {
	for i := 0; i < 4; i++ {
		for j := 0; j < 4; j++ {
			r[j][3-i] = a[i][j]
		}
	}
	return
}

const W, H = 12, 10

type board [H][W]bool

//go:noinline
func step(b board) board {
	var n board
	for y := 0; y < H; y++ {
		for x := 0; x < W; x++ {
			c := 0
			for dy := -1; dy <= 1; dy++ {
				for dx := -1; dx <= 1; dx++ {
					if (dx != 0 || dy != 0) && b[(y+dy+H)%H][(x+dx+W)%W] {
						c++
					}
				}
			}
			n[y][x] = c == 3 || (c == 2 && b[y][x])
		}
	}
	return n
}

func (b board) String() string {
	s := ""
	for _, row := range b {
		for _, c := range row {
			if c {
				s += "#"
			} else {
				s += "."
			}
		}
		s += "\n"
	}
	return s
}

type item struct {
	name  string
	tags  [3]string
	score [2]float64
}

func main() {
	fmt.Println("-- copies")
	a := start
	b := a
	b[0] = 100
	c := double(a)
	fmt.Println(a, b, c)
	doubleP(&a)
	fmt.Println(a, start)
	p := &a
	q := *p
	q[1] = -1
	p[2] = -2
	fmt.Println(a, q, len(p), cap(p[:]))
	fmt.Println("-- comparison")
	x := [3]string{"a", "b", "c"}
	y := [3]string{"a", "b", "c"}
	z := y
	z[2] = "C"
	fmt.Println(x == y, x == z, x != z, [0]int{} == [0]int{})
	type pair [2]int
	fmt.Println(pair{1, 2} == pair{1, 2}, pair{1, 2} == pair{2, 1})
	fmt.Println("-- range copies")
	r := [5]int{1, 2, 3, 4, 5}
	for i, v := range r { // ranges over a copy
		if i+1 < len(r) {
			r[i+1] += 10
		}
		fmt.Print(v, " ")
	}
	fmt.Println(r)
	r = [5]int{1, 2, 3, 4, 5}
	for i, v := range &r { // pointer: sees the writes
		if i+1 < len(r) {
			r[i+1] += 10
		}
		fmt.Print(v, " ")
	}
	fmt.Println(r)
	r = [5]int{1, 2, 3, 4, 5}
	for i, v := range r[:] { // slice: sees the writes
		if i+1 < len(r) {
			r[i+1] += 10
		}
		fmt.Print(v, " ")
	}
	fmt.Println(r)
	fmt.Println("-- map keys")
	counts := map[[2]int]int{}
	for i := 0; i < 50; i++ {
		counts[[2]int{i % 3, i % 4}]++
	}
	for i := 0; i < 3; i++ {
		for j := 0; j < 4; j++ {
			fmt.Print(counts[[2]int{i, j}], " ")
		}
	}
	fmt.Println(len(counts))
	fmt.Println("-- matrices")
	var m mat
	for i := 0; i < 4; i++ {
		for j := 0; j < 4; j++ {
			m[i][j] = start[(i*4+j)%8] - i
		}
	}
	id := mat{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}
	fmt.Println(m)
	fmt.Println(mul(m, id) == m, mul(id, m) == m)
	fmt.Println(transpose(m))
	fmt.Println(rotate(m))
	fmt.Println(rotate(rotate(rotate(rotate(m)))) == m)
	pw := id
	for i := 0; i < 5; i++ {
		pw = mul(pw, m)
		fmt.Println(i, pw)
	}
	tr := 0
	for i := range pw {
		tr += pw[i][i]
	}
	fmt.Println("trace", tr, m == transpose(transpose(m)))
	row := m[1] // a copy of one row
	row[0] = 999
	fmt.Println(m[1], row)
	fmt.Println("-- life")
	var bd board
	glider := [][2]int{{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}
	for _, g := range glider {
		bd[g[1]][g[0]] = true
	}
	blinker := [][2]int{{8, 6}, {8, 7}, {8, 8}}
	for _, g := range blinker {
		bd[g[1]][g[0]] = true
	}
	for gen := 0; gen <= 8; gen++ {
		if gen%4 == 0 {
			fmt.Printf("gen %d\n%v", gen, bd)
		}
		prev := bd
		bd = step(bd)
		if prev == bd {
			fmt.Println("still")
		}
	}
	fmt.Println("-- arrays of structs")
	items := [3]item{
		{"a", [3]string{"x", "y", "z"}, [2]float64{1.5, 2}},
		{"b", [3]string{"p"}, [2]float64{3, 0.25}},
		{name: "c"},
	}
	cp := items
	cp[0].tags[1] = "CHANGED"
	cp[2].score[0] = 9
	fmt.Println(items)
	fmt.Println(cp)
	fmt.Println(items == cp, items[1] == cp[1])
	ip := &items[1]
	ip.tags[2] = "q"
	it := items[1]
	it.name = "copy"
	fmt.Printf("%+v %+v\n", items[1], it)
	var big [300]int
	for i := range big {
		big[i] = i * i % 97
	}
	big2 := big
	for i := range big2 {
		big2[i] += big[len(big)-1-i]
	}
	s := 0
	for _, v := range big2 {
		s += v
	}
	fmt.Println(s, big[299], big2[0], big2[299])
}
