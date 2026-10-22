// Floating-point formatting and parsing: strconv.FormatFloat in every format
// ('e', 'E', 'f', 'g', 'G', 'b', 'x', 'X') at precisions -1 through 17 and
// both bit sizes; shortest round-trip output for awkward values (0.1, 1/3,
// subnormals, the largest and smallest finite values, powers of two and ten
// around the format switch-over); ParseFloat over decimal, exponent,
// hex-float, underscore, infinity and NaN spellings, with rounding to
// float32, halfway cases, overflow to ±Inf with ErrRange and underflow to
// zero; fmt's %v, %g, %e, %f, %x for float32 and float64; negative zero; and
// float32 values that only print correctly if they were really rounded to
// float32.
//
// FormatFloat's shortest mode is Ryu, which is 64x64->128-bit multiplication
// done with math/bits, and ParseFloat uses the Eisel-Lemire fast path with
// its own 128-bit products, so this exercises a lot of wide integer
// arithmetic under the float formatting.
package main

import (
	"errors"
	"fmt"
	"math"
	"strconv"
)

func main() {
	vals := []float64{
		0, math.Copysign(0, -1), 1, -1, 0.1, 0.2, 0.3, 0.1 + 0.2, 1.0 / 3, 2.0 / 3, 100, 1e6, 1e20, 1e21, 1e-6, 1e-7,
		123456789, 1234567890123456789, 0.000123456, 9.999999999999999e22, 5e-324, 2.2250738585072014e-308,
		2.2250738585072009e-308, math.MaxFloat64, math.SmallestNonzeroFloat64, math.Pi, math.E, -math.Sqrt2,
		1 << 53, 1<<53 + 1, 1 << 63, 0.5, 0.25, 0.125, 1.5, 2.5, 3.5, -2.5, 1e23, 8.41e21, 4.35, 0.035,
		math.Inf(1), math.Inf(-1), math.NaN(),
	}
	for _, v := range vals {
		fmt.Printf("%-24s e=%s f=%s g=%s G=%s b=%s x=%s\n",
			strconv.FormatFloat(v, 'g', -1, 64),
			strconv.FormatFloat(v, 'e', -1, 64), strconv.FormatFloat(v, 'f', -1, 64),
			strconv.FormatFloat(v, 'g', 6, 64), strconv.FormatFloat(v, 'G', 3, 64),
			strconv.FormatFloat(v, 'b', -1, 64), strconv.FormatFloat(v, 'x', -1, 64))
	}

	// Precision sweep.
	for _, v := range []float64{math.Pi, 2.0 / 3, 1e-10, 12345.6789, 0.5, 0.05, 1e15 + 0.3} {
		for prec := 0; prec <= 17; prec += 1 {
			fmt.Printf("prec %2d: e=%s f=%s g=%s x=%s\n", prec,
				strconv.FormatFloat(v, 'e', prec, 64), strconv.FormatFloat(v, 'f', prec, 64),
				strconv.FormatFloat(v, 'g', prec, 64), strconv.FormatFloat(v, 'x', prec, 64))
		}
	}

	// float32.
	f32s := []float32{0.1, 1.0 / 3, 16777216, 16777217, 3.4028235e38, 1.4e-45, 1.17549435e-38, 0.3, 100.1, -7.25}
	for _, f := range f32s {
		fmt.Printf("float32 %s | as64 %s | %%v=%v %%g=%g %%e=%e %%.3f=%.3f %%x=%x bits=%08x\n",
			strconv.FormatFloat(float64(f), 'g', -1, 32), strconv.FormatFloat(float64(f), 'g', -1, 64),
			f, f, f, f, f, math.Float32bits(f))
	}

	// ParseFloat.
	inputs := []string{
		"0", "-0", "1", "+1", "1.", ".5", "-.5", "1e10", "1E-10", "1e+308", "1e309", "-1e309", "1e-400", "4.9e-324", "2.4e-324", "2.5e-324",
		"0.1", "0.30000000000000004", "123456789012345678901234567890", "1_000.5", "0x1p-2", "0x1.8p1", "0X.8P+4", "0x1p", "0x1.fffffffffffffp1023",
		"0x1.fffffffffffff8p1023", "inf", "+Inf", "-infinity", "INFINITY", "nan", "NaN", "nAn", "1e", "e1", "", " 1", "1 ", "1_", "0x_1p0",
		"9007199254740993", "9007199254740992.5", "2.2250738585072011e-308", "1.7976931348623157e308", "1.7976931348623159e308",
		"0.000000000000000000000000000000000000000000001", "3.4028236e38", "1e-46", "16777217",
	}
	for _, s := range inputs {
		v64, err64 := strconv.ParseFloat(s, 64)
		v32, err32 := strconv.ParseFloat(s, 32)
		r := ""
		if errors.Is(err64, strconv.ErrRange) {
			r = " (range)"
		}
		fmt.Printf("ParseFloat(%q) 64=%v err=%v%s | 32=%v err=%v\n", s, v64, err64, r, float32(v32), err32)
	}

	// Round trip: shortest formatting re-parses to the same bits.
	bits := uint64(0x3FF0000000000000)
	mismatches := 0
	totalLen := 0
	for i := 0; i < 3000; i++ {
		bits = bits*6364136223846793005 + 1442695040888963407
		f := math.Float64frombits(bits)
		if math.IsNaN(f) || math.IsInf(f, 0) {
			continue
		}
		s := strconv.FormatFloat(f, 'g', -1, 64)
		back, err := strconv.ParseFloat(s, 64)
		if err != nil || math.Float64bits(back) != math.Float64bits(f) {
			mismatches++
		}
		totalLen += len(s)
		if i%300 == 0 {
			fmt.Printf("rt %016x -> %s\n", math.Float64bits(f), s)
		}
	}
	fmt.Println("round trip mismatches:", mismatches, "total length:", totalLen)
	bits32 := uint32(0x3f800000)
	mm32 := 0
	for i := 0; i < 3000; i++ {
		bits32 = bits32*1664525 + 1013904223
		f := math.Float32frombits(bits32)
		if f != f || math.IsInf(float64(f), 0) {
			continue
		}
		s := strconv.FormatFloat(float64(f), 'g', -1, 32)
		back, err := strconv.ParseFloat(s, 32)
		if err != nil || math.Float32bits(float32(back)) != math.Float32bits(f) {
			mm32++
		}
		if i%300 == 0 {
			fmt.Printf("rt32 %08x -> %s\n", math.Float32bits(f), s)
		}
	}
	fmt.Println("float32 round trip mismatches:", mm32)

	// fmt verbs over floats, widths and flags.
	for _, v := range []float64{0, -0.0, 3.14159, -2.5e-8, 6.02214076e23, 1e100} {
		fmt.Printf("[%v] [%8.3f] [%-10.2e] [%+g] [% .4G] [%010.4f] [%#g] [%#.0f] [%x] [%.2X]\n", v, v, v, v, v, v, v, v, v, v)
	}
	negz := math.Copysign(0, -1)
	fmt.Println("negative zero:", negz, 1/negz, math.Signbit(negz), negz == 0, fmt.Sprintf("%+.1f %v", negz, -negz))
	fmt.Printf("special: %v %v %v %5.2f %-6v| %+v %e\n", math.Inf(1), math.Inf(-1), math.NaN(), math.Inf(1), math.NaN(), math.Inf(1), math.NaN())

	// Arithmetic results that depend on correct rounding.
	a, b := 0.1, 0.2
	fmt.Println("0.1+0.2 =", a+b, a+b == 0.3)
	fmt.Println("float32(0.1)+float32(0.2) =", float32(a)+float32(b))
	x32 := float32(16777216)
	x32 += 1
	fmt.Println("float32 16777216+1 =", x32)
	var acc float64
	for i := 1; i <= 1000; i++ {
		acc += 1 / float64(i)
	}
	fmt.Println("harmonic(1000) =", acc, strconv.FormatFloat(acc, 'e', 20, 64))
	var acc32 float32
	for i := 1; i <= 1000; i++ {
		acc32 += 1 / float32(i)
	}
	fmt.Println("harmonic32(1000) =", acc32)
	f1, f2, f3, f4 := 1e18, float32(-2147483520), 255.9, -7.9
	fmt.Println("int conversions:", int64(f1), int32(f2), uint8(f3), int(f4), int(-f4), int64(f4*1e15), uint32(f3*1e7))
	fmt.Println("float<->int big:", float64(int64(1)<<62+1), float32(int32(16777217)), float64(uint64(1)<<63+1025))
}
