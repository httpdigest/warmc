// Arbitrary precision arithmetic with math/big.  Factorials, Fibonacci and
// powers far past 64 bits; division and modulus with negative operands in
// both Go's truncated (Quo/Rem) and Euclidean (Div/Mod) conventions; GCD
// with Bezout coefficients, modular inverse and exponentiation, square
// roots, bit operations and shifts on negative numbers, parsing and printing
// in several bases; exact rationals (harmonic numbers, continued fractions)
// and big.Float at 200 bits for sqrt(2) and e.  math/big's inner loops are
// bits.Mul64/Add64 chains and Knuth division by bits.Div64, so every 64x64
// multiply-high and 128/64 divide in the engine is exercised.  Values are
// printed with String/Text rather than through fmt's formatter interface.
package main

import (
	"fmt"
	"math/big"
	"os"
)

var base int64 = 10

func init() {
	if len(os.Args) > 1000 {
		base = 11
	}
}

func main() {
	fmt.Println("-- factorials and powers")
	f := big.NewInt(1)
	for i := int64(1); i <= 60; i++ {
		f.Mul(f, big.NewInt(i))
		if i%10 == 0 {
			fmt.Println(i, f.String(), f.BitLen())
		}
	}
	fmt.Println(new(big.Int).MulRange(1, 100).String())
	p := new(big.Int).Exp(big.NewInt(3), big.NewInt(200), nil)
	fmt.Println(p.String(), p.Text(16), p.Text(36))
	q := new(big.Int).Exp(big.NewInt(2), big.NewInt(521), nil)
	q.Sub(q, big.NewInt(1)) // a Mersenne prime
	fmt.Println(q.Text(int(base)), q.ProbablyPrime(10), new(big.Int).Add(q, big.NewInt(2)).ProbablyPrime(10))
	fmt.Println("-- fibonacci")
	a, b := big.NewInt(0), big.NewInt(1)
	for i := 0; i <= 300; i++ {
		if i%50 == 0 {
			fmt.Println(i, a.String())
		}
		a.Add(a, b)
		a, b = b, a
	}
	fmt.Println("-- division")
	nums := []string{"123456789012345678901234567890", "-123456789012345678901234567890", "7", "-7", "0"}
	dens := []string{"987654321", "-987654321", "3", "-3", "18446744073709551617"}
	for _, ns := range nums {
		for _, ds := range dens {
			n, _ := new(big.Int).SetString(ns, 10)
			d, _ := new(big.Int).SetString(ds, 10)
			qq, rr := new(big.Int).QuoRem(n, d, new(big.Int))
			dq, mm := new(big.Int).DivMod(n, d, new(big.Int))
			fmt.Println(ns, ds, qq.String(), rr.String(), dq.String(), mm.String())
		}
	}
	fmt.Println("-- gcd, inverse, modexp, sqrt")
	x, _ := new(big.Int).SetString("240043243243243243243240", 10)
	y, _ := new(big.Int).SetString("46327432743274327432743", 10)
	s, t := new(big.Int), new(big.Int)
	g := new(big.Int).GCD(s, t, x, y)
	check := new(big.Int).Add(new(big.Int).Mul(s, x), new(big.Int).Mul(t, y))
	fmt.Println(g.String(), s.String(), t.String(), check.Cmp(g) == 0)
	m := big.NewInt(1000000007)
	for _, v := range []int64{2, 3, 12345, 999999999} {
		inv := new(big.Int).ModInverse(big.NewInt(v), m)
		e := new(big.Int).Exp(big.NewInt(v), big.NewInt(1000000005), m)
		fmt.Println(v, inv.String(), e.String(), inv.Cmp(e) == 0)
	}
	rsaN, _ := new(big.Int).SetString("3233", 10)
	msg := big.NewInt(65)
	enc := new(big.Int).Exp(msg, big.NewInt(17), rsaN)
	dec := new(big.Int).Exp(enc, big.NewInt(2753), rsaN)
	fmt.Println(enc.String(), dec.String())
	sq := new(big.Int).Sqrt(new(big.Int).Exp(big.NewInt(10), big.NewInt(101), nil))
	fmt.Println(sq.String())
	fmt.Println("-- bits")
	v, _ := new(big.Int).SetString("-0x123456789abcdef0123456789", 0)
	fmt.Println(v.String(), v.Text(2)[:20], v.Sign(), v.BitLen(), v.Bit(3), v.TrailingZeroBits())
	fmt.Println(new(big.Int).Lsh(v, 70).String(), new(big.Int).Rsh(v, 13).String())
	w := big.NewInt(0xff00ff)
	fmt.Println(new(big.Int).And(v, w).String(), new(big.Int).Or(v, w).String(),
		new(big.Int).Xor(v, w).String(), new(big.Int).AndNot(v, w).String(), new(big.Int).Not(v).String())
	fmt.Println(v.Cmp(w), w.Cmp(v), v.CmpAbs(w), new(big.Int).Abs(v).String(), new(big.Int).Neg(w).String())
	for _, sv := range []string{"0b1011", "0o777", "0x_ff_ff", "-12_345", "zz", "1e5"} {
		n, ok := new(big.Int).SetString(sv, 0)
		if ok {
			fmt.Println(sv, n.String(), n.Int64(), n.IsInt64())
		} else {
			fmt.Println(sv, "invalid")
		}
	}
	big64 := new(big.Int).Lsh(big.NewInt(1), 64)
	fmt.Println(big64.IsUint64(), new(big.Int).Sub(big64, big.NewInt(1)).Uint64(), big64.Bytes(), big.NewInt(-5).Int64())
	fmt.Println("-- rationals")
	h := new(big.Rat)
	for i := int64(1); i <= 30; i++ {
		h.Add(h, big.NewRat(1, i))
		if i%5 == 0 {
			fmt.Println(i, h.String(), h.FloatString(12))
		}
	}
	// Continued fraction for sqrt(2): 1 + 1/(2 + 1/(2 + ...)).
	cf := big.NewRat(1, 1)
	for i := 0; i < 20; i++ {
		cf.Add(big.NewRat(2, 1), new(big.Rat).Inv(cf))
		cf.Sub(cf, big.NewRat(1, 1))
	}
	fmt.Println(cf.String(), cf.FloatString(20))
	r1, _ := new(big.Rat).SetString("-3/12")
	r2, _ := new(big.Rat).SetString("0.125")
	fmt.Println(r1.String(), r2.String(), new(big.Rat).Mul(r1, r2).String(), new(big.Rat).Quo(r1, r2).String(),
		r1.Cmp(r2), r1.Num().String(), r1.Denom().String())
	fmt.Println("-- floats")
	two := new(big.Float).SetPrec(200).SetInt64(2)
	root := new(big.Float).SetPrec(200).Sqrt(two)
	fmt.Println(root.Text('g', 60))
	e := new(big.Float).SetPrec(200).SetInt64(1)
	term := new(big.Float).SetPrec(200).SetInt64(1)
	for i := int64(1); i < 60; i++ {
		term.Quo(term, new(big.Float).SetInt64(i))
		e.Add(e, term)
	}
	fmt.Println(e.Text('g', 60), e.Text('e', 20))
	third := new(big.Float).SetPrec(100).Quo(big.NewFloat(1), big.NewFloat(3))
	f64, acc := third.Float64()
	fmt.Println(third.Text('f', 35), f64, acc, third.MinPrec(), third.Prec())
	pf, _, _ := big.ParseFloat("1.5e1000", 10, 64, big.ToNearestEven)
	fmt.Println(pf.Text('e', 10), pf.MantExp(nil))
}
