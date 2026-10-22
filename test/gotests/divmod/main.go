// Integer division and remainder at every width with negative operands.  Go
// truncates toward zero and the remainder takes the sign of the dividend;
// the most negative value divided by -1 is itself with remainder 0 rather
// than a trap, so TinyGo guards wasm's `i32.div_s`/`i64.div_s` with a check
// that an engine must keep.  Division by constants is strength-reduced by
// LLVM to multiply-high sequences and by powers of two to biased shifts,
// both of which have to round negative dividends toward zero; division by a
// run-time divisor stays a real divide.  Also floor/Euclidean helpers built
// from them, and a digit-sum and base conversion loop driven by `/` and `%`.
package main

import (
	"fmt"
	"os"
)

var dividends = []int64{0, 1, -1, 2, -2, 3, -3, 7, -7, 8, -8, 9, -9, 100, -100,
	127, -128, 255, 32767, -32768, 65535, 1<<31 - 1, -1 << 31, 1<<32 + 5,
	-(1<<32 + 5), 1<<62 + 3, 1<<63 - 1, -1 << 63, 0x7edcba9876543210,
	-0x123456789abcdef}

var divisors = []int64{1, -1, 2, -2, 3, -3, 4, -4, 7, -7, 8, 10, -10, 16,
	127, -128, 255, 1000, 65536, 1<<31 - 1, -1 << 31, 1<<40 + 1, 1<<63 - 1,
	-1 << 63}

func init() {
	if len(os.Args) > 1000 {
		dividends[0], divisors[0] = 5, 5
	}
}

type signed interface {
	~int8 | ~int16 | ~int32 | ~int64 | ~int
}
type unsigned interface {
	~uint8 | ~uint16 | ~uint32 | ~uint64 | ~uint | ~uintptr
}

//go:noinline
func qr[T signed | unsigned](a, b T) string {
	if b == 0 {
		return "div0"
	}
	return fmt.Sprint(a/b, ",", a%b)
}

// Floor division and modulo (Python semantics) from Go's truncating ones.
//
//go:noinline
func floorDivMod(a, b int64) (int64, int64) {
	q, r := a/b, a%b
	if r != 0 && (r < 0) != (b < 0) {
		q--
		r += b
	}
	return q, r
}

// Euclidean: the remainder is never negative.
//
//go:noinline
func euclid(a, b int64) (int64, int64) {
	q, r := a/b, a%b
	if r < 0 {
		if b > 0 {
			q--
			r += b
		} else {
			q++
			r -= b
		}
	}
	return q, r
}

// Division by constants: LLVM turns each of these into a multiply-high and
// shift sequence, or a biased shift for the powers of two.
//
//go:noinline
func byConst32(x int32) string {
	return fmt.Sprint(x/2, " ", x%2, " ", x/3, " ", x%3, " ", x/5, " ", x%5, " ",
		x/7, " ", x%7, " ", x/-7, " ", x%-7, " ", x/16, " ", x%16, " ",
		x/100, " ", x%100, " ", x/1000000007, " ", x%1000000007, " ",
		x/(-1<<31), " ", x/-1)
}

//go:noinline
func byConstU32(x uint32) string {
	return fmt.Sprint(x/2, " ", x%2, " ", x/3, " ", x%3, " ", x/7, " ", x%7, " ",
		x/10, " ", x%10, " ", x/16, " ", x%16, " ", x/641, " ", x%641, " ",
		x/0x80000001, " ", x%0x80000001)
}

//go:noinline
func byConst64(x int64) string {
	return fmt.Sprint(x/2, " ", x%2, " ", x/3, " ", x%3, " ", x/7, " ", x%7, " ",
		x/-9, " ", x%-9, " ", x/10, " ", x%10, " ", x/1024, " ", x%1024, " ",
		x/1000000007, " ", x%1000000007, " ", x/(1<<62), " ", x%(1<<62))
}

