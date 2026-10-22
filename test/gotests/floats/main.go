// float32 and float64 arithmetic as Go defines it: IEEE 754 round-to-nearest
// at the width of the type after every operation, so float32 values must be
// rounded to single precision each step and never kept in double.  Signed
// zeros, infinities and NaN (compared, not printed by bit pattern, since a
// NaN's payload is not fixed by wasm), the min/max builtins with -0 and NaN,
// float<->int conversions in range (truncation toward zero, uint64 values
// past 2^63 in both directions, int64 values past 2^53 rounding to even),
// float32<->float64 narrowing, subnormals, and separately rounded a*b+c,
// which an engine must not fuse into a single-rounding FMA.
package main

import (
	"fmt"
	"math"
	"os"
)

var f64s = []float64{0, 1, -1, 0.1, -0.1, 0.5, 1.5, 2.5, -2.5, 3, 1e10,
	-1e-10, 123.456, 1 << 53, 1<<53 + 2, 1e300, -1e300, 5e-324, 2.2250738585072014e-308,
	math.Pi, math.E, 1.0 / 3, 16777217, 0.30000000000000004}

var f32s = []float32{0, 1, -1, 0.1, 1.0 / 3, 16777216, 16777217, 3.4e38,
	1.17549435e-38, 1.4e-45, -2.5, 0.7, 65504}

func init() {
	if len(os.Args) > 1000 {
		f64s[0], f32s[0] = 9, 9
	}
}

//go:noinline
func zero() float64 { return f64s[0] }

//go:noinline
func arith64(a, b float64) string {
	return fmt.Sprint(a+b, " ", a-b, " ", a*b, " ", a/b, " ", a < b, a <= b,
		a == b, a != b)
}

//go:noinline
func arith32(a, b float32) string {
	return fmt.Sprint(a+b, " ", a-b, " ", a*b, " ", a/b, " ", a < b, a == b)
}

// Separately rounded multiply-add.  If the engine fuses these the low bits
// change.
//
//go:noinline
func mulAdd(a, b, c float64) (float64, float64, float64) {
	p := a * b
	return p + c, a*b - c, c - a*b
}

//go:noinline
func mulAdd32(a, b, c float32) float32 { return a*b + c }

//go:noinline
func toInts(f float64) string {
	return fmt.Sprint(int64(f), " ", int32(f), " ", int8(int32(f)), " ",
		uint8(int32(f)), " ", int(f))
}

//go:noinline
func toUint64(f float64) uint64 { return uint64(f) }

//go:noinline
func fromU64(u uint64) (float64, float32) { return float64(u), float32(u) }

//go:noinline
func fromI64(i int64) (float64, float32) { return float64(i), float32(i) }

//go:noinline
func narrow(f float64) float32 { return float32(f) }

//go:noinline
func minmax(a, b float64) (float64, float64) { return min(a, b), max(a, b) }

//go:noinline
func minmax32(a, b, c float32) (float32, float32) { return min(a, b, c), max(a, b, c) }

func show(f float64) string {
	switch {
	case math.IsNaN(f):
		return "NaN"
	case f == 0 && math.Signbit(f):
		return "-0"
	}
	return fmt.Sprintf("%v/%016x", f, math.Float64bits(f))
}

func show32(f float32) string {
	switch {
	case f != f:
		return "NaN"
	case f == 0 && math.Signbit(float64(f)):
		return "-0"
	}
	return fmt.Sprintf("%v/%08x", f, math.Float32bits(f))
}

