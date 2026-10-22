// The math package over a grid of arguments.  Some of these TinyGo lowers to
// single wasm instructions (Sqrt, Floor, Ceil, Trunc, Abs, Copysign, and
// RoundToEven as `f64.nearest`), the rest are Go's own software
// implementations -- polynomial evaluation, range reduction, bit
// manipulation through Float64bits/Float64frombits -- so a wrong rounding
// anywhere in the engine's float lowering shows up in the last digit.
// Results print with %v (shortest round-trip form, so every bit counts) and
// NaN results print as NaN whatever their payload.
package main

import (
	"fmt"
	"math"
	"os"
)

var args = []float64{0, 0.25, 0.5, 0.75, 1, 1.5, 2, 2.5, 3, 4.75, 10, 100,
	1e-5, 12345.678, -0.25, -0.5, -1, -1.5, -2.5, -3.75, -10, -1e6, 7e22, 1e-310}

func init() {
	if len(os.Args) > 1000 {
		args[0] = 3
	}
}

type fn struct {
	name string
	f    func(float64) float64
}

var unary = []fn{
	{"Sqrt", math.Sqrt}, {"Cbrt", math.Cbrt}, {"Exp", math.Exp},
	{"Exp2", math.Exp2}, {"Expm1", math.Expm1}, {"Log", math.Log},
	{"Log2", math.Log2}, {"Log10", math.Log10}, {"Log1p", math.Log1p},
	{"Sin", math.Sin}, {"Cos", math.Cos}, {"Tan", math.Tan},
	{"Asin", math.Asin}, {"Acos", math.Acos}, {"Atan", math.Atan},
	{"Sinh", math.Sinh}, {"Cosh", math.Cosh}, {"Tanh", math.Tanh},
	{"Asinh", math.Asinh}, {"Acosh", math.Acosh}, {"Atanh", math.Atanh},
	{"Floor", math.Floor}, {"Ceil", math.Ceil}, {"Trunc", math.Trunc},
	{"Round", math.Round}, {"RoundToEven", math.RoundToEven},
	{"Abs", math.Abs}, {"Gamma", math.Gamma}, {"Erf", math.Erf},
	{"Erfc", math.Erfc}, {"Erfinv", math.Erfinv}, {"J0", math.J0},
	{"Y0", math.Y0}, {"Logb", math.Logb},
}

type fn2 struct {
	name string
	f    func(float64, float64) float64
}

var binary = []fn2{
	{"Pow", math.Pow}, {"Atan2", math.Atan2}, {"Mod", math.Mod},
	{"Remainder", math.Remainder}, {"Hypot", math.Hypot},
	{"Copysign", math.Copysign}, {"Max", math.Max}, {"Min", math.Min},
	{"Dim", math.Dim}, {"Nextafter", math.Nextafter},
}

func v(f float64) string {
	if math.IsNaN(f) {
		return "NaN"
	}
	if f == 0 && math.Signbit(f) {
		return "-0"
	}
	return fmt.Sprint(f)
}

func main() {
	for _, u := range unary {
		line := u.name + ":"
		for _, a := range args {
			line += " " + v(u.f(a))
		}
		fmt.Println(line)
	}
	bargs := []float64{-2.5, -1, -0.5, 0, 0.5, 1, 2, 3, 10}
	for _, b := range binary {
		fmt.Println(b.name)
		for _, x := range bargs {
			line := " "
			for _, y := range bargs {
				line += " " + v(b.f(x+args[0], y))
			}
			fmt.Println(line)
		}
	}
	fmt.Println("-- Frexp, Ldexp, Modf, Ilogb, Signbit")
	for _, a := range args {
		fr, ex := math.Frexp(a)
		ip, fp := math.Modf(a)
		fmt.Println(v(a), v(fr), ex, v(math.Ldexp(fr, ex)), v(math.Ldexp(a, -1030)),
			v(math.Ldexp(a, 1000)), v(ip), v(fp), math.Signbit(a))
		if a != 0 {
			fmt.Println("  ilogb", math.Ilogb(a))
		}
	}
	fmt.Println("-- Lgamma, Sincos, FMA")
	for _, a := range args {
		lg, sign := math.Lgamma(a + 0.1)
		s, c := math.Sincos(a)
		fmt.Println(v(lg), sign, v(s), v(c), v(math.FMA(a, a, -1)),
			v(math.FMA(a, 0.1, 0.3)))
	}
	fmt.Println("-- bits round trips")
	for _, a := range args {
		b := math.Float64bits(a)
		f32 := float32(a)
		b32 := math.Float32bits(f32)
		fmt.Printf("%016x %v %08x %v\n", b, math.Float64frombits(b^1),
			b32, math.Float32frombits(b32+1))
	}
	fmt.Println("-- special arguments")
	inf := math.Inf(1)
	nan := math.NaN()
	specials := []float64{inf, -inf, nan, math.MaxFloat64, math.SmallestNonzeroFloat64,
		-math.MaxFloat64}
	for _, u := range unary {
		line := u.name + ":"
		for _, a := range specials {
			line += " " + v(u.f(a))
		}
		fmt.Println(line)
	}
	fmt.Println(v(math.Pow(0, -1)), v(math.Pow(-8, 1.0/3)), v(math.Pow(2, 1024)),
		v(math.Pow(-2, 3)), v(math.Pow(nan, 0)), v(math.Pow(1, nan)),
		v(math.Atan2(0, -1)), v(math.Atan2(math.Copysign(0, -1), -1)),
		v(math.Mod(inf, 1)), v(math.Mod(5, inf)), v(math.Hypot(inf, nan)))
	fmt.Println("-- series")
	// Leibniz and Machin for pi, a Newton square root, the harmonic sum.
	pi := 0.0
	for k := 0; k < 2000; k++ {
		t := 4 / float64(2*k+1)
		if k%2 == 1 {
			t = -t
		}
		pi += t
	}
	machin := 4 * (4*math.Atan(1.0/5) - math.Atan(1.0/239))
	r := 2.0 + args[0]
	for i := 0; i < 8; i++ {
		r = (r + 2/r) / 2
		fmt.Println("newton", i, v(r))
	}
	h := 0.0
	for i := 1; i <= 5000; i++ {
		h += 1 / float64(i)
	}
	fmt.Println(v(pi), v(machin), v(h), v(h-math.Log(5000)))
	for d := 0; d <= 360; d += 15 {
		rad := float64(d) * math.Pi / 180
		fmt.Println(d, v(math.Sin(rad)), v(math.Cos(rad)), v(math.Tan(rad)))
	}
}
