// Value semantics and memory layout: struct and array assignment copies
// (including nested arrays of structs), comparison of structs and arrays with
// == (padding must not matter, NaN fields make a value unequal to itself),
// structs and arrays as map keys, slices aliasing their backing array through
// re-slicing, three-index slices limiting capacity, append growth reusing or
// reallocating, copy with overlap in both directions, string/byte-slice
// conversions copying, clear() on slices and maps, the min/max builtins,
// unsafe.Sizeof/Alignof/Offsetof of padded structs, unsafe.Slice/String/
// StringData/SliceData, and pointer arithmetic with unsafe.Add over an array.
//
// Go's wasm back end moves structs with sequences of loads and stores or
// memory.copy, and zeroes with memory.fill; array equality is a loop or a
// memequal call.  Sizes are computed for GOARCH=wasm (8-byte pointers).
package main

import (
	"fmt"
	"math"
	"unsafe"
)

type Inner struct {
	A int8
	B int64
	C [3]uint16
}

type Outer struct {
	Name  string
	In    Inner
	Arr   [4]Inner
	Flag  bool
	Ptr   *int
	Small byte
}

type Padded struct {
	a bool
	b int64
	c bool
	d int32
	e bool
}

type Packed struct {
	b int64
	d int32
	a bool
	c bool
	e bool
}

type key struct {
	X, Y int16
	Tag  string
}

type withFloat struct {
	F float64
	N int
}

