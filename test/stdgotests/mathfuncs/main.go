// math, math/bits and math/cmplx over tables of inputs.
//
// math: Sqrt, Cbrt, Pow (every special case in its doc comment), Exp, Exp2,
// Expm1, Log, Log2, Log10, Log1p, trig and inverse trig, hyperbolics, Atan2
// in all quadrants, Hypot, Floor/Ceil/Trunc/Round/RoundToEven at halves and
// negatives, Mod/Remainder/Modf, Frexp/Ldexp, Float64bits round trips,
// Nextafter, FMA, Gamma/Lgamma, Erf/Erfc/Erfinv, Bessel J0/J1/Y0, Max/Min
// with NaN and signed zeros, Copysign, Signbit, Dim, IsInf/IsNaN.
//
// Several of these lower to single wasm instructions (sqrt, floor, ceil,
// trunc, nearest, abs, copysign, min/max are not: Go handles NaN and -0
// itself), and the rest are pure Go polynomial and table code; results are
// printed with %v (shortest round trip) so every last bit must match.
//
// math/bits: LeadingZeros/TrailingZeros/OnesCount/Len/Reverse/ReverseBytes/
// RotateLeft at every width, Add64/Sub64/Mul64/Div64 with carries, and
// math/cmplx: Abs, Phase, Polar/Rect, Sqrt, Exp, Log, Pow, trig, Inf/NaN.
package main

import (
	"fmt"
	"math"
	"math/bits"
	"math/cmplx"
)

