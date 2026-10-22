// IEEE 754 arithmetic as wasm defines it, computed at run time from values
// the Go compiler cannot fold: a*b+c rounded twice (wasm has no fused
// multiply-add, so an engine that fuses changes the low bits), float32
// arithmetic rounded to single precision after every operation, subnormal
// inputs and results (no flush-to-zero), signed zeros through every
// operation, NaN propagation and comparisons, division by zero, sqrt of
// negatives, conversions between float32 and float64 (rounding, subnormals,
// infinities), int64/uint64/int32/uint32 to float conversions near 2^53,
// 2^63 and 2^64 where rounding to nearest-even matters, float to integer
// truncation of in-range values and (separately) of NaN, infinities and
// out-of-range values, and floor/ceil/trunc/round-to-even on values next to
// integers.
//
// Every result is printed as its exact bit pattern next to %v, so a wrong
// rounding in the last place is visible.
package main

import (
	"fmt"
	"math"
)

//go:noinline
func f64(x float64) float64 { return x }

//go:noinline
func f32(x float32) float32 { return x }

//go:noinline
func mulAdd(a, b, c float64) float64 { return a*b + c }

//go:noinline
func mulSub(a, b, c float64) float64 { return a*b - c }

//go:noinline
func mulAdd32(a, b, c float32) float32 { return a*b + c }

//go:noinline
func dot(xs, ys []float64) float64 {
	s := 0.0
	for i := range xs {
		s += xs[i] * ys[i]
	}
	return s
}

func b64(x float64) string { return fmt.Sprintf("%v(%016x)", x, math.Float64bits(x)) }
func b32(x float32) string { return fmt.Sprintf("%v(%08x)", x, math.Float32bits(x)) }

