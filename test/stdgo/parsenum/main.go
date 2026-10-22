// strconv in both directions: ParseInt with base prefixes, ParseFloat, and
// AppendFloat both shortest ('g', -1) and to a fixed precision ('e', 9), with
// a Quote now and then.
//
// strbuild formats integers only; floats are the harder half.  The Go here,
// 1.27, does them with unrounded scaling in internal/strconv, where earlier
// releases used Eisel-Lemire and Ryu: parseFloat64, shortFloat and
// fixedWidthFloat each scale a 64-bit mantissa by a 128-bit power of ten from
// a table with two bits.Mul64.  wasm has no 64x64->128 multiply, so each of
// those is inlined as four i64.mul on 32-bit halves with the carries put back
// by shifts -- 35 i64.mul in shortFloat alone.  So this is dense 64-bit integer
// arithmetic behind a float API, and the inputs are text built at run time so
// none of it can be folded.
package main

import (
	"fmt"
	"math"
	"strconv"
)

func main() {
	const n = 256
	ints := make([]string, n)
	floats := make([]string, n)
	words := make([]string, n)
	x := uint64(0x9e3779b97f4a7c15)
	for i := 0; i < n; i++ {
		x ^= x << 13
		x ^= x >> 7
		x ^= x << 17
		v := int64(x>>20) - 1<<43
		switch i % 3 {
		case 0:
			ints[i] = strconv.FormatInt(v, 10)
		case 1:
			ints[i] = "0x" + strconv.FormatUint(uint64(v)>>4, 16)
		default:
			ints[i] = "-0o" + strconv.FormatInt(v&0xffffff, 8)
		}
		floats[i] = strconv.FormatFloat(float64(v)/float64(i+3), 'g', -1, 64)
		words[i] = string([]byte{'a' + byte(i%26), '\t', byte(x >> 56), '"'})
	}
	var acc uint64
	buf := make([]byte, 0, 64)
	for r := 0; r < 1300; r++ {
		scale := float64(r%97) + 0.5
		for i := 0; i < n; i++ {
			v, err := strconv.ParseInt(ints[i], 0, 64)
			if err != nil {
				panic(err)
			}
			f, err := strconv.ParseFloat(floats[i], 64)
			if err != nil {
				panic(err)
			}
			acc += uint64(v) ^ math.Float64bits(f)
			buf = strconv.AppendFloat(buf[:0], f*scale, 'g', -1, 64)
			acc = acc*31 + uint64(len(buf)) + uint64(buf[len(buf)-1])
			buf = strconv.AppendFloat(buf[:0], f, 'e', 9, 64)
			acc = acc*31 + uint64(buf[3])
			if i&7 == 0 {
				q := strconv.Quote(words[i])
				acc += uint64(len(q))
			}
		}
	}
	fmt.Println("acc =", acc)
}
