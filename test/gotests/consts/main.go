// Constants and iota.  iota in sequences with skips, expressions, several
// constants per line and typed enumerations with String methods; untyped
// constant arithmetic carried out at arbitrary precision before conversion
// (1<<100 >> 98, big float products that only fit after division, exact
// rational results like 1/3*3), untyped float, rune and string constants,
// constant shifts, typed constants that are rounded on conversion to float32,
// the Max/Min constants of math, and constants mixed with run-time values
// so the converted constant is what the generated code actually loads.
package main

import (
	"fmt"
	"math"
	"os"
)

type Weekday int

const (
	Sunday Weekday = iota
	Monday
	Tuesday
	Wednesday
	Thursday
	Friday
	Saturday
)

var dayNames = [...]string{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}

func (d Weekday) String() string {
	if d < 0 || int(d) >= len(dayNames) {
		return fmt.Sprintf("Weekday(%d)", int(d))
	}
	return dayNames[d]
}

type ByteSize float64

const (
	_           = iota
	KB ByteSize = 1 << (10 * iota)
	MB
	GB
	TB
	PB
)

func (b ByteSize) String() string {
	switch {
	case b >= PB:
		return fmt.Sprintf("%.2fPB", b/PB)
	case b >= TB:
		return fmt.Sprintf("%.2fTB", b/TB)
	case b >= GB:
		return fmt.Sprintf("%.2fGB", b/GB)
	case b >= MB:
		return fmt.Sprintf("%.2fMB", b/MB)
	case b >= KB:
		return fmt.Sprintf("%.2fKB", b/KB)
	}
	return fmt.Sprintf("%.0fB", float64(b))
}

type Flags uint8

const (
	FlagRead Flags = 1 << iota
	FlagWrite
	FlagExec
	_
	FlagSticky
	FlagAll = FlagRead | FlagWrite | FlagExec | FlagSticky
)

func (f Flags) String() string {
	s := ""
	names := []string{"r", "w", "x", "?", "s"}
	for i, n := range names {
		if f&(1<<i) != 0 {
			s += n
		} else {
			s += "-"
		}
	}
	return s
}

const (
	a, b = iota * 10, iota + 100 // two per line share iota
	c, d
	e, f
)

const (
	x0 = iota * iota
	x1
	x2
	_
	x4
	skip = "s"
	x6   = iota
)

const (
	Huge          = 1 << 100
	Shifted       = Huge >> 98
	Third         = 1.0 / 3
	Exact         = Third * 3 // exactly 1 in constant arithmetic
	BigF          = 1e300 * 1e300 / 1e300
	Tiny          = 1e-300 * 1e-300 * 1e300
	Pi2           = math.Pi * 2
	Mask          = ^uint32(0)
	MinI64        = -1 << 63
	RuneSum       = 'a' + 1
	Str           = "con" + "cat"
	StrLen        = len(Str)
	Typed   int16 = 1000 * 30
	Cplx          = (1 + 2i) * (3 - 1i)
)

var rt = 1

func init() {
	if len(os.Args) > 1000 {
		rt = 2
	}
}

func main() {
	fmt.Println("-- enums")
	for d := Sunday; d <= Saturday+1; d++ {
		fmt.Print(d, " ")
	}
	fmt.Println()
	fmt.Println(Wednesday, int(Wednesday), Weekday(rt*5), Weekday(-rt))
	fmt.Printf("%v %d %s\n", Friday, Friday, Saturday)
	sizes := []ByteSize{512, KB, 1536, MB * 3, GB * 1.5, TB, PB * 2.25}
	for _, s := range sizes {
		fmt.Print(s, " ")
	}
	fmt.Println(float64(PB))
	for f := Flags(0); f <= FlagAll; f += 3 {
		fmt.Print(f, " ")
	}
	fmt.Println(FlagAll, FlagSticky, uint8(FlagAll))
	fmt.Println(a, b, c, d, e, f)
	fmt.Println(x0, x1, x2, x4, skip, x6)
	fmt.Println("-- untyped arithmetic")
	fmt.Println(Shifted, Exact == 1, Exact, BigF, Tiny, Pi2)
	fmt.Println(Mask, int64(MinI64), RuneSum, string(rune(RuneSum)), Str, StrLen, Typed, Cplx)
	fmt.Println(Huge/(1<<90), Huge%7, float64(Huge), uint64(Huge>>40))
	const big = 123456789012345678901234567890
	fmt.Println(big/1e20, big%1000000007, float32(big))
	fmt.Println(7/2, 7.0/2, 7/2.0, -7/2, -7%3, 1<<3*2, 1<<(3*2))
	fmt.Println(math.MaxInt8, math.MinInt8, math.MaxInt16, math.MaxUint16,
		math.MaxInt32, uint32(math.MaxUint32), int64(math.MaxInt64), uint64(math.MaxUint64))
	fmt.Println(math.MaxFloat32, math.SmallestNonzeroFloat32, math.MaxFloat64,
		math.SmallestNonzeroFloat64)
	fmt.Println("-- constants meet run-time values")
	v := rt
	fmt.Println(v*Shifted, v<<10, v<<StrLen, Huge>>(100-3)*v, -int64(v)*MinI64/(1<<62))
	var f32 float32 = Third
	var f64 float64 = Third
	fmt.Println(f32, f64, float64(f32) == f64, f32*3 == 1, f64*3 == 1)
	var u8 uint8 = 200
	u8 += 100 // the constant converts to uint8 first, then the sum wraps
	var i8 int8 = -100
	i8 -= 100
	fmt.Println(u8, i8, uint8(v*300), int8(v*200))
	h := 0
	for i := 0; i < 10; i++ {
		h = h*31 + int(Str[i%StrLen]) + int(RuneSum)
	}
	fmt.Println(h)
	fmt.Println(Cplx*complex(float64(v), 0), real(Cplx), imag(Cplx))
	const typedF float32 = 0.1
	fmt.Println(typedF, float64(typedF), float64(0.1), typedF == 0.1)
	fmt.Println(len([10]int{}), cap([5]byte{}), len("héllo"), len([]rune("héllo")))
	type Level uint8
	const (
		Debug Level = iota + 1
		Info
		Warn = Info * 3
		Error
	)
	fmt.Println(Debug, Info, Warn, Error)
	fmt.Println("-- constant tables against run-time arithmetic")
	for i := 0; i < 16; i++ {
		d := Weekday((i*5 + rt) % 7)
		next := (d + 1) % 7
		sz := ByteSize(float64(int64(1)<<(i*4)) * float64(rt))
		fl := Flags(i*rt) & FlagAll
		fmt.Printf("%2d %s->%s %-9v %v %3d %v\n", i, d, next, sz, fl, fl&^FlagWrite,
			fl&FlagExec != 0)
	}
	for i := 0; i < 8; i++ {
		v := int64(rt) << (8 * i)
		fmt.Println(i, v*Shifted, v%int64(RuneSum), float32(v)*Third, uint8(v+MinI64), v > int64(Typed))
	}
}
