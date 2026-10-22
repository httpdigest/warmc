// Shifts and math/bits.  Go defines a shift by a count at or past the width
// of the operand: a left shift or an unsigned right shift gives 0, and a
// signed right shift gives 0 or -1 by sign.  Wasm masks the count instead,
// so TinyGo guards every variable shift with a compare and a select, which
// an engine's own shift lowering must not reorder or fold away.  Counts here
// run from 0 to 130 at every width, through signed and unsigned count types,
// and with constant counts that LLVM folds into masks.  math/bits covers the
// count-leading/trailing-zeros, popcount, rotate, reverse and byte-swap
// instructions, and the multi-word Add64/Sub64/Mul64/Div64 helpers.
package main

import (
	"fmt"
	"math/bits"
	"os"
)

var bases = []uint64{1, 0x80, 0xff, 0x8000, 0xdead, 0x80000000, 0xdeadbeef,
	0x8000000000000000, 0xfedcba9876543210, 0x0123456789abcdef,
	0xffffffffffffffff, 0x5a5a5a5a5a5a5a5a}

func init() {
	if len(os.Args) > 1000 {
		bases[0] = 3
	}
}

//go:noinline
func sh64(x uint64, s uint) string {
	return fmt.Sprintf("%x %x %x %x", x<<s, x>>s, uint64(int64(x)<<s),
		uint64(int64(x)>>s))
}

//go:noinline
func sh32(x uint32, s uint8) string {
	return fmt.Sprintf("%x %x %x %x", x<<s, x>>s, uint32(int32(x)<<s),
		uint32(int32(x)>>s))
}

//go:noinline
func sh16(x uint16, s int) string {
	return fmt.Sprintf("%x %x %x %x", x<<s, x>>s, uint16(int16(x)<<s),
		uint16(int16(x)>>s))
}

//go:noinline
func sh8(x uint8, s uint64) string {
	return fmt.Sprintf("%x %x %x %x", x<<s, x>>s, uint8(int8(x)<<s),
		uint8(int8(x)>>s))
}

// A shift whose count is the result of arithmetic the engine cannot see.
//
//go:noinline
func shMixed(x int64, s int32) (int64, int64, uint32, int8) {
	return x << s, x >> (s + 1), uint32(x) >> (s * 2), int8(x) >> (s / 2)
}

// Constant shift amounts: masks and sign extensions LLVM forms itself.
//
//go:noinline
func constShifts(x uint64) string {
	i := int64(x)
	return fmt.Sprintf("%x %x %x %x %x %x %x %x", x<<1, x>>1, x<<63, x>>63,
		i>>63, (i<<56)>>56, (i<<32)>>32, uint64(uint32(x)<<16)>>8)
}

//go:noinline
func bitsInfo(x uint64) string {
	x32, x16, x8 := uint32(x), uint16(x), uint8(x)
	return fmt.Sprint(
		bits.LeadingZeros64(x), bits.LeadingZeros32(x32),
		bits.LeadingZeros16(x16), bits.LeadingZeros8(x8), "|",
		bits.TrailingZeros64(x), bits.TrailingZeros32(x32),
		bits.TrailingZeros16(x16), bits.TrailingZeros8(x8), "|",
		bits.OnesCount64(x), bits.OnesCount32(x32), bits.OnesCount16(x16),
		bits.OnesCount8(x8), "|", bits.Len64(x), bits.Len32(x32),
		bits.Len16(x16), bits.Len8(x8), bits.Len(uint(x)))
}

//go:noinline
func bitsPerm(x uint64, k int) string {
	return fmt.Sprintf("%x %x %x %x | %x %x %x %x | %x %x %x",
		bits.RotateLeft64(x, k), bits.RotateLeft32(uint32(x), k),
		bits.RotateLeft16(uint16(x), k), bits.RotateLeft8(uint8(x), k),
		bits.Reverse64(x), bits.Reverse32(uint32(x)), bits.Reverse16(uint16(x)),
		bits.Reverse8(uint8(x)), bits.ReverseBytes64(x),
		bits.ReverseBytes32(uint32(x)), bits.ReverseBytes16(uint16(x)))
}

