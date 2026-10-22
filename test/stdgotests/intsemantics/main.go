// Go's integer semantics, which the wasm the Go compiler emits must
// reproduce on 64-bit wasm locals: wrap-around on overflow at every width
// (int8 through uint64, through +, -, *, unary minus and ++), shifts by
// counts at and beyond the width (Go defines them: 0 or -1, never the
// modulo-width result wasm's own shift instructions give), signed shifts
// right of negatives, truncated division and remainder with every sign
// combination, MinInt / -1 (defined in Go, a trap in wasm's i64.div_s), and
// conversions between widths that truncate, sign-extend and zero-extend,
// plus bitwise ops and &^ at every width.
//
// Go's wasm back end keeps almost everything as i64 and re-extends after each
// narrow operation, so every narrow-type result here depends on an extend
// being in the right place.  Operands come from //go:noinline functions so
// the compiler cannot fold them.
package main

import (
	"fmt"
	"math"
)

//go:noinline
func i64(x int64) int64 { return x }

//go:noinline
func u64(x uint64) uint64 { return x }

//go:noinline
func cnt(x uint) uint { return x }

//go:noinline
func divmod(a, b int64) (int64, int64) { return a / b, a % b }

//go:noinline
func divmod32(a, b int32) (int32, int32) { return a / b, a % b }

//go:noinline
func divmod8(a, b int8) (int8, int8) { return a / b, a % b }

//go:noinline
func udivmod(a, b uint64) (uint64, uint64) { return a / b, a % b }

