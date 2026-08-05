// Arithmetic at its edges, folded into one checksum per group.
//
// The values are the ones a compiler gets wrong: the minimum of each signed
// width (whose negation is itself), division by -1 at that minimum, shifts at
// and past the width, and the float boundaries where a conversion traps or
// saturates.  A checksum rather than a printout per operation, because what
// matters is that some difference shows, and one line per case would be
// thousands.
package main

import (
	"fmt"
	"math"
	"math/bits"
)

func mix(h uint64, v uint64) uint64 { return (h ^ v) * 1099511628211 }

func main() {
	i32 := []int32{0, 1, -1, 2, -2, 7, -7, math.MaxInt32, math.MinInt32, 65535, -65536}
	i64 := []int64{0, 1, -1, 3, -3, math.MaxInt64, math.MinInt64, 1 << 32, -(1 << 32)}
	sh := []uint{0, 1, 7, 31, 32, 33, 63, 64, 65}

	h := uint64(1469598103934665603)
	for _, a := range i32 {
		for _, b := range i32 {
			h = mix(h, uint64(uint32(a+b)))
			h = mix(h, uint64(uint32(a-b)))
			h = mix(h, uint64(uint32(a*b)))
			if b != 0 && !(a == math.MinInt32 && b == -1) {
				h = mix(h, uint64(uint32(a/b)))
				h = mix(h, uint64(uint32(a%b)))
				h = mix(h, uint64(uint32(a)/uint32(b)))
				h = mix(h, uint64(uint32(a)%uint32(b)))
			}
		}
		for _, s := range sh {
			h = mix(h, uint64(uint32(a)<<(s&31)))
			h = mix(h, uint64(uint32(a)>>(s&31)))
			h = mix(h, uint64(uint32(a>>(s&31))))
			h = mix(h, uint64(bits.RotateLeft32(uint32(a), int(s))))
		}
		h = mix(h, uint64(bits.LeadingZeros32(uint32(a))))
		h = mix(h, uint64(bits.TrailingZeros32(uint32(a))))
		h = mix(h, uint64(bits.OnesCount32(uint32(a))))
		h = mix(h, uint64(bits.ReverseBytes32(uint32(a))))
	}
	fmt.Printf("i32 %016x\n", h)

	h = 1469598103934665603
	for _, a := range i64 {
		for _, b := range i64 {
			h = mix(h, uint64(a+b))
			h = mix(h, uint64(a*b))
			if b != 0 && !(a == math.MinInt64 && b == -1) {
				h = mix(h, uint64(a/b))
				h = mix(h, uint64(a%b))
				h = mix(h, uint64(a)/uint64(b))
			}
		}
		for _, s := range sh {
			h = mix(h, uint64(a)<<(s&63))
			h = mix(h, uint64(a)>>(s&63))
			h = mix(h, uint64(a>>(s&63)))
			h = mix(h, bits.RotateLeft64(uint64(a), int(s)))
		}
	}
	fmt.Printf("i64 %016x\n", h)

	fs := []float64{0, math.Copysign(0, -1), 1, -1, 0.5, 1e300, -1e300, 1e-300,
		math.MaxFloat64, math.SmallestNonzeroFloat64, 2147483647.5, -2147483648.5,
		math.Inf(1), math.Inf(-1)}
	h = 1469598103934665603
	for _, a := range fs {
		for _, b := range fs {
			h = mix(h, math.Float64bits(a+b))
			h = mix(h, math.Float64bits(a*b))
			h = mix(h, math.Float64bits(a/b))
			h = mix(h, math.Float64bits(math.Min(a, b)))
			h = mix(h, math.Float64bits(math.Max(a, b)))
		}
		h = mix(h, math.Float64bits(math.Sqrt(a)))
		h = mix(h, math.Float64bits(math.Abs(a)))
		h = mix(h, math.Float64bits(math.Trunc(a)))
		h = mix(h, math.Float64bits(math.Floor(a)))
		h = mix(h, math.Float64bits(math.Ceil(a)))
		h = mix(h, uint64(math.Float32bits(float32(a))))
	}
	fmt.Printf("f64 %016x\n", h)
}