//go:noinline
func byConstU64(x uint64) string {
	return fmt.Sprint(x/3, " ", x%3, " ", x/7, " ", x%7, " ", x/10, " ", x%10,
		" ", x/1000000007, " ", x%1000000007, " ", x/(1<<63+5), " ",
		x%(1<<63+5), " ", x/4096, " ", x%4096)
}

//go:noinline
func byConst8(x int8, y uint8, z int16, w uint16) string {
	return fmt.Sprint(x/3, " ", x%3, " ", x/-4, " ", x%-4, " ", y/3, " ", y%3,
		" ", y/16, " ", z/7, " ", z%7, " ", z/256, " ", w/7, " ", w%7, " ",
		w/1000)
}

//go:noinline
func digits(x uint64, base uint64) string {
	if x == 0 {
		return "0"
	}
	const alpha = "0123456789abcdefghijklmnopqrstuvwxyz"
	var buf [64]byte
	i := len(buf)
	for x > 0 {
		i--
		buf[i] = alpha[x%base]
		x /= base
	}
	return string(buf[i:])
}

//go:noinline
func digitSum(x int64) int64 {
	s := int64(0)
	for x != 0 {
		s += x % 10
		x /= 10
	}
	return s
}

//go:noinline
func gcd(a, b int64) int64 {
	for b != 0 {
		a, b = b, a%b
	}
	if a < 0 {
		return -a
	}
	return a
}

func main() {
	fmt.Println("-- int64 / and %")
	for _, a := range dividends {
		line := fmt.Sprint(a, ":")
		for _, b := range divisors {
			line += " " + qr(a, b)
		}
		fmt.Println(line)
	}
	fmt.Println("-- uint64 / and %")
	for _, a := range dividends {
		line := fmt.Sprint(uint64(a), ":")
		for _, b := range divisors {
			line += " " + qr(uint64(a), uint64(b))
		}
		fmt.Println(line)
	}
	fmt.Println("-- int32, uint32, int16, uint16, int8, uint8, int, uintptr")
	for _, a := range dividends {
		for _, b := range divisors[:12] {
			fmt.Println(int32(a), int32(b), qr(int32(a), int32(b)),
				qr(uint32(a), uint32(b)), qr(int16(a), int16(b)),
				qr(uint16(a), uint16(b)), qr(int8(a), int8(b)),
				qr(uint8(a), uint8(b)), qr(int(a), int(b)),
				qr(uintptr(a), uintptr(b)))
		}
	}
	fmt.Println("-- most negative / -1")
	m1 := divisors[1]
	fmt.Println(qr(int8(-128), int8(m1)), qr(int16(-32768), int16(m1)),
		qr(int32(-1<<31), int32(m1)), qr(int64(-1<<63), m1),
		qr(int(dividends[22]), int(m1)))
	fmt.Println("-- floor and euclidean")
	for _, a := range dividends[:21] {
		for _, b := range []int64{3, -3, 7, -7, 10} {
			fq, fr := floorDivMod(a, b+divisors[0]-1)
			eq, er := euclid(a, b+divisors[0]-1)
			fmt.Println(a, b, fq, fr, eq, er)
		}
	}
	fmt.Println("-- by constant")
	for _, a := range dividends {
		fmt.Println(a, "|", byConst32(int32(a)), "|", byConstU32(uint32(a)))
		fmt.Println(a, "|", byConst64(a), "|", byConstU64(uint64(a)))
		fmt.Println(a, "|", byConst8(int8(a), uint8(a), int16(a), uint16(a)))
	}
	for x := int32(-40); x <= 40; x++ {
		fmt.Println(x, byConst32(x+int32(divisors[0])-1))
	}
	fmt.Println("-- digits")
	for _, a := range dividends {
		u := uint64(a)
		fmt.Println(digits(u, 2), digits(u, 7), digits(u, 10), digits(u, 16),
			digits(u, 36), digitSum(a))
	}
	fmt.Println("-- gcd")
	for _, a := range dividends[:24] {
		for _, b := range divisors[:16] {
			fmt.Print(gcd(a, b), " ")
		}
		fmt.Println()
	}
}
