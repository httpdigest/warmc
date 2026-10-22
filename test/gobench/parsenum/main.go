// strconv in both directions.  Integers are rendered with AppendInt, with
// FormatInt in base 16 and with Itoa, and floats with AppendFloat('f', 3);
// the text is then split on spaces and parsed back with ParseInt, Atoi and
// ParseFloat, and a few strings at the end take the error paths.  None of it
// is code TinyGo writes: it is Go's own strconv, reached by plain `call`s into
// internal/strconv -- formatBase10, fmtEFG and uscale, which multiplies
// 64-bit halves through `i64.mul` and compiler-rt's `__multi3`, on the way
// out; ParseInt and parseFloat64 on the way back, with the big-decimal
// fallback `decimal.Shift` behind both.  The four-way field switch is a
// `br_table`, and every token is copied into a string by
// runtime.stringFromBytes before it is parsed.
//
// strbuild covers AppendInt alone.  This adds the parse side, a
// byte-at-a-time loop with an overflow check and a digit-class branch per
// character, and float formatting and parsing, which mix wide integer
// multiplies with f64 work: the code a config or JSON reader spends its time
// in.
package main

import "strconv"

//go:noinline
func render(dst []byte, vals []int64, fl []float64) []byte {
	dst = dst[:0]
	for i, v := range vals {
		switch i & 3 {
		case 0:
			dst = strconv.AppendInt(dst, v, 10)
		case 1:
			dst = append(dst, strconv.FormatInt(v, 16)...)
		case 2:
			dst = append(dst, strconv.Itoa(int(v))...)
		default:
			dst = strconv.AppendFloat(dst, fl[i], 'f', 3, 64)
		}
		dst = append(dst, ' ')
	}
	return dst
}

//go:noinline
func parse(text []byte) (int64, int) {
	var s int64
	bad := 0
	start, field := 0, 0
	for i := 0; i <= len(text); i++ {
		if i < len(text) && text[i] != ' ' {
			continue
		}
		tok := string(text[start:i])
		start = i + 1
		if len(tok) == 0 {
			continue
		}
		switch field & 3 {
		case 0:
			v, err := strconv.ParseInt(tok, 10, 64)
			if err != nil {
				bad++
			}
			s += v
		case 1:
			v, err := strconv.ParseInt(tok, 16, 64)
			if err != nil {
				bad++
			}
			s ^= v
		case 2:
			v, err := strconv.Atoi(tok)
			if err != nil {
				bad++
			}
			s += int64(v) * 3
		default:
			f, err := strconv.ParseFloat(tok, 64)
			if err != nil {
				bad++
			}
			s += int64(f * 1000)
		}
		field++
	}
	return s, bad
}

func main() {
	const n = 256
	vals := make([]int64, n)
	fl := make([]float64, n)
	x := uint64(88172645463325252)
	for i := range vals {
		x ^= x << 13
		x ^= x >> 7
		x ^= x << 17
		vals[i] = int64(x>>20) - int64(1)<<42
		fl[i] = float64(int64(x>>40)-(1<<23)) / 1024
	}
	buf := make([]byte, 0, 8192)
	total := 0
	for r := 0; r < 4800; r++ {
		vals[r&(n-1)] = int64(r) * -7919
		fl[(r*3)&(n-1)] = float64(r) / 8
		buf = render(buf, vals, fl)
		s, bad := parse(buf)
		total += int(s&0xffffff) + len(buf) + bad*1000000
	}
	// A few error paths, so the error values are built and inspected too.
	for _, t := range []string{"12x", "", "99999999999999999999", "-0x10", "+42"} {
		v, err := strconv.ParseInt(t, 0, 64)
		if err != nil {
			total += 1
		}
		total += int(v & 0xff)
	}
	println("total =", total)
}
