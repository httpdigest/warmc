// math/big: Fibonacci by repeated addition, a factorial by repeated
// multiplication by a word, one long division of the two, and every result
// converted to decimal and digested digit by digit.
//
// math/big has assembly for its vector kernels on most targets and none for
// wasm, so the portable Go ones run: addVV_g, mulAddVWW_g, and divWW under the
// division and the decimal conversion, which divides and conquers by powers of
// ten.  They are 64-bit word loops built on math/bits.  wasm has the 64-bit
// add, so Add64 is an add with the carry rebuilt by ands, ors and a shift by
// 63; it has no 64x64->128 multiply, so each Mul64 is inlined as four i64.mul
// on 32-bit halves.  Multiprecision arithmetic is where a native compiler's
// instruction selection shows most, with no call in the inner loops at all.
package main

import (
	"fmt"
	"math/big"
)

func main() {
	var acc uint64
	for r := 0; r < 26; r++ {
		// Fibonacci by repeated addition.
		a, b := big.NewInt(int64(r)), big.NewInt(1)
		for i := 0; i < 12000; i++ {
			a.Add(a, b)
			a, b = b, a
		}
		// Factorial by repeated multiplication by a word.
		f := big.NewInt(1)
		for i := 2; i <= 4000+r; i++ {
			f.Mul(f, big.NewInt(int64(i)))
		}
		// A quotient and a remainder, and both to decimal.
		q, m := new(big.Int).QuoRem(f, b, new(big.Int))
		for _, z := range []*big.Int{b, q, m} {
			s := z.String()
			for i := 0; i < len(s); i++ {
				acc = acc*31 + uint64(s[i]-'0')
			}
			acc += uint64(len(s))
		}
		acc += uint64(f.BitLen())
	}
	fmt.Println("acc =", acc)
}