func main() {
	// Copies.
	x := 5
	o1 := Outer{Name: "one", In: Inner{1, 2, [3]uint16{3, 4, 5}}, Flag: true, Ptr: &x, Small: 9}
	for i := range o1.Arr {
		o1.Arr[i] = Inner{int8(i), int64(i * 100), [3]uint16{uint16(i), uint16(i * 2), uint16(i * 3)}}
	}
	o2 := o1
	o2.Name = "two"
	o2.In.C[1] = 99
	o2.Arr[2].B = -1
	*o2.Ptr = 6 // shared pointer
	fmt.Printf("o1 %s %v %v %v ptr=%d\n", o1.Name, o1.In, o1.Arr[2], o1.Flag, *o1.Ptr)
	fmt.Printf("o2 %s %v %v %v ptr=%d\n", o2.Name, o2.In, o2.Arr[2], o2.Flag, *o2.Ptr)
	fmt.Println("struct equal", o1 == o2, o1.Arr == o2.Arr, o1.Arr[0] == o2.Arr[0])
	arr1 := [3][2]string{{"a", "b"}, {"c", "d"}, {"e", "f"}}
	arr2 := arr1
	arr2[1][0] = "X"
	fmt.Println("nested arrays", arr1, arr2, arr1 == arr2)
	arrP := &arr1
	arrP[2][1] = "Z"
	fmt.Println("array via pointer", arr1)

	// Function arguments copy; pointer arguments don't.
	modify := func(o Outer) Outer { o.Arr[0].A = 77; return o }
	modifyP := func(o *Outer) { o.Arr[0].A = 88 }
	o3 := modify(o1)
	fmt.Println("by value", o1.Arr[0].A, o3.Arr[0].A)
	modifyP(&o1)
	fmt.Println("by pointer", o1.Arr[0].A)

	// NaN in structs.
	wf := withFloat{math.NaN(), 1}
	fmt.Println("NaN struct equal to itself:", wf == wf, withFloat{1, 1} == withFloat{1, 1})
	nanArr := [2]float64{math.NaN(), 0}
	fmt.Println("NaN array equal to itself:", nanArr == nanArr)
	negz := [1]float64{math.Copysign(0, -1)}
	fmt.Println("-0 == +0 in arrays:", negz == [1]float64{0})

	// Map keys.
	m := map[key]int{}
	m[key{1, 2, "a"}] = 1
	m[key{1, 2, "a"}] += 10
	m[key{2, 1, "a"}] = 2
	m[key{1, 2, "b"}] = 3
	fmt.Println("struct keys", len(m), m[key{1, 2, "a"}], m[key{2, 1, "a"}], m[key{9, 9, ""}])
	am := map[[3]int]string{{1, 2, 3}: "x", {3, 2, 1}: "y"}
	fmt.Println("array keys", am[[3]int{1, 2, 3}], am[[3]int{3, 2, 1}], len(am))
	fm := map[float64]int{}
	fm[math.NaN()] = 1
	fm[math.NaN()] = 2
	fm[0] = 3
	fm[math.Copysign(0, -1)] = 4
	fmt.Println("float keys: len", len(fm), "zero", fm[0])
	im := map[any]int{1: 1, int64(1): 2, "1": 3, key{}: 4, [0]int{}: 5}
	fmt.Println("interface keys len", len(im), im[key{}], im[[0]int{}])

	// Slices aliasing.
	base := []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	s1 := base[2:5]
	s2 := base[4:8]
	s1[2] = 100 // base[4]
	fmt.Println("alias", base, s1, s2, len(s1), cap(s1), len(s2), cap(s2))
	s1 = append(s1, 200) // writes base[5]
	fmt.Println("append within cap", base, s1)
	s3 := base[2:4:4]
	s3 = append(s3, 300) // reallocates
	s3[0] = -2
	fmt.Println("3-index slice", base, s3, cap(base[2:4:4]))
	var grown []int
	var caps []int
	for i := 0; i < 20; i++ {
		grown = append(grown, i)
		if len(caps) == 0 || caps[len(caps)-1] != cap(grown) {
			caps = append(caps, cap(grown))
		}
	}
	fmt.Println("append growth caps", caps)
	sbig := make([]byte, 0, 1000)
	sbig = append(sbig, make([]byte, 1001)...)
	fmt.Println("big append cap>=1001", cap(sbig) >= 1001)

	// copy with overlap.
	c := []int{1, 2, 3, 4, 5, 6, 7, 8}
	n := copy(c[2:], c)
	fmt.Println("copy forward overlap", n, c)
	c = []int{1, 2, 3, 4, 5, 6, 7, 8}
	n = copy(c, c[3:])
	fmt.Println("copy backward overlap", n, c)
	bs := make([]byte, 3)
	n = copy(bs, "string source")
	fmt.Println("copy from string", n, string(bs))

	// String <-> []byte copies.
	str := "immutable"
	b := []byte(str)
	b[0] = 'I'
	str2 := string(b)
	b[1] = 'M'
	fmt.Println("conversions copy", str, str2, string(b))
	rs := []rune("héllo")
	rs[1] = 'e'
	fmt.Println("rune slice", string(rs), len(rs), len("héllo"))

	// clear.
	cs := []int{1, 2, 3}
	clear(cs)
	cm := map[string]int{"a": 1, "b": 2}
	clear(cm)
	fmt.Println("clear", cs, len(cs), len(cm))
	ps := []*int{&x, &x}
	clear(ps[:1])
	fmt.Println("clear pointers", ps[0] == nil, *ps[1])

	// min / max.
	fmt.Println("min/max", min(3, 1, 2), max(3, 1, 2), min("b", "a"), max(2.5, math.Inf(-1)), min(int8(-128), 127), max(uint(0), 7))
	fmt.Println("min/max NaN", min(1, math.NaN()), max(math.NaN(), 1))

	// unsafe layout (GOARCH=wasm).
	var pd Padded
	var pk Packed
	var o Outer
	fmt.Println("Sizeof Padded", unsafe.Sizeof(pd), "Packed", unsafe.Sizeof(pk), "Inner", unsafe.Sizeof(Inner{}), "Outer", unsafe.Sizeof(o))
	fmt.Println("Offsetof Padded", unsafe.Offsetof(pd.a), unsafe.Offsetof(pd.b), unsafe.Offsetof(pd.c), unsafe.Offsetof(pd.d), unsafe.Offsetof(pd.e))
	fmt.Println("Offsetof Outer", unsafe.Offsetof(o.Name), unsafe.Offsetof(o.In), unsafe.Offsetof(o.Arr), unsafe.Offsetof(o.Flag), unsafe.Offsetof(o.Ptr), unsafe.Offsetof(o.Small))
	fmt.Println("Alignof", unsafe.Alignof(pd), unsafe.Alignof(pd.d), unsafe.Alignof(o.Small), unsafe.Alignof(complex128(0)), unsafe.Alignof([0]int64{}))
	fmt.Println("Sizeof basics", unsafe.Sizeof(int(0)), unsafe.Sizeof(uintptr(0)), unsafe.Sizeof(""), unsafe.Sizeof([]int{}), unsafe.Sizeof(map[int]int{}), unsafe.Sizeof(any(nil)), unsafe.Sizeof(struct{}{}), unsafe.Sizeof([0]int{}), unsafe.Sizeof(func() {}))

	// unsafe.Slice / String / Data and pointer arithmetic.
	nums := [6]int32{10, 20, 30, 40, 50, 60}
	p := unsafe.Pointer(&nums[0])
	third := (*int32)(unsafe.Add(p, 2*unsafe.Sizeof(nums[0])))
	*third = 33
	fmt.Println("unsafe.Add", nums, *third)
	view := unsafe.Slice(&nums[1], 4)
	view[3] = 55
	fmt.Println("unsafe.Slice", view, nums)
	data := []byte("bytes to string")
	us := unsafe.String(&data[0], 5)
	fmt.Println("unsafe.String", us, unsafe.StringData(us) == &data[0], unsafe.SliceData(data) == &data[0])
	backb := unsafe.Slice(unsafe.StringData("const"), 5)
	fmt.Println("StringData slice", string(backb))
	// Reinterpret float bits.
	f := 1.5
	bits := *(*uint64)(unsafe.Pointer(&f))
	fmt.Printf("float bits via unsafe %016x == math %v\n", bits, bits == math.Float64bits(f))
	// Struct field through offset.
	fieldD := (*int32)(unsafe.Add(unsafe.Pointer(&pd), unsafe.Offsetof(pd.d)))
	*fieldD = 1234
	fmt.Println("field via offset", pd.d)

	// Zero values and composite literal defaults.
	var zo Outer
	fmt.Printf("zero Outer: %q %v %v %v %v %v\n", zo.Name, zo.In, zo.Arr[3], zo.Flag, zo.Ptr == nil, zo.Small)
	lit := [...]string{2: "two", 5: "five"}
	fmt.Printf("indexed array literal len=%d %q\n", len(lit), lit)
	sl := []int{3: 1, 1: 2}
	fmt.Println("indexed slice literal", sl, len(sl))
	type pt struct{ X, Y int }
	pts := []*pt{{1, 2}, {3, 4}}
	mp := map[pt][]pt{{0, 0}: {{1, 1}}}
	fmt.Println("elided types", *pts[0], *pts[1], mp)

	// Large struct moved through channels, interfaces and returns.
	type huge struct {
		data [64]int64
		s    string
	}
	mk := func(seed int64) huge {
		var h huge
		for i := range h.data {
			h.data[i] = seed * int64(i)
		}
		h.s = fmt.Sprint("seed", seed)
		return h
	}
	ch := make(chan huge, 2)
	ch <- mk(3)
	ch <- mk(-7)
	var iface any = <-ch
	h2 := <-ch
	h1 := iface.(huge)
	sum := int64(0)
	for i := range h1.data {
		sum += h1.data[i] - h2.data[i]
	}
	fmt.Println("huge struct", h1.s, h2.s, sum, h1.data[63], h2.data[63])
	arrOfHuge := [3]huge{mk(1), mk(2), mk(3)}
	cp := arrOfHuge
	cp[1].data[10] = -1
	fmt.Println("array of huge copied", arrOfHuge[1].data[10], cp[1].data[10], arrOfHuge == cp)
}
