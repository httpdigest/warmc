// Big structs passed and returned by value, and pointer aliasing.  A struct
// of several hundred bytes mixing every scalar width with arrays and a
// string is passed by value (the callee's changes stay local), returned by
// value through chains of calls, swapped, compared with ==, stored in
// slices, arrays and maps and copied out again.  TinyGo passes such values
// through memory with memcpy-style copies whose sizes and alignments vary,
// and LLVM turns some into wide loads and stores.  Pointer aliasing covers
// two pointers to one element, pointers into a slice that is then grown,
// self-assignment through aliases, a swap through aliased pointers, and
// overlapping copy() of struct slices.
package main

import (
	"fmt"
	"os"
)

type big struct {
	a  int8
	b  int64
	c  uint16
	d  float32
	e  [7]byte
	f  float64
	g  [5]int32
	h  string
	i  bool
	j  [3]uint64
	k  int16
	m  [4][4]float32
	n  uint8
	o  complex128
	p  [9]int16
	q  uintptr
	tl [33]byte
}

var k0 = 1

func init() {
	if len(os.Args) > 1000 {
		k0 = 2
	}
}

//go:noinline
func make1(k int) big {
	var b big
	b.a = int8(k * 7)
	b.b = int64(k) << 40
	b.c = uint16(k * 999)
	b.d = float32(k) / 3
	for i := range b.e {
		b.e[i] = byte('a' + (k+i)%26)
	}
	b.f = float64(k) * 1.25
	for i := range b.g {
		b.g[i] = int32(k*i - 50)
	}
	b.h = fmt.Sprint("s", k)
	b.i = k%2 == 0
	for i := range b.j {
		b.j[i] = uint64(k+i) * 0x0101010101010101
	}
	b.k = int16(-k * 300)
	for i := range b.m {
		for j := range b.m[i] {
			b.m[i][j] = float32(i*4+j) * float32(k) / 8
		}
	}
	b.n = uint8(255 - k)
	b.o = complex(float64(k), -float64(k)/2)
	for i := range b.p {
		b.p[i] = int16(k * (i - 4) * 1000)
	}
	b.q = uintptr(k * 4096)
	for i := range b.tl {
		b.tl[i] = byte(k + i)
	}
	return b
}

//go:noinline
func mutate(b big) big {
	b.a++
	b.b--
	b.e[0] = 'Z'
	b.g[4] *= 2
	b.h += "!"
	b.m[3][3] = -1
	b.tl[32] = 0xff
	b.o *= 2i
	return b
}

//go:noinline
func digest(b big) uint64 {
	h := uint64(14695981039346656037)
	mix := func(v uint64) { h = (h ^ v) * 1099511628211 }
	mix(uint64(b.a))
	mix(uint64(b.b))
	mix(uint64(b.c))
	mix(uint64(int64(b.d * 1000)))
	for _, x := range b.e {
		mix(uint64(x))
	}
	mix(uint64(int64(b.f * 1000)))
	for _, x := range b.g {
		mix(uint64(x))
	}
	for i := 0; i < len(b.h); i++ {
		mix(uint64(b.h[i]))
	}
	if b.i {
		mix(1)
	}
	for _, x := range b.j {
		mix(x)
	}
	mix(uint64(b.k))
	for _, r := range b.m {
		for _, x := range r {
			mix(uint64(int64(x * 64)))
		}
	}
	mix(uint64(b.n))
	mix(uint64(int64(real(b.o) * 100)))
	mix(uint64(int64(imag(b.o) * 100)))
	for _, x := range b.p {
		mix(uint64(x))
	}
	mix(uint64(b.q))
	for _, x := range b.tl {
		mix(uint64(x))
	}
	return h
}

//go:noinline
func swap(x, y big) (big, big) { return y, x }

//go:noinline
func pick(c bool, x, y big) big {
	if c {
		return x
	}
	return y
}

//go:noinline
func chain(b big, n int) big {
	if n == 0 {
		return b
	}
	return chain(mutate(b), n-1)
}