//go:noinline
func wide(a, b uint64) string {
	s, c := bits.Add64(a, b, 0)
	s1, c1 := bits.Add64(a, b, 1)
	d, bo := bits.Sub64(a, b, 0)
	d1, bo1 := bits.Sub64(a, b, 1)
	hi, lo := bits.Mul64(a, b)
	h32, l32 := bits.Mul32(uint32(a), uint32(b))
	out := fmt.Sprintf("add %x/%d %x/%d sub %x/%d %x/%d mul %x:%x %x:%x",
		s, c, s1, c1, d, bo, d1, bo1, hi, lo, h32, l32)
	if b != 0 {
		// hi < b is required by Div64; reduce it first.
		q, r := bits.Div64(hi%b, lo, b)
		out += fmt.Sprintf(" div %x %x rem %x", q, r, bits.Rem64(hi, lo, b))
	}
	return out
}

// 128-bit arithmetic from 64-bit halves, as a hand-written carry chain.
type u128 struct{ hi, lo uint64 }

//go:noinline
func mul128(a, b u128) u128 {
	hi, lo := bits.Mul64(a.lo, b.lo)
	hi += a.hi*b.lo + a.lo*b.hi
	return u128{hi, lo}
}

//go:noinline
func shl128(a u128, s uint) u128 {
	if s >= 64 {
		return u128{a.lo << (s - 64), 0}
	}
	return u128{a.hi<<s | a.lo>>(64-s), a.lo << s}
}

func main() {
	fmt.Println("-- 64-bit shifts: x<<s x>>s int<<s int>>s")
	for _, x := range bases {
		for s := uint(0); s <= 130; s += 3 {
			fmt.Println(s, sh64(x, s))
		}
	}
	fmt.Println("-- 32-bit shifts, count uint8")
	for _, x := range bases[:8] {
		for s := uint8(0); s <= 70; s += 5 {
			fmt.Println(s, sh32(uint32(x), s), sh32(uint32(x>>32), s))
		}
	}
	fmt.Println("-- 16-bit shifts, count int")
	for _, x := range bases[:6] {
		for s := 0; s <= 40; s += 3 {
			fmt.Println(s, sh16(uint16(x), s), sh16(uint16(x>>8), s))
		}
	}
	fmt.Println("-- 8-bit shifts, count uint64")
	for _, x := range bases[:4] {
		for _, s := range []uint64{0, 1, 3, 7, 8, 9, 15, 31, 32, 63, 64, 65, 255,
			256, 1 << 32, 1<<63 + 1} {
			fmt.Println(s, sh8(uint8(x), s))
		}
	}
	fmt.Println("-- mixed counts")
	for _, x := range []int64{1, -1, 12345, -98765, 1 << 40, -1 << 62} {
		for s := int32(0); s < 70; s += 7 {
			a, b, c, d := shMixed(x, s)
			fmt.Println(x, s, a, b, c, d)
		}
	}
	fmt.Println("-- constant shifts")
	for _, x := range bases {
		fmt.Println(constShifts(x))
	}
	fmt.Println("-- bits counts")
	for _, x := range append(bases, 0, 2, 0x100, 0x10000) {
		fmt.Printf("%x %s\n", x, bitsInfo(x))
	}
	fmt.Println("-- bits rotate/reverse")
	for _, x := range bases {
		for _, k := range []int{0, 1, -1, 7, -9, 31, 64, 100, -100} {
			fmt.Println(k, bitsPerm(x, k))
		}
	}
	fmt.Println("-- wide arithmetic")
	for _, a := range bases {
		for _, b := range bases[3:] {
			fmt.Println(wide(a, b))
		}
	}
	fmt.Println("-- 128-bit")
	v := u128{0, bases[0]}
	for i := 0; i < 40; i++ {
		v = mul128(v, u128{0, 0x9e3779b97f4a7c15})
		v.lo ^= uint64(i)
		w := shl128(v, uint(i*3))
		fmt.Printf("%016x%016x %016x%016x\n", v.hi, v.lo, w.hi, w.lo)
	}
}
