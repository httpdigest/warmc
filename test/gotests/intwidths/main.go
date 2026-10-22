// Integer arithmetic at every width: int8, int16, int32, int64, int, uint8,
// uint16, uint32, uint64, uint and uintptr.  Add, subtract, multiply, negate
// and the bitwise operators must wrap at the width of the type, which in wasm
// is i32 or i64 with an explicit sign or zero extension after every narrow
// operation -- the extension TinyGo emits is exactly what an engine is
// tempted to drop.  Conversions between widths (truncation, sign extension,
// zero extension), signed against unsigned comparisons with the top bit set,
// counters that run through their wraparound, and overflow detected after
// the fact.  The inputs live in a global the optimiser has to assume can
// change, so LLVM cannot fold the arithmetic away before warmc sees it.
package main

import (
	"fmt"
	"os"
)

var vals = []int64{
	0, 1, -1, 2, -2, 7, -7, 100, -100, 127, -128, 128, -129, 255, 256,
	32767, -32768, 32768, 65535, 65536, 1<<31 - 1, -1 << 31, 1 << 31,
	1<<32 - 1, 1 << 32, 1<<63 - 1, -1 << 63, 0x123456789abcdef0,
	-0x0fedcba987654321, 0x5555555555555555, -0x5555555555555556,
}

var pairs = []int64{0, 1, -1, 3, -3, 100, -128, 127, 255, 40000, -70000,
	1<<31 - 1, -1 << 31, 1<<53 + 7, -1 << 63, 1<<63 - 1}

func init() {
	if len(os.Args) > 1000 {
		vals[0], pairs[0] = 42, 42
	}
}

type integer interface {
	~int8 | ~int16 | ~int32 | ~int64 | ~int |
		~uint8 | ~uint16 | ~uint32 | ~uint64 | ~uint | ~uintptr
}

//go:noinline
func ops[T integer](a, b T) string {
	return fmt.Sprint(a+b, " ", a-b, " ", a*b, " ", a&b, " ", a|b, " ",
		a^b, " ", a&^b, " ", -a, " ", ^a, " ", a < b, " ", a == b, " ", a >= b)
}

//go:noinline
func conv(x int64) string {
	return fmt.Sprint(int8(x), " ", int16(x), " ", int32(x), " ", int(x), " ",
		uint8(x), " ", uint16(x), " ", uint32(x), " ", uint64(x), " ",
		uint(x), " ", uintptr(x))
}

// Round trips through a narrow type and back: the extension on the way up
// is what has to be right.
//
//go:noinline
func widen(x int64) string {
	a := int64(int8(x))
	b := int64(uint8(x))
	c := int64(int16(x))
	d := int64(uint16(x))
	e := int64(int32(x))
	f := int64(uint32(x))
	g := uint64(int8(x))
	h := uint64(int32(x))
	i := int32(int8(x))
	j := uint32(int16(x))
	return fmt.Sprint(a, " ", b, " ", c, " ", d, " ", e, " ", f, " ", g, " ",
		h, " ", i, " ", j)
}

// Signed-or-unsigned comparisons of the same bit patterns.
//
//go:noinline
func cmps(a, b int64) string {
	ua, ub := uint64(a), uint64(b)
	a32, b32 := int32(a), int32(b)
	ua32, ub32 := uint32(a), uint32(b)
	a8, b8 := int8(a), int8(b)
	ua8, ub8 := uint8(a), uint8(b)
	return fmt.Sprint(a < b, ua < ub, a32 < b32, ua32 < ub32, a8 < b8,
		ua8 < ub8, a32 <= b32, ua32 >= ub32, a8 > b8, ua8 >= ub8)
}

//go:noinline
func addOverflows64(a, b int64) bool {
	s := a + b
	return (a >= 0 && b >= 0 && s < 0) || (a < 0 && b < 0 && s >= 0)
}

//go:noinline
func mulOverflows32(a, b int32) bool {
	p := int64(a) * int64(b)
	return p != int64(int32(p))
}

//go:noinline
func wrapCount8(start uint8, step uint8) (n int, last uint8) {
	x := start
	for {
		prev := x
		x += step
		n++
		if x < prev {
			return n, x
		}
	}
}

//go:noinline
func wrapCountSigned16(start int16, step int16) (n int, last int16) {
	x := start
	for {
		prev := x
		x += step
		n++
		if x < prev {
			return n, x
		}
	}
}

// Horner-style mixing at each width, run long enough to wrap many times.
//
//go:noinline
func mix8(seed int) (int8, uint8) {
	var s int8 = int8(seed)
	var u uint8 = uint8(seed)
	for i := 0; i < 1000; i++ {
		s = s*31 + int8(i) ^ (s >> 3)
		u = u*31 + uint8(i) ^ (u >> 3)
	}
	return s, u
}

//go:noinline
func mix16(seed int) (int16, uint16) {
	var s int16 = int16(seed)
	var u uint16 = uint16(seed)
	for i := 0; i < 1000; i++ {
		s = s*31 + int16(i) ^ (s >> 5)
		u = u*31 + uint16(i) ^ (u >> 5)
	}
	return s, u
}