func main() {
	xs := []float64{0, 0.5, 1, 1.5, 2, 2.5, -0.5, -1, -2.5, 3, 10, 100, 1e-10, 1e10, math.Pi, -math.Pi / 3, 0.1, 7.75}
	for _, x := range xs {
		fmt.Printf("x=%-12v sqrt=%-22v cbrt=%-22v exp=%-22v log=%-22v sin=%-22v cos=%-22v tan=%v\n",
			x, math.Sqrt(x), math.Cbrt(x), math.Exp(x), math.Log(x), math.Sin(x), math.Cos(x), math.Tan(x))
		fmt.Printf("   exp2=%v expm1=%v log2=%v log10=%v log1p=%v sinh=%v cosh=%v tanh=%v asinh=%v\n",
			math.Exp2(x), math.Expm1(x), math.Log2(x), math.Log10(x), math.Log1p(x), math.Sinh(x), math.Cosh(x), math.Tanh(x), math.Asinh(x))
		fmt.Printf("   asin=%v acos=%v atan=%v acosh=%v atanh=%v gamma=%v erf=%v erfc=%v j0=%v y0=%v\n",
			math.Asin(x/20), math.Acos(x/20), math.Atan(x), math.Acosh(x), math.Atanh(x/20), math.Gamma(x), math.Erf(x), math.Erfc(x), math.J0(x), math.Y0(x))
		lg, sign := math.Lgamma(x)
		fr, ex := math.Frexp(x)
		ip, fp := math.Modf(x)
		fmt.Printf("   lgamma=%v,%d frexp=%v,%d modf=%v,%v floor=%v ceil=%v trunc=%v round=%v rte=%v\n",
			lg, sign, fr, ex, ip, fp, math.Floor(x), math.Ceil(x), math.Trunc(x), math.Round(x), math.RoundToEven(x))
	}

	// Pow special cases.
	inf, nan, negz := math.Inf(1), math.NaN(), math.Copysign(0, -1)
	pows := [][2]float64{{2, 10}, {2, -2}, {-2, 3}, {-2, 0.5}, {0, -1}, {negz, -1}, {negz, -2}, {0, 0}, {nan, 0}, {1, nan},
		{inf, 1}, {inf, -1}, {-inf, 3}, {-inf, 2}, {-inf, -3}, {0.5, inf}, {2, inf}, {2, -inf}, {-1, inf}, {1, inf}, {10, 308}, {10, 309}, {10, -323}, {10, -324}, {1.0000001, 1e9}}
	for _, p := range pows {
		fmt.Printf("Pow(%v, %v) = %v\n", p[0], p[1], math.Pow(p[0], p[1]))
	}

	// Atan2 quadrants and Hypot.
	for _, yx := range [][2]float64{{1, 1}, {1, -1}, {-1, -1}, {-1, 1}, {0, -1}, {negz, -1}, {0, 0}, {negz, negz}, {inf, inf}, {1, 0}, {3, 4}} {
		fmt.Printf("Atan2(%v,%v)=%v Hypot=%v\n", yx[0], yx[1], math.Atan2(yx[0], yx[1]), math.Hypot(yx[0], yx[1]))
	}

	// Rounding at halves.
	for _, x := range []float64{-2.5, -1.5, -0.5, -0.49999999999999994, 0.49999999999999994, 0.5, 1.5, 2.5, 3.5, 4503599627370495.5, 4503599627370497, -0.0} {
		fmt.Printf("round %v: Round=%v RoundToEven=%v Floor=%v Ceil=%v Trunc=%v\n", x, math.Round(x), math.RoundToEven(x), math.Floor(x), math.Ceil(x), math.Trunc(x))
	}

	// Mod, Remainder.
	for _, p := range [][2]float64{{7, 3}, {-7, 3}, {7, -3}, {-7, -3}, {7.5, 2}, {5, 0}, {inf, 2}, {2, inf}, {1e300, 3}, {0.3, 0.1}} {
		fmt.Printf("Mod(%v,%v)=%v Remainder=%v Dim=%v\n", p[0], p[1], math.Mod(p[0], p[1]), math.Remainder(p[0], p[1]), math.Dim(p[0], p[1]))
	}

	// Max/Min with NaN and signed zeros.
	fmt.Println("Max(0,-0)", math.Max(0, negz), "Min(0,-0)", math.Min(0, negz), 1/math.Min(0, negz), "Max(NaN,1)", math.Max(nan, 1), "Min(-Inf,NaN)", math.Min(math.Inf(-1), nan), "Max(Inf,NaN)", math.Max(inf, nan))
	fmt.Println("builtin max/min", max(1.5, 2.5, -1), min(3, 1, 2), max("b", "a", "c"), 1/min(0.0, negz), min(nan, 1))
	fmt.Println("Copysign", math.Copysign(3, -1), math.Copysign(-3, 1), math.Copysign(nan, -1), "Signbit", math.Signbit(negz), math.Signbit(-nan), "Abs", math.Abs(-inf), math.Abs(negz))

	// Bits and Nextafter.
	for _, x := range []float64{1, -1, 0.1, math.MaxFloat64, math.SmallestNonzeroFloat64, inf, negz} {
		b := math.Float64bits(x)
		fmt.Printf("bits %v = %016x back=%v next(up)=%v next(down)=%v\n", x, b, math.Float64frombits(b), math.Nextafter(x, inf), math.Nextafter(x, -inf))
	}
	fmt.Printf("Float32 bits %08x %08x Nextafter32 %v\n", math.Float32bits(1), math.Float32bits(-0.1), math.Nextafter32(1, 2))
	fmt.Println("NaN bits", math.Float64bits(math.NaN()) == math.Float64bits(math.NaN()), math.IsNaN(math.Float64frombits(0x7ff8000000000001)))
	fmt.Println("Ldexp", math.Ldexp(0.5, 3), math.Ldexp(1, -1074), math.Ldexp(1, -1075), math.Ldexp(1, 1024), math.Ilogb(1000), math.Logb(0.1))
	fmt.Println("FMA", math.FMA(0.1, 10, -1), 0.1*10-1, math.FMA(1e308, 10, -1e308), math.FMA(2, 3, 4))
	fmt.Println("Erfinv", math.Erfinv(0.5), math.Erfinv(-0.9), math.Erfcinv(0.1), "J1", math.J1(2.5), "Jn", math.Jn(3, 4.5), "Yn", math.Yn(2, 3.5))
	fmt.Println("Sincos", fmt.Sprint(math.Sincos(1)), "Pow10", math.Pow10(5), math.Pow10(-400), math.Pow10(400))

	// Accumulated series to amplify any error.
	s := 0.0
	for k := 0; k < 2000; k++ {
		s += math.Sin(float64(k)*0.01) * math.Exp(-float64(k)*0.001) / math.Sqrt(1+float64(k))
	}
	fmt.Println("series", s)

	// math/bits.
	u64s := []uint64{0, 1, 2, 3, 0x80, 0xff, 0x8000000000000000, 0xffffffffffffffff, 0x0123456789abcdef, 1 << 40, 0xf0f0f0f0}
	for _, x := range u64s {
		fmt.Printf("u64 %016x lz=%2d tz=%2d ones=%2d len=%2d rev=%016x revb=%016x rotl7=%016x rotr13=%016x\n",
			x, bits.LeadingZeros64(x), bits.TrailingZeros64(x), bits.OnesCount64(x), bits.Len64(x),
			bits.Reverse64(x), bits.ReverseBytes64(x), bits.RotateLeft64(x, 7), bits.RotateLeft64(x, -13))
		x32 := uint32(x) ^ uint32(x>>32)
		x16 := uint16(x32)
		x8 := uint8(x16)
		fmt.Printf("    u32 %08x lz=%d tz=%d ones=%d rev=%08x revb=%08x rotl=%08x | u16 %04x lz=%d rev=%04x revb=%04x | u8 %02x lz=%d tz=%d rev=%02x ones=%d len=%d\n",
			x32, bits.LeadingZeros32(x32), bits.TrailingZeros32(x32), bits.OnesCount32(x32), bits.Reverse32(x32), bits.ReverseBytes32(x32), bits.RotateLeft32(x32, 5),
			x16, bits.LeadingZeros16(x16), bits.Reverse16(x16), bits.ReverseBytes16(x16), x8, bits.LeadingZeros8(x8), bits.TrailingZeros8(x8), bits.Reverse8(x8), bits.OnesCount8(x8), bits.Len8(x8))
	}
	pairs := [][2]uint64{{1, 2}, {0xffffffffffffffff, 1}, {0xffffffffffffffff, 0xffffffffffffffff}, {0x8000000000000000, 0x8000000000000000}, {0x123456789abcdef0, 0xfedcba9876543210}, {0, 0}}
	for _, p := range pairs {
		sum, c := bits.Add64(p[0], p[1], 0)
		sum1, c1 := bits.Add64(p[0], p[1], 1)
		diff, bw := bits.Sub64(p[0], p[1], 0)
		hi, lo := bits.Mul64(p[0], p[1])
		fmt.Printf("%016x %016x add=%016x,%d add+1=%016x,%d sub=%016x,%d mul=%016x:%016x\n", p[0], p[1], sum, c, sum1, c1, diff, bw, hi, lo)
		if p[1] != 0 && hi < p[1] {
			q, r := bits.Div64(hi, lo, p[1])
			fmt.Printf("    div64(hi:lo / y) q=%016x r=%016x Rem64=%016x\n", q, r, bits.Rem64(hi, lo, p[1]))
		}
	}
	h32, l32 := bits.Mul32(0xffffffff, 0xfffffffe)
	q32, r32 := bits.Div32(1, 5, 7)
	s32, c32 := bits.Add32(0xffffffff, 1, 1)
	fmt.Printf("Mul32 %08x:%08x Div32 %d %d Add32 %d %d UintSize %d\n", h32, l32, q32, r32, s32, c32, bits.UintSize)
	func() {
		defer func() { fmt.Println("Div64 overflow panics:", recover()) }()
		bits.Div64(5, 0, 5)
	}()
	func() {
		defer func() { fmt.Println("Div64 by zero panics:", recover()) }()
		bits.Div64(0, 1, 0)
	}()

	// math/cmplx.
	zs := []complex128{complex(1, 1), complex(-1, 0), complex(0, 1), complex(3, -4), complex(0, 0), complex(-2.5, 1e-3), complex(1e10, 1e-10)}
	for _, z := range zs {
		r, th := cmplx.Polar(z)
		fmt.Printf("z=%v abs=%v phase=%v polar->rect=%v sqrt=%v exp=%v log=%v conj=%v\n",
			z, cmplx.Abs(z), cmplx.Phase(z), cmplx.Rect(r, th), cmplx.Sqrt(z), cmplx.Exp(z), cmplx.Log(z), cmplx.Conj(z))
		fmt.Printf("   sin=%v cos=%v tan=%v asin=%v atanh=%v pow(z,2)=%v pow(z,0.5i)=%v z*z=%v 1/z=%v\n",
			cmplx.Sin(z), cmplx.Cos(z), cmplx.Tan(z), cmplx.Asin(z), cmplx.Atanh(z), cmplx.Pow(z, 2), cmplx.Pow(z, 0.5i), z*z, 1/z)
	}
	fmt.Println("cmplx Inf/NaN", cmplx.Inf(), cmplx.IsInf(cmplx.Inf()), cmplx.NaN(), cmplx.IsNaN(complex(math.NaN(), 1)), cmplx.IsInf(complex(math.NaN(), math.Inf(1))))
	cz := complex(0, 0)
	fmt.Println("complex division by zero", complex(1, 1)/cz, cz/cz, complex(inf, 1)/complex(2, 0), complex(1, 1)/complex(inf, inf))
	z := complex(1, 0)
	for k := 0; k < 12; k++ {
		z *= cmplx.Exp(complex(0, math.Pi/6))
	}
	fmt.Printf("12 rotations by pi/6: %.12f\n", z)
	c64 := complex64(complex(1.5, -2.5))
	fmt.Println("complex64", c64*c64, real(c64), imag(c64), complex64(cmplx.Sqrt(complex128(c64))))
}