func main() {
	// Overflow at each width.
	vals := []int64{0, 1, -1, 127, 128, -128, -129, 255, 256, 32767, 32768, -32769, 65535, 1<<31 - 1, 1 << 31, -1 << 31, 1<<32 - 1, 1 << 32, math.MaxInt64, math.MinInt64}
	for _, v := range vals {
		x := i64(v)
		fmt.Printf("%20d -> i8 %4d u8 %3d i16 %6d u16 %5d i32 %11d u32 %10d u64 %20d\n",
			x, int8(x), uint8(x), int16(x), uint16(x), int32(x), uint32(x), uint64(x))
	}
	// Arithmetic wrap.
	for _, v := range []int64{100, 127, -128, 200, 30000, 1 << 30, math.MaxInt64} {
		a8, a16, a32, a64 := int8(i64(v)), int16(i64(v)), int32(i64(v)), i64(v)
		u8, u16, u32, uu := uint8(i64(v)), uint16(i64(v)), uint32(i64(v)), uint64(i64(v))
		fmt.Printf("v=%d i8: +100=%d *3=%d neg=%d ++=%d | i16: *7=%d | i32: *5=%d +max=%d | i64: *3=%d +1=%d\n",
			v, a8+100, a8*3, -a8, func() int8 { a8++; return a8 }(), a16*7, a32*5, a32+math.MaxInt32, a64*3, a64+1)
		fmt.Printf("     u8: +100=%d *3=%d neg=%d -1=%d | u16 *7=%d | u32 *5=%d neg=%d | u64 *3=%d neg=%d\n",
			u8+100, u8*3, -u8, u8-uint8(i64(v))-1, u16*7, u32*5, -u32, uu*3, -uu)
	}

	// Shifts at and past the width.
	for _, s := range []uint{0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 127, 200} {
		c := cnt(s)
		n8, n32, n64 := int8(-100), int32(-100000), i64(-1000000000000)
		p8, p32, p64 := uint8(0xAB), uint32(0xDEADBEEF), u64(0xFEDCBA9876543210)
		fmt.Printf("shift %3d: i8<<=%4d i8>>=%4d u8<<=%3d u8>>=%3d | i32<<=%11d i32>>=%7d u32<<=%10d u32>>=%10d\n",
			c, n8<<c, n8>>c, p8<<c, p8>>c, n32<<c, n32>>c, p32<<c, p32>>c)
		fmt.Printf("           i64<<=%20d i64>>=%14d u64<<=%016x u64>>=%016x one<<=%d\n",
			n64<<c, n64>>c, p64<<c, p64>>c, int64(1)<<c)
	}
	// Signed count shifts (allowed since Go 1.13).
	for _, s := range []int{0, 3, 64} {
		x := i64(-17)
		fmt.Printf("signed count %d: %d %d\n", s, x<<s, x>>s)
	}

	// Division with sign combinations, all widths.
	for _, p := range [][2]int64{{7, 2}, {-7, 2}, {7, -2}, {-7, -2}, {1, 3}, {-1, 3}, {0, -5}, {math.MaxInt64, 2}, {math.MinInt64, 2}, {math.MinInt64, -1}, {math.MinInt64, 1}, {math.MinInt64, math.MinInt64}} {
		q, r := divmod(i64(p[0]), i64(p[1]))
		fmt.Printf("i64 %d / %d = %d rem %d\n", p[0], p[1], q, r)
	}
	for _, p := range [][2]int32{{-7, 2}, {math.MinInt32, -1}, {math.MinInt32, 3}, {math.MaxInt32, -1}} {
		q, r := divmod32(p[0], p[1])
		fmt.Printf("i32 %d / %d = %d rem %d\n", p[0], p[1], q, r)
	}
	for _, p := range [][2]int8{{-128, -1}, {-128, 3}, {127, -2}, {-7, 2}, {-1, -128}} {
		q, r := divmod8(p[0], p[1])
		fmt.Printf("i8 %d / %d = %d rem %d\n", p[0], p[1], q, r)
	}
	for _, p := range [][2]uint64{{math.MaxUint64, 2}, {math.MaxUint64, math.MaxUint64}, {1 << 63, 3}, {5, 1 << 63}, {math.MaxUint64, 10}} {
		q, r := udivmod(u64(p[0]), u64(p[1]))
		fmt.Printf("u64 %d / %d = %d rem %d\n", p[0], p[1], q, r)
	}
	// Division by constants (the compiler turns these into multiplies).
	for _, v := range []int64{-1000003, -7, -1, 0, 1, 7, 1000003, math.MaxInt64, math.MinInt64} {
		x := i64(v)
		ux := uint64(x)
		fmt.Printf("const div %d: /3=%d %%3=%d /7=%d %%7=%d /10=%d /-4=%d %%-4=%d /16=%d %%16=%d | u /3=%d u /10=%d u %%7=%d\n",
			x, x/3, x%3, x/7, x%7, x/10, x/-4, x%-4, x/16, x%16, ux/3, ux/10, ux%7)
		x32 := int32(x)
		fmt.Printf("    i32 %d: /3=%d %%5=%d /-8=%d u32/7=%d\n", x32, x32/3, x32%5, x32/-8, uint32(x32)/7)
	}

	// Bitwise ops at every width.
	for _, v := range []uint64{0x0123456789ABCDEF, 0xFFFFFFFF00000000, 0x8000000000000001} {
		a := u64(v)
		b := u64(0xF0F0F0F0F0F0F0F0)
		fmt.Printf("u64 & %016x | %016x ^ %016x &^ %016x ^x %016x\n", a&b, a|b, a^b, a&^b, ^a)
		a32, b32 := uint32(a), uint32(b>>4)
		fmt.Printf("u32 & %08x | %08x ^ %08x &^ %08x ^x %08x\n", a32&b32, a32|b32, a32^b32, a32&^b32, ^a32)
		a8, b8 := int8(a), int8(b)
		fmt.Printf("i8 & %d | %d ^ %d &^ %d ^x %d\n", a8&b8, a8|b8, a8^b8, a8&^b8, ^a8)
	}

	// Mixed-width expressions that need correct extension.
	var acc int64
	var uacc uint32
	for i := int64(0); i < 300; i++ {
		x := int8(i64(i * 37))
		y := uint16(i64(i * 1009))
		z := int32(i64(i * i * 7919))
		acc += int64(x)*int64(y) - int64(z)>>3
		uacc = uacc*31 + uint32(x) ^ uint32(y)<<16
	}
	fmt.Println("mixed width acc", acc, "uacc", uacc)

	// Unsigned comparison of values with the top bit set.
	big1, big2 := u64(1<<63), u64(1)
	fmt.Println("unsigned compare", big1 > big2, int64(big1) > int64(big2), uint32(big1>>32) > uint32(big2))
	fmt.Println("min/max builtins", min(i64(-1), 0), max(u64(0), u64(1<<63)), min(int8(-128), int8(i64(127))))

	// Constant expressions evaluated at compile time vs run time.
	const bigc = 1 << 100
	fmt.Println("const", bigc>>98, uint8(bigc>>95), int64(bigc>>40))
	rt := i64(1)
	fmt.Println("runtime", rt<<62>>60, (rt<<63)>>63, uint64(rt<<63)>>63)

	// Hash mixing loop (multiply-xorshift) at 64 and 32 bits.
	h64 := u64(0x9E3779B97F4A7C15)
	h32 := uint32(0x811C9DC5)
	for i := 0; i < 1000; i++ {
		h64 ^= h64 >> 33
		h64 *= 0xFF51AFD7ED558CCD
		h64 ^= h64 >> 29
		h64 += uint64(i)
		h32 ^= uint32(i)
		h32 *= 16777619
		h32 = h32<<5 | h32>>27
	}
	fmt.Printf("hash mix %016x %08x\n", h64, h32)
}