//go:noinline
func mix32(seed int) (int32, uint32) {
	var s int32 = int32(seed)
	var u uint32 = uint32(seed)
	for i := 0; i < 1000; i++ {
		s = s*1000003 + int32(i) ^ (s >> 13)
		u = u*1000003 + uint32(i) ^ (u >> 13)
	}
	return s, u
}

//go:noinline
func mix64(seed int) (int64, uint64) {
	var s int64 = int64(seed)
	var u uint64 = uint64(seed)
	for i := 0; i < 1000; i++ {
		s = s*6364136223846793005 + int64(i) ^ (s >> 29)
		u = u*6364136223846793005 + uint64(i) ^ (u >> 29)
	}
	return s, u
}

// Accumulating into narrow types through a slice: the loads must extend.
//
//go:noinline
func sums(xs []int64) string {
	b8 := make([]int8, len(xs))
	u8 := make([]uint8, len(xs))
	b16 := make([]int16, len(xs))
	u16 := make([]uint16, len(xs))
	b32 := make([]int32, len(xs))
	u32 := make([]uint32, len(xs))
	for i, x := range xs {
		b8[i], u8[i] = int8(x), uint8(x)
		b16[i], u16[i] = int16(x), uint16(x)
		b32[i], u32[i] = int32(x), uint32(x)
	}
	var s8 int64
	var su8 uint64
	var s16 int64
	var su16 uint64
	var s32 int64
	var su32 uint64
	var w8 int8
	var w16 int16
	var w32 int32
	for i := range xs {
		s8 += int64(b8[i])
		su8 += uint64(u8[i])
		s16 += int64(b16[i])
		su16 += uint64(u16[i])
		s32 += int64(b32[i])
		su32 += uint64(u32[i])
		w8 += b8[i]
		w16 += b16[i]
		w32 += b32[i]
	}
	return fmt.Sprint(s8, " ", su8, " ", s16, " ", su16, " ", s32, " ", su32,
		" ", w8, " ", w16, " ", w32)
}

func main() {
	fmt.Println("-- conversions: int8 int16 int32 int uint8 uint16 uint32 uint64 uint uintptr")
	for _, v := range vals {
		fmt.Printf("%d: %s\n", v, conv(v))
	}
	fmt.Println("-- widening round trips")
	for _, v := range vals {
		fmt.Printf("%d: %s\n", v, widen(v))
	}
	fmt.Println("-- ops: + - * & | ^ &^ neg not < == >=")
	for _, a := range pairs {
		for _, b := range pairs[:8] {
			fmt.Printf("a=%d b=%d\n", a, b)
			fmt.Println("  i8 ", ops(int8(a), int8(b)))
			fmt.Println("  u8 ", ops(uint8(a), uint8(b)))
			fmt.Println("  i16", ops(int16(a), int16(b)))
			fmt.Println("  u16", ops(uint16(a), uint16(b)))
			fmt.Println("  i32", ops(int32(a), int32(b)))
			fmt.Println("  u32", ops(uint32(a), uint32(b)))
			fmt.Println("  i64", ops(a, b))
			fmt.Println("  u64", ops(uint64(a), uint64(b)))
			fmt.Println("  int", ops(int(a), int(b)))
			fmt.Println("  uptr", ops(uintptr(a), uintptr(b)))
		}
	}
	fmt.Println("-- comparisons signed vs unsigned at 64/32/8")
	for _, a := range pairs {
		for _, b := range pairs {
			if a < b {
				fmt.Println(a, b, cmps(a, b))
			}
		}
	}
	fmt.Println("-- overflow checks")
	for _, a := range pairs {
		for _, b := range pairs {
			if addOverflows64(a, b) || mulOverflows32(int32(a), int32(b)) {
				fmt.Println(a, b, addOverflows64(a, b), mulOverflows32(int32(a), int32(b)))
			}
		}
	}
	fmt.Println("-- wraparound counters")
	for _, st := range []uint8{0, 1, 7, 13, 100, 255} {
		n, last := wrapCount8(uint8(pairs[1]+int64(st)), st|1)
		fmt.Println("u8", st, n, last)
	}
	for _, st := range []int16{1, 7, 1000, 32767} {
		n, last := wrapCountSigned16(int16(pairs[0]), st)
		fmt.Println("i16", st, n, last)
	}
	fmt.Println("-- mixing")
	for seed := -3; seed < 4; seed++ {
		a, b := mix8(seed)
		c, d := mix16(seed)
		e, f := mix32(seed)
		g, h := mix64(seed)
		fmt.Println(seed, a, b, c, d, e, f, g, h)
	}
	fmt.Println("-- narrow sums")
	fmt.Println(sums(vals))
	fmt.Println(sums(pairs))
	var mn8 int8 = -128
	var mn16 int16 = -32768
	var mn32 int32 = -1 << 31
	var mn64 int64 = -1 << 63
	if len(os.Args) > 1000 {
		mn8, mn16, mn32, mn64 = 0, 0, 0, 0
	}
	fmt.Println("-- negating the minimum", -mn8, -mn16, -mn32, -mn64,
		mn8-1, mn16-1, mn32-1, mn64-1, mn8*-1, mn64*-1)
}