func main() {
	fmt.Println("-- float64 + - * / and comparisons")
	for _, a := range f64s {
		for _, b := range f64s[:12] {
			fmt.Println(a, b, arith64(a, b))
		}
	}
	fmt.Println("-- float32 + - * / and comparisons")
	for _, a := range f32s {
		for _, b := range f32s {
			fmt.Println(a, b, arith32(a, b))
		}
	}
	fmt.Println("-- special values")
	z := zero()
	inf := 1 / z
	ninf := -1 / z
	nan := z / z
	negz := -z
	fmt.Println(inf, ninf, nan, show(negz), show(1/inf), show(-1/inf))
	fmt.Println(inf+ninf, inf*z, inf-inf, inf > 1e308, ninf < -1e308)
	fmt.Println(nan == nan, nan != nan, nan < 1, nan > 1, nan <= nan)
	fmt.Println(negz == z, 1/negz, math.Signbit(negz), math.Signbit(z+negz),
		math.Signbit(negz+negz), math.Signbit(negz*-1), math.Signbit(z*-1))
	fmt.Println(math.IsInf(inf, 1), math.IsInf(ninf, -1), math.IsNaN(nan),
		math.IsInf(f64s[15]*1e10, 0), math.IsInf(f64s[15]*1e10*1e10, 1))
	fmt.Println("-- min and max builtins")
	pairs := [][2]float64{{1, 2}, {2, 1}, {-1, 1}, {z, negz}, {negz, z},
		{nan, 1}, {1, nan}, {inf, ninf}, {-0.5, -0.25}}
	for _, p := range pairs {
		lo, hi := minmax(p[0], p[1])
		fmt.Println(show(p[0]), show(p[1]), show(lo), show(hi))
	}
	for _, t := range [][3]float32{{1, 2, 3}, {3, -2, 1}, {0, float32(negz), 0},
		{float32(nan), 1, 2}, {2, 1, float32(inf)}} {
		lo, hi := minmax32(t[0], t[1], t[2])
		fmt.Println(show32(lo), show32(hi))
	}
	fmt.Println("-- separately rounded multiply-add")
	ma := [][3]float64{{0.1, 10, -1}, {1 + 1.0/(1<<30), 1 - 1.0/(1<<30), -1},
		{math.Pi, math.E, -8.539734222673566}, {1e16, 1.0000000000000002, -1e16},
		{3, 1.0 / 3, -1}, {0.7, 0.7, -0.49}}
	for _, t := range ma {
		r1, r2, r3 := mulAdd(t[0], t[1], t[2])
		fmt.Println(show(r1), show(r2), show(r3))
		fmt.Println(show32(mulAdd32(float32(t[0]), float32(t[1]), float32(t[2]))))
	}
	fmt.Println("-- to integers (truncation toward zero)")
	for _, f := range []float64{0, 0.9, -0.9, 1.5, -1.5, 2.5, -2.5, 127.9,
		-128.9, 255.5, 1e9, -1e9, 2147483647.9, -2147483648.9, 1 << 52, 1<<62 + 1<<40,
		-(1 << 63)} {
		fmt.Println(f, toInts(f+z))
	}
	for _, f := range []float64{0, 1, 1 << 63, 1<<63 + 1<<11, 1<<64 - 1<<11,
		9.2e18, 1.8e19} {
		fmt.Println(f, toUint64(f+z))
	}
	fmt.Println("-- from integers")
	for _, u := range []uint64{0, 1, 1<<53 + 1, 1<<53 + 3, 1<<63 - 1, 1 << 63,
		1<<63 + 1, 1<<63 + 1<<10, 1<<63 + 1<<11 - 1, 1<<64 - 1, 16777217,
		0x8000008000000001} {
		d, s := fromU64(u)
		fmt.Println(u, show(d), show32(s))
	}
	for _, i := range []int64{-1, -(1<<53 + 1), 1<<53 + 1, -1 << 63, 1<<63 - 1,
		16777217, -16777217, 33554435} {
		d, s := fromI64(i)
		fmt.Println(i, show(d), show32(s))
	}
	fmt.Println("-- narrowing to float32")
	for _, f := range f64s {
		fmt.Println(show(f), show32(narrow(f)), show(float64(narrow(f))))
	}
	for _, f := range []float64{1 + 1.0/(1<<24), 1 + 3.0/(1<<24),
		1 + 1.0/(1<<24) + 1.0/(1<<40), 3.4028235677973366e38, 3.5e38, 1e-46,
		7e-46, 1.5e-45} {
		fmt.Println(show(f), show32(narrow(f+z)))
	}
	fmt.Println("-- accumulation")
	var s32 float32
	var s64 float64
	var k32 float32 // Kahan-compensated
	var c32 float32
	for i := 0; i < 1000; i++ {
		x := f32s[3] * float32(i%7+1)
		s32 += x
		s64 += float64(x)
		y := x - c32
		t := k32 + y
		c32 = (t - k32) - y
		k32 = t
	}
	fmt.Println(show32(s32), show(s64), show32(k32), show32(c32))
	h := 1.0 + z
	for i := 0; i < 60; i++ {
		h = h/3 + 1/h
		if i%6 == 0 {
			fmt.Println(i, show(h), show32(float32(h)))
		}
	}
	fmt.Println("-- subnormals")
	d := 1e-300 + z
	for i := 0; i < 12; i++ {
		d /= 1e3
		fmt.Println(show(d), show(d*1e300), show32(float32(d*1e270)))
	}
	f := float32(1e-30) + float32(z)
	for i := 0; i < 8; i++ {
		f /= 1000
		fmt.Println(show32(f), show32(f*1e30))
	}
	fmt.Println("-- nextafter and ulp steps")
	x := 1.0 + z
	for i := 0; i < 5; i++ {
		x = math.Nextafter(x, 2)
		fmt.Println(show(x), show(x-1))
	}
	y := float32(1) + float32(z)
	for i := 0; i < 5; i++ {
		y = math.Nextafter32(y, -1)
		fmt.Println(show32(y), show32(1-y))
	}
}
