// math/big: Int arithmetic far past 64 bits (factorials to 300!, Fibonacci
// to F(1000), powers, Exp with and without a modulus, GCD with Bézout
// coefficients, ModInverse, ModSqrt, Sqrt, Binomial, DivMod vs QuoRem sign
// rules, bit operations on negatives, SetString/Text in bases 2..62,
// ProbablyPrime on known primes and Carmichael numbers), Rat (exact sums of
// harmonic and Egyptian-fraction series, FloatString, SetString of decimals
// and fractions, comparisons), and Float at fixed precision (Sqrt(2) to 200
// bits, a series for e, Text in every format, rounding modes, accuracy,
// Int/Rat conversions, parsing).
//
// math/big's inner loops (addVV, mulAddVWW, divWW) are pure Go on wasm, built
// on math/bits' 64x64->128 multiply and add-with-carry, which wasm has to
// synthesise from 32-bit halves: a carry dropped anywhere changes digits here.
package main

import (
	"fmt"
	"math"
	"math/big"
	"strings"
)

func main() {
	// Factorials.
	f := big.NewInt(1)
	for i := int64(1); i <= 300; i++ {
		f.Mul(f, big.NewInt(i))
		if i%25 == 0 || i == 20 || i == 21 {
			s := f.String()
			fmt.Printf("%3d! has %3d digits, bits %4d, head %s tail %s\n", i, len(s), f.BitLen(), s[:min(20, len(s))], s[max(0, len(s)-20):])
		}
	}
	fmt.Println("MulRange(1,50) == 50!:", new(big.Int).MulRange(1, 50).String())
	fmt.Println("Binomial(100,50):", new(big.Int).Binomial(100, 50))

	// Fibonacci.
	a, b := big.NewInt(0), big.NewInt(1)
	for i := 0; i < 1000; i++ {
		a.Add(a, b)
		a, b = b, a
		if i%200 == 199 {
			fmt.Printf("F(%d) = %s...%s (%d digits)\n", i+1, a.String()[:15], a.String()[len(a.String())-15:], len(a.String()))
		}
	}
	fmt.Println("F(1000) mod 1e9+7 =", new(big.Int).Mod(a, big.NewInt(1000000007)))

	// Powers and modular exponentiation.
	two := big.NewInt(2)
	p127 := new(big.Int).Sub(new(big.Int).Exp(two, big.NewInt(127), nil), big.NewInt(1))
	fmt.Println("2^127-1 =", p127, "prime:", p127.ProbablyPrime(20))
	fmt.Println("3^1000 mod 2^127-1 =", new(big.Int).Exp(big.NewInt(3), big.NewInt(1000), p127))
	fmt.Println("7^(p-1) mod p (Fermat) =", new(big.Int).Exp(big.NewInt(7), new(big.Int).Sub(p127, big.NewInt(1)), p127))
	fmt.Println("2^-1 mod 2^127-1 via Exp neg:", new(big.Int).Exp(two, big.NewInt(-1), p127))
	big10 := new(big.Int).Exp(big.NewInt(10), big.NewInt(50), nil)
	fmt.Println("10^50 =", big10, "Sqrt =", new(big.Int).Sqrt(big10), "Sqrt(10^50+1) =", new(big.Int).Sqrt(new(big.Int).Add(big10, big.NewInt(1))))

	// Division semantics.
	for _, xy := range [][2]int64{{17, 5}, {-17, 5}, {17, -5}, {-17, -5}, {0, 3}} {
		x, y := big.NewInt(xy[0]), big.NewInt(xy[1])
		q, r := new(big.Int).QuoRem(x, y, new(big.Int))
		d, m := new(big.Int).DivMod(x, y, new(big.Int))
		fmt.Printf("%d/%d QuoRem=(%v,%v) DivMod=(%v,%v) Go=(%d,%d)\n", xy[0], xy[1], q, r, d, m, xy[0]/xy[1], xy[0]%xy[1])
	}
	func() {
		defer func() { fmt.Println("big division by zero panics:", recover()) }()
		new(big.Int).Div(big.NewInt(1), big.NewInt(0))
	}()
	n1, _ := new(big.Int).SetString("123456789012345678901234567890123456789012345678901234567890", 10)
	n2, _ := new(big.Int).SetString("987654321098765432109876543210", 10)
	q, r := new(big.Int).QuoRem(n1, n2, new(big.Int))
	fmt.Println("big quorem", q, r)
	fmt.Println("big product", new(big.Int).Mul(n1, n2))
	fmt.Println("check", new(big.Int).Add(new(big.Int).Mul(q, n2), r).Cmp(n1) == 0)

	// GCD, ModInverse, ModSqrt.
	x, y := new(big.Int), new(big.Int)
	g := new(big.Int).GCD(x, y, big.NewInt(240), big.NewInt(46))
	fmt.Println("GCD(240,46) =", g, "x =", x, "y =", y)
	g2 := new(big.Int).GCD(nil, nil, n1, n2)
	fmt.Println("GCD(n1,n2) =", g2)
	inv := new(big.Int).ModInverse(big.NewInt(3), big.NewInt(11))
	fmt.Println("ModInverse(3,11) =", inv, "none:", new(big.Int).ModInverse(big.NewInt(4), big.NewInt(8)))
	sq := new(big.Int).ModSqrt(big.NewInt(10), big.NewInt(13))
	fmt.Println("ModSqrt(10,13) =", sq, "Jacobi(10,13) =", big.Jacobi(big.NewInt(10), big.NewInt(13)))

	// Bit operations, including on negative numbers (two's complement semantics).
	v := big.NewInt(-12345)
	m := big.NewInt(0xff0f)
	fmt.Println("And", new(big.Int).And(v, m), "Or", new(big.Int).Or(v, m), "Xor", new(big.Int).Xor(v, m), "AndNot", new(big.Int).AndNot(v, m), "Not", new(big.Int).Not(v))
	fmt.Println("Lsh", new(big.Int).Lsh(v, 70), "Rsh", new(big.Int).Rsh(v, 3), new(big.Int).Rsh(big.NewInt(12345), 3))
	fmt.Println("Bit", v.Bit(0), v.Bit(1), v.Bit(100), "TrailingZeroBits", big.NewInt(96).TrailingZeroBits(), "SetBit", new(big.Int).SetBit(big.NewInt(0), 100, 1))
	fmt.Println("Bytes", fmt.Sprintf("%x", big.NewInt(0x1234567).Bytes()), "FillBytes", fmt.Sprintf("%x", big.NewInt(258).FillBytes(make([]byte, 6))))

	// Bases.
	for _, base := range []int{2, 3, 8, 16, 36, 62} {
		s := n2.Text(base)
		back, ok := new(big.Int).SetString(s, base)
		fmt.Printf("base %2d: %s (round trip %v)\n", base, s, ok && back.Cmp(n2) == 0)
	}
	for _, s := range []string{"0x1f", "0b101", "0o17", "017", "1_000_000", "-0x_ff", "zz", ""} {
		z, ok := new(big.Int).SetString(s, 0)
		fmt.Printf("SetString(%q, 0) = %v %v\n", s, z, ok)
	}
	fmt.Printf("fmt verbs on Int: %d %x %X %o %b %+d %08d %s\n", big.NewInt(255), big.NewInt(255), big.NewInt(-255), big.NewInt(8), big.NewInt(5), big.NewInt(3), big.NewInt(-42), big.NewInt(7))

	// Primality.
	for _, s := range []string{"2", "561", "1105", "1729", "2147483647", "2305843009213693951", "18446744073709551557", "18446744073709551559", "170141183460469231731687303715884105727", "170141183460469231731687303715884105729"} {
		z, _ := new(big.Int).SetString(s, 10)
		fmt.Printf("ProbablyPrime(%s) = %v\n", s, z.ProbablyPrime(10))
	}

	// Int64/Uint64/IsInt64 and float conversion.
	big64 := new(big.Int).Lsh(big.NewInt(1), 63)
	fmt.Println("IsInt64", big64.IsInt64(), "IsUint64", big64.IsUint64(), "Uint64", big64.Uint64(), "Int64 wraps", big64.Int64())
	fl, acc := new(big.Int).Exp(big.NewInt(3), big.NewInt(40), nil).Float64()
	fmt.Println("3^40 as float64", fl, acc)

	// Rationals.
	h := new(big.Rat)
	for i := int64(1); i <= 30; i++ {
		h.Add(h, big.NewRat(1, i))
	}
	fmt.Println("H(30) =", h, "≈", h.FloatString(30))
	egypt := new(big.Rat).Add(big.NewRat(1, 2), big.NewRat(1, 3))
	egypt.Add(egypt, big.NewRat(1, 7))
	egypt.Add(egypt, big.NewRat(1, 43))
	egypt.Add(egypt, big.NewRat(1, 1807))
	fmt.Println("Sylvester partial sum", egypt, "1 - sum =", new(big.Rat).Sub(big.NewRat(1, 1), egypt))
	for _, s := range []string{"3/4", "-6/8", "0.125", "1e-3", "2.5e2", "1/0", "abc", "0x10", "-0", "355/113"} {
		r, ok := new(big.Rat).SetString(s)
		if ok {
			fv, exact := r.Float64()
			fmt.Printf("Rat(%q) = %s num=%v den=%v isInt=%v float=%v exact=%v %s\n", s, r.RatString(), r.Num(), r.Denom(), r.IsInt(), fv, exact, r.FloatString(5))
		} else {
			fmt.Printf("Rat(%q) invalid\n", s)
		}
	}
	pi := big.NewRat(355, 113)
	fmt.Println("355/113 cmp 22/7:", pi.Cmp(big.NewRat(22, 7)), "Inv", new(big.Rat).Inv(pi), "Neg", new(big.Rat).Neg(pi), "Mul", new(big.Rat).Mul(pi, pi), "Quo", new(big.Rat).Quo(pi, big.NewRat(-2, 3)))
	ratf := new(big.Rat).SetFloat64(0.1)
	fmt.Println("SetFloat64(0.1) exact =", ratf)

	// Floats at fixed precision.
	sqrt2 := new(big.Float).SetPrec(200).SetInt64(2)
	sqrt2.Sqrt(sqrt2)
	fmt.Println("sqrt(2) @200 =", sqrt2.Text('g', 60))
	sq2 := new(big.Float).SetPrec(200).Mul(sqrt2, sqrt2)
	fmt.Println("sqrt(2)^2 =", sq2.Text('g', 60), "acc", sq2.Acc())
	// e = sum 1/k!
	e := new(big.Float).SetPrec(300).SetInt64(1)
	term := new(big.Float).SetPrec(300).SetInt64(1)
	for k := 1; k < 80; k++ {
		term.Quo(term, new(big.Float).SetPrec(300).SetInt64(int64(k)))
		e.Add(e, term)
	}
	fmt.Println("e @300 =", e.Text('f', 80))
	for _, fmtc := range []byte{'e', 'E', 'f', 'g', 'G', 'b', 'p', 'x'} {
		fmt.Printf("format %c: %s\n", fmtc, sqrt2.Text(fmtc, 20))
	}
	fmt.Printf("fmt verbs on Float: %.10f %g %e %v %10.3f|\n", sqrt2, e, e, big.NewFloat(1.5), big.NewFloat(-2.25))

	// Rounding modes at low precision.
	modes := []big.RoundingMode{big.ToNearestEven, big.ToNearestAway, big.ToZero, big.AwayFromZero, big.ToNegativeInf, big.ToPositiveInf}
	for _, val := range []float64{2.5, 3.5, -2.5, 2.25, -2.75} {
		var parts []string
		for _, mode := range modes {
			z := new(big.Float).SetPrec(2).SetMode(mode).SetFloat64(val)
			parts = append(parts, fmt.Sprintf("%s=%v(%v)", mode, z, z.Acc()))
		}
		fmt.Printf("round %v @2 bits: %s\n", val, strings.Join(parts, " "))
	}

	// Conversions and parsing.
	pf, _, err := big.ParseFloat("1.234567890123456789012345678901234567890e-5", 10, 160, big.ToNearestEven)
	fmt.Println("ParseFloat", pf.Text('e', 40), err)
	pf2, ok := new(big.Float).SetPrec(64).SetString("0x1.8p+3")
	fmt.Println("hex SetString", pf2, ok)
	bi, accI := new(big.Float).SetPrec(100).SetFloat64(-12345.678).Int(nil)
	fmt.Println("Float.Int", bi, accI)
	br, _ := big.NewFloat(0.375).Rat(nil)
	fmt.Println("Float.Rat", br)
	f64, acc64 := sqrt2.Float64()
	fmt.Println("Float64()", f64, acc64, f64 == math.Sqrt2)
	mant := new(big.Float)
	exp := sqrt2.MantExp(mant)
	fmt.Println("MantExp", mant.Text('g', 10), exp, "Sign", sqrt2.Sign(), "IsInt", sqrt2.IsInt(), "MinPrec", big.NewFloat(0.5).MinPrec())
	inf := new(big.Float).SetInf(true)
	fmt.Println("Inf", inf, inf.IsInf(), new(big.Float).Quo(big.NewFloat(1), inf))
	func() {
		defer func() {
			if r := recover(); r != nil {
				_, isNaN := r.(big.ErrNaN)
				fmt.Println("Inf-Inf panics with ErrNaN:", isNaN)
			}
		}()
		new(big.Float).Sub(inf, inf)
	}()

	// Pi by Machin's formula with big.Float.
	prec := uint(256)
	arctanInv := func(x int64) *big.Float {
		// arctan(1/x) = sum (-1)^k / ((2k+1) x^(2k+1))
		sum := new(big.Float).SetPrec(prec)
		xf := new(big.Float).SetPrec(prec).SetInt64(x)
		x2 := new(big.Float).SetPrec(prec).Mul(xf, xf)
		pow := new(big.Float).SetPrec(prec).Quo(big.NewFloat(1).SetPrec(prec), xf)
		for k := int64(0); k < 120; k++ {
			t := new(big.Float).SetPrec(prec).Quo(pow, new(big.Float).SetPrec(prec).SetInt64(2*k+1))
			if k%2 == 0 {
				sum.Add(sum, t)
			} else {
				sum.Sub(sum, t)
			}
			pow.Quo(pow, x2)
		}
		return sum
	}
	pi16 := new(big.Float).SetPrec(prec).Mul(big.NewFloat(16).SetPrec(prec), arctanInv(5))
	pi4 := new(big.Float).SetPrec(prec).Mul(big.NewFloat(4).SetPrec(prec), arctanInv(239))
	piF := new(big.Float).SetPrec(prec).Sub(pi16, pi4)
	fmt.Println("pi =", piF.Text('f', 70))
}