func summary(b big) string {
	return fmt.Sprintf("a=%d b=%d c=%d d=%g e=%s f=%g g=%v h=%s i=%v j0=%x k=%d m33=%g n=%d o=%v p=%v q=%d tl=%d,%d",
		b.a, b.b, b.c, b.d, b.e[:], b.f, b.g, b.h, b.i, b.j[0], b.k, b.m[3][3], b.n, b.o, b.p, b.q, b.tl[0], b.tl[32])
}

type cell struct{ v, w int }

func main() {
	fmt.Println("-- by value")
	x := make1(k0 + 2)
	y := mutate(x)
	fmt.Println(summary(x))
	fmt.Println(summary(y))
	fmt.Println(x == y, x == make1(3), digest(x), digest(y))
	p, q := swap(x, y)
	fmt.Println(p == y, q == x, digest(p) == digest(y))
	fmt.Println(digest(pick(true, x, y)) == digest(x), digest(pick(false, x, y)) == digest(y))
	z := chain(x, 5)
	fmt.Println(summary(z))
	fmt.Println(digest(z), x.h, z.h)
	for k := 0; k < 12; k++ {
		b := make1(k + k0)
		c := chain(b, k%4)
		l, r := swap(b, c)
		fmt.Printf("%2d %016x %016x %v %v %d %s\n", k, digest(b), digest(c), l == c, r == b,
			c.a, c.h)
	}
	fmt.Println("-- in containers")
	arr := [4]big{}
	sl := make([]big, 0, 2)
	m := map[int]big{}
	for i := 0; i < 6; i++ {
		b := make1(i + k0)
		if i < 4 {
			arr[i] = b
		}
		sl = append(sl, b)
		m[i] = b
	}
	cpArr := arr
	cpArr[2].h = "changed"
	cpArr[3].m[0][1] = 1000
	e := m[4]
	e.c = 1
	fmt.Println(arr[2].h, cpArr[2].h, arr[3].m[0][1], m[4].c, e.c)
	sl[1] = mutate(sl[1])
	sl = append(sl[:2], sl[3:]...)
	total := uint64(0)
	for i, b := range sl {
		total ^= digest(b) + uint64(i)
	}
	fmt.Println(len(sl), total)
	for i := range 6 {
		fmt.Print(digest(m[i])%1000, " ")
	}
	fmt.Println()
	ptrs := []*big{&sl[0], &sl[1], &arr[0]}
	ptrs[0].a = 100
	ptrs[2].h = "via pointer"
	fmt.Println(sl[0].a, arr[0].h, cpArr[0].h)
	fmt.Println("-- aliasing")
	cells := make([]cell, 3, 3)
	a, b := &cells[1], &cells[1]
	a.v = 5
	b.w = a.v * 2
	fmt.Println(cells, a == b, *a == *b)
	old := &cells[0]
	cells = append(cells, cell{9, 9}) // reallocates
	old.v = -1
	cells[0].w = -2
	fmt.Println(cells, *old)
	s := []int{1, 2, 3, 4, 5}
	pa, pb := &s[1], &s[1]
	*pa, *pb = *pb+10, *pa+20 // both read before either write
	fmt.Println(s)
	sw := func(x, y *int) { *x, *y = *y, *x }
	sw(&s[0], &s[0])
	sw(&s[3], &s[4])
	fmt.Println(s)
	bb := make1(7)
	pb1, pb2 := &bb, &bb
	*pb1 = *pb2 // self-assignment
	*pb1 = mutate(*pb2)
	fmt.Println(summary(bb))
	sc := []cell{{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}}
	copy(sc[1:], sc) // overlapping struct copy
	fmt.Println(sc)
	copy(sc, sc[2:])
	fmt.Println(sc)
	grid := [3][3]cell{}
	row := &grid[1]
	row[2].v = 7
	colp := &grid[2][0]
	colp.w = 8
	g2 := grid
	g2[1][2].v = 70
	fmt.Println(grid, g2[1][2])
	type node struct {
		val  big
		next *node
	}
	head := &node{val: make1(1)}
	head.next = &node{val: head.val}
	head.next.val.a = 99
	head.next.next = head // cycle
	fmt.Println(head.val.a, head.next.val.a, head.next.next.val.a, head.next.next == head)
}