func main() {
	// Unfused multiply-add: these differ from math.FMA in the last bits.
	cases := [][3]float64{
		{0.1, 10, -1},
		{1 + 0x1p-52, 1 - 0x1p-52, -1},
		{1e16, 1e16 + 1, -1e32},
		{3.0000000000000004, 3.0000000000000004, -9},
		{0x1.fffffffffffffp-1, 0x1.0000000000001p0, -1},
		{1.1, 1.1, -1.21},
		{math.Pi, math.E, -8.539734222673566},
	}
	for _, c := range cases {
		a, b, cc := f64(c[0]), f64(c[1]), f64(c[2])
		fmt.Printf("a*b+c=%s a*b-(-c)=%s fma=%s\n", b64(mulAdd(a, b, cc)), b64(mulSub(a, b, -cc)), b64(math.FMA(a, b, cc)))
	}
	xs := []float64{1e10, 1, -1e10, 0.1, 0.2, 0.3, 1e-10, 3}
	ys := []float64{1e10, 1, 1e10, 0.1, 0.2, -0.3, 1e10, 1.0 / 3}
	fmt.Println("dot", b64(dot(xs, ys)))
	fmt.Println("float32 mulAdd", b32(mulAdd32(f32(0.1), f32(10), f32(-1))), b32(mulAdd32(f32(1+0x1p-23), f32(1-0x1p-23), f32(-1))))

	// float32 rounding after every operation.
	a32, b32v, c32 := f32(16777216), f32(1), f32(0.1)
	fmt.Println("f32 16777216+1", b32(a32+b32v), "+1+1", b32(a32+b32v+b32v), "(+2)", b32(a32+(b32v+b32v)))
	fmt.Println("f32 0.1*3", b32(c32*3), "0.1+0.1+0.1", b32(c32+c32+c32), "as f64", b64(float64(c32)*3))
	sum32 := f32(0)
	for i := 0; i < 1000; i++ {
		sum32 += c32
	}
	fmt.Println("f32 sum of 1000 x 0.1", b32(sum32))
	q := f32(1) / f32(3)
	fmt.Println("f32 1/3", b32(q), "*3", b32(q*3), "1/3 in f64", b64(float64(f32(1))/3))
	big := f32(3e38)
	fmt.Println("f32 overflow", b32(big*f32(2)), b32(-big*f32(2)), "f32 max+ulp", b32(f32(math.MaxFloat32)+f32(1e31)))
	// Double rounding trap: f64 result then rounded to f32 vs direct f32 op.
	x, y := f32(1.0000001), f32(1.0000002)
	fmt.Println("f32 mul", b32(x*y), "via f64", b32(float32(float64(x)*float64(y))))

	// Subnormals.
	tiny := f64(math.SmallestNonzeroFloat64)
	minNorm := f64(0x1p-1022)
	fmt.Println("subnormal", b64(tiny), b64(tiny*2), b64(tiny/2), b64(minNorm/2), b64(minNorm-tiny), b64(minNorm*0.5*2))
	fmt.Println("subnormal f32", b32(f32(math.SmallestNonzeroFloat32)), b32(f32(0x1p-126)/f32(4)), b32(f32(1e-45)*f32(3)))
	fmt.Println("subnormal add", b64(f64(3e-320)+f64(4e-320)), "mul to subnormal", b64(f64(1e-160)*f64(1e-160)), "underflow", b64(f64(1e-200)*f64(1e-200)))

	// Signed zeros.
	pz, nz := f64(0), f64(math.Copysign(0, -1))
	fmt.Println("zeros", b64(pz+nz), b64(nz+nz), b64(nz-pz), b64(pz-pz), b64(nz*f64(5)), b64(pz*f64(-5)), b64(nz/f64(-3)), b64(-pz))
	fmt.Println("zero compare", pz == nz, pz < nz, 1/pz, 1/nz, math.Signbit(nz*nz))
	fmt.Println("f32 zeros", b32(f32(0)*f32(-1)), b32(-f32(0)), b32(f32(float32(nz))))

	// NaN.
	nan := f64(math.NaN())
	inf := f64(math.Inf(1))
	fmt.Println("nan compares", nan == nan, nan != nan, nan < 1, nan > 1, nan <= nan, !(nan >= 0))
	fmt.Println("nan ops are nan", math.IsNaN(nan+1), math.IsNaN(nan*0), math.IsNaN(inf-inf), math.IsNaN(inf*0), math.IsNaN(pz/pz), math.IsNaN(math.Sqrt(f64(-1))))
	fmt.Println("inf ops", b64(inf+1), b64(inf*-1), b64(f64(1)/pz), b64(f64(-1)/pz), b64(f64(1)/nz), b64(inf/inf*0+1))
	// Sign of NaN through negation and abs (bit operations, so defined).
	negNaN := -nan
	fmt.Println("nan sign bits", math.Signbit(nan) != math.Signbit(negNaN), math.Signbit(math.Abs(negNaN)), math.Signbit(math.Copysign(nan, -1)))
	m := map[bool]int{}
	for _, v := range []float64{1, nan, 3, nan, -1} {
		m[v < 2]++
	}
	fmt.Println("nan in comparisons count", m[true], m[false])

	// Conversions float64 <-> float32.
	for _, v := range []float64{0.1, 1.0 / 3, 16777217, 16777219, 3.4028235677973366e38, 3.4028236e38, 1e39, 1.401298464324817e-45, 7e-46, 1e-46, -2.5, math.Inf(-1)} {
		fv := float32(f64(v))
		fmt.Printf("f64->f32 %v -> %s -> back %s\n", v, b32(fv), b64(float64(fv)))
	}

	// Integer -> float conversions with rounding.
	i64s := []int64{1<<53 - 1, 1 << 53, 1<<53 + 1, 1<<53 + 2, 1<<53 + 3, 1<<62 + 1<<9, 1<<62 + 1<<9 + 1, math.MaxInt64, math.MinInt64, math.MinInt64 + 1, -(1<<53 + 1)}
	for _, v := range i64s {
		vv := v + int64(f64(0))
		fmt.Printf("int64 %d -> f64 %s f32 %s\n", vv, b64(float64(vv)), b32(float32(vv)))
	}
	u64s := []uint64{1 << 63, 1<<63 + 1, 1<<63 + 1<<10, 1<<63 + 1<<10 + 1, 1<<63 + 3<<10, math.MaxUint64, math.MaxUint64 - 1<<10, 1<<64 - 1<<11, 16777217, 1<<24 + 3}
	for _, v := range u64s {
		vv := v + uint64(f64(0))
		fmt.Printf("uint64 %d -> f64 %s f32 %s\n", vv, b64(float64(vv)), b32(float32(vv)))
	}
	for _, v := range []int32{math.MaxInt32, math.MinInt32, 16777217, -16777217} {
		vv := v + int32(f64(0))
		fmt.Printf("int32 %d -> f64 %v f32 %s\n", vv, float64(vv), b32(float32(vv)))
	}
	for _, v := range []uint32{math.MaxUint32, 1 << 31, 1<<31 + 1, 16777219} {
		vv := v + uint32(f64(0))
		fmt.Printf("uint32 %d -> f64 %v f32 %s\n", vv, float64(vv), b32(float32(vv)))
	}

	// Float -> integer truncation (in-range only).
	for _, v := range []float64{0.9, -0.9, 1.5, -1.5, 2147483647.9, -2147483648.9, 9007199254740993, 9.223372036854775e18, -9.223372036854775e18, 1.8446744073709550e19, 4294967295.5} {
		fv := f64(v)
		line := fmt.Sprintf("trunc %v:", v)
		if fv > -9.223372036854777e18 && fv < 9.223372036854775807e18 {
			line += fmt.Sprintf(" int64=%d", int64(fv))
		}
		if fv >= 0 && fv < 1.8446744073709552e19 {
			line += fmt.Sprintf(" uint64=%d", uint64(fv))
		}
		if fv > -2147483649 && fv < 2147483648 {
			line += fmt.Sprintf(" int32=%d", int32(fv))
		}
		if fv >= 0 && fv < 4294967296 {
			line += fmt.Sprintf(" uint32=%d", uint32(fv))
		}
		fmt.Println(line)
	}
	for _, v := range []float32{0.99999994, -16777215.5, 2147483520, -2147483648, 4294967040} {
		fv := f32(v)
		line := fmt.Sprintf("trunc f32 %v: int64=%d uint64=%d", v, int64(fv), uint64(math.Abs(float64(fv))))
		if fv >= -2147483648 && fv < 2147483648 {
			line += fmt.Sprintf(" int32=%d", int32(fv))
		}
		fmt.Println(line)
	}

	// Out-of-range conversions are implementation-defined in Go, but the wasm
	// Go emits for them is not: the saturating truncations and the wrap to a
	// narrower type are fixed instructions, so both engines must agree.
	for _, v := range []float64{math.NaN(), math.Inf(1), math.Inf(-1), 1e19, -1e19, 1.8446744073709552e19, 1e30, -1, -0.5, 4294967296, 3e9, -3e9} {
		fv := f64(v)
		fmt.Printf("out of range %v: int64=%d uint64=%d int32=%d uint32=%d int8=%d uint8=%d\n", v, int64(fv), uint64(fv), int32(fv), uint32(fv), int8(fv), uint8(fv))
	}
	for _, v := range []float32{float32(math.NaN()), float32(math.Inf(-1)), 3e38, -5e9, 5e9} {
		fv := f32(v)
		fmt.Printf("out of range f32 %v: int64=%d uint64=%d int32=%d uint32=%d\n", v, int64(fv), uint64(fv), int32(fv), uint32(fv))
	}

	// Rounding functions next to integers.
	for _, v := range []float64{0.5, 1.5, 2.5, -0.5, -1.5, 0.49999999999999994, 4503599627370495.5, 4503599627370496.5, -4503599627370495.5, 1e300, -0.0000001, 0.9999999999999999} {
		fv := f64(v)
		fmt.Printf("%v: floor=%v ceil=%v trunc=%v rte=%v round=%v\n", v, math.Floor(fv), math.Ceil(fv), math.Trunc(fv), math.RoundToEven(fv), math.Round(fv))
	}

	// Comparison-driven control flow with NaN and zeros.
	vals := []float64{3, nan, -0.0, 0, -1, inf, -inf, nan, 2.5}
	lt, eq, gt, un := 0, 0, 0, 0
	for i := range vals {
		for j := range vals {
			a, b := f64(vals[i]), f64(vals[j])
			switch {
			case a < b:
				lt++
			case a == b:
				eq++
			case a > b:
				gt++
			default:
				un++
			}
		}
	}
	fmt.Println("pairwise lt/eq/gt/unordered", lt, eq, gt, un)
	fmt.Println("builtin min/max with -0", b64(min(pz, nz)), b64(max(nz, pz)), b64(min(nz, pz)), b64(max(pz, nz)))

	// Kahan vs naive summation.
	naive, kahan, comp := 0.0, 0.0, 0.0
	for i := 1; i <= 10000; i++ {
		v := f64(1.0 / float64(i*i))
		naive += v
		yv := v - comp
		t := kahan + yv
		comp = (t - kahan) - yv
		kahan = t
	}
	fmt.Println("naive", b64(naive), "kahan", b64(kahan))
}
