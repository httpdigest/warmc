// reflect: walking types (Kind, Name, String, Elem, Key, Len, NumField,
// NumMethod, NumIn/NumOut, embedded fields, struct tags with Get and Lookup),
// reading and writing values through pointers (Elem, Set*, CanSet,
// FieldByName, unexported fields refusing to be set), building values (New,
// MakeSlice, MakeMap, MakeChan, MakeFunc, Append, Zero, Indirect), calling
// functions and methods by name with reflect.Value.Call and variadic
// CallSlice, sorted MapKeys and MapRange, Convert between numeric types,
// DeepEqual over nested and cyclic structures, and a generic recursive
// pretty-printer driven entirely by reflection.
//
// reflect.Value.Call is the interesting path for a compiler: it lays out an
// argument frame and enters runtime.reflectcall, which on wasm picks one of a
// family of fixed-frame trampolines and copies the frame onto the Go stack.
// MakeFunc is the reverse, a closure whose body is reflect's own dispatcher.
package main

import (
	"fmt"
	"reflect"
	"sort"
	"strings"
)

type Base struct {
	ID      int    `json:"id" db:"pk"`
	Created string `json:"created,omitempty"`
}

func (b Base) Describe() string { return fmt.Sprintf("Base#%d", b.ID) }

type User struct {
	Base
	Name    string            `json:"name" validate:"required,min=2"`
	Email   string            `json:"email"`
	Age     int               `json:"age,omitempty"`
	Tags    []string          `json:"tags"`
	Meta    map[string]string `json:"-"`
	private int
	Next    *User
}

func (u User) Greeting(prefix string, times int) string {
	return strings.Repeat(prefix+" "+u.Name+"! ", times)
}

func (u *User) Rename(n string) { u.Name = n }

func (u User) Sum(xs ...int) int {
	s := u.Age
	for _, x := range xs {
		s += x
	}
	return s
}

type Celsius float64
type Shape interface {
	Area() float64
}
type Rect struct{ W, H float64 }

func (r Rect) Area() float64 { return r.W * r.H }

func dump(v reflect.Value, depth int, b *strings.Builder) {
	ind := strings.Repeat("  ", depth)
	switch v.Kind() {
	case reflect.Struct:
		fmt.Fprintf(b, "%s{\n", v.Type())
		for i := 0; i < v.NumField(); i++ {
			f := v.Type().Field(i)
			fmt.Fprintf(b, "%s  %s: ", ind, f.Name)
			if !f.IsExported() {
				fmt.Fprintf(b, "(unexported %s)\n", f.Type)
				continue
			}
			dump(v.Field(i), depth+1, b)
		}
		fmt.Fprintf(b, "%s}\n", ind)
	case reflect.Pointer:
		if v.IsNil() {
			b.WriteString("nil\n")
			return
		}
		b.WriteString("&")
		dump(v.Elem(), depth, b)
	case reflect.Slice, reflect.Array:
		fmt.Fprintf(b, "%s len=%d [", v.Type(), v.Len())
		for i := 0; i < v.Len(); i++ {
			if i > 0 {
				b.WriteString(", ")
			}
			fmt.Fprintf(b, "%v", v.Index(i).Interface())
		}
		b.WriteString("]\n")
	case reflect.Map:
		keys := v.MapKeys()
		sort.Slice(keys, func(i, j int) bool { return fmt.Sprint(keys[i]) < fmt.Sprint(keys[j]) })
		fmt.Fprintf(b, "%s{", v.Type())
		for i, k := range keys {
			if i > 0 {
				b.WriteString(", ")
			}
			fmt.Fprintf(b, "%v: %v", k, v.MapIndex(k))
		}
		b.WriteString("}\n")
	default:
		fmt.Fprintf(b, "%v (%s)\n", v.Interface(), v.Kind())
	}
}

func main() {
	// Types.
	types := []any{
		0, int8(0), uint64(0), 1.5, float32(1), complex(1, 2), "s", true, 'r', byte(1),
		[]int{}, [3]string{}, map[string][]int{}, make(chan int), make(<-chan bool),
		func(int, string) (bool, error) { return false, nil }, &User{}, User{},
		Celsius(0), (*Shape)(nil), Rect{}, struct{ A, b int }{}, any(nil), new(any),
		[]*map[int]Rect{},
	}
	for _, x := range types {
		t := reflect.TypeOf(x)
		if t == nil {
			fmt.Println("type: <nil interface>")
			continue
		}
		extra := ""
		switch t.Kind() {
		case reflect.Slice, reflect.Pointer, reflect.Chan:
			extra = " elem=" + t.Elem().String()
			if t.Kind() == reflect.Chan {
				extra += " dir=" + t.ChanDir().String()
			}
		case reflect.Array:
			extra = fmt.Sprintf(" len=%d elem=%s", t.Len(), t.Elem())
		case reflect.Map:
			extra = " key=" + t.Key().String() + " elem=" + t.Elem().String()
		case reflect.Func:
			extra = fmt.Sprintf(" in=%d out=%d first=%s variadic=%v", t.NumIn(), t.NumOut(), t.In(0), t.IsVariadic())
		case reflect.Struct:
			extra = fmt.Sprintf(" fields=%d", t.NumField())
		}
		fmt.Printf("type %-28s kind=%-9s name=%-8q size>0=%v comparable=%v%s\n",
			t.String(), t.Kind(), t.Name(), t.Size() > 0, t.Comparable(), extra)
	}

	// Struct fields and tags.
	ut := reflect.TypeOf(User{})
	for i := 0; i < ut.NumField(); i++ {
		f := ut.Field(i)
		js, hasJS := f.Tag.Lookup("json")
		fmt.Printf("field %-8s type=%-17s idx=%v anon=%-5v exported=%-5v json=%q(%v) validate=%q\n",
			f.Name, f.Type, f.Index, f.Anonymous, f.IsExported(), js, hasJS, f.Tag.Get("validate"))
	}
	if f, ok := ut.FieldByName("ID"); ok {
		fmt.Println("promoted field ID index", f.Index, "db tag", f.Tag.Get("db"))
	}
	_, ok := ut.FieldByName("Missing")
	fmt.Println("FieldByName Missing:", ok)

	// Methods (value and pointer method sets).
	for _, t := range []reflect.Type{ut, reflect.PointerTo(ut)} {
		var names []string
		for i := 0; i < t.NumMethod(); i++ {
			m := t.Method(i)
			names = append(names, fmt.Sprintf("%s%s", m.Name, strings.TrimPrefix(m.Type.String(), "func")))
		}
		fmt.Println("methods of", t, "->", names)
	}
	fmt.Println("Rect implements Shape:", reflect.TypeOf(Rect{}).Implements(reflect.TypeOf((*Shape)(nil)).Elem()))
	fmt.Println("User implements Shape:", ut.Implements(reflect.TypeOf((*Shape)(nil)).Elem()))
	fmt.Println("Celsius convertible to int:", reflect.TypeOf(Celsius(0)).ConvertibleTo(reflect.TypeOf(0)))

	// Values: set through a pointer.
	u := &User{Base: Base{ID: 7}, Name: "ann", Email: "a@x", Age: 30, Tags: []string{"x"}}
	v := reflect.ValueOf(u).Elem()
	fmt.Println("CanSet direct:", reflect.ValueOf(*u).Field(1).CanSet(), "via ptr:", v.Field(1).CanSet())
	v.FieldByName("Name").SetString("Bob")
	v.FieldByName("Age").SetInt(41)
	v.FieldByName("ID").SetInt(8)
	v.FieldByName("Tags").Set(reflect.Append(v.FieldByName("Tags"), reflect.ValueOf("y"), reflect.ValueOf("z")))
	v.FieldByName("Meta").Set(reflect.MakeMap(reflect.TypeOf(map[string]string{})))
	v.FieldByName("Meta").SetMapIndex(reflect.ValueOf("k2"), reflect.ValueOf("v2"))
	v.FieldByName("Meta").SetMapIndex(reflect.ValueOf("k1"), reflect.ValueOf("v1"))
	priv := v.FieldByName("private")
	fmt.Println("private CanSet:", priv.CanSet(), "CanInterface:", priv.CanInterface(), "Int():", priv.Int())
	func() {
		defer func() { fmt.Println("set unexported panics:", recover()) }()
		priv.SetInt(3)
	}()
	fmt.Printf("after sets: %+v meta=%v\n", u.Base, u.Meta)
	fmt.Println(u.Name, u.Age, u.Tags)

	// Calling methods by name.
	res := reflect.ValueOf(*u).MethodByName("Greeting").Call([]reflect.Value{reflect.ValueOf("Hi"), reflect.ValueOf(2)})
	fmt.Printf("Greeting -> %q\n", res[0].String())
	reflect.ValueOf(u).MethodByName("Rename").Call([]reflect.Value{reflect.ValueOf("Carol")})
	fmt.Println("after Rename:", u.Name)
	sum := reflect.ValueOf(*u).MethodByName("Sum")
	fmt.Println("Sum variadic:", sum.Call([]reflect.Value{reflect.ValueOf(1), reflect.ValueOf(2), reflect.ValueOf(3)})[0].Int())
	fmt.Println("Sum CallSlice:", sum.CallSlice([]reflect.Value{reflect.ValueOf([]int{10, 20})})[0].Int())
	fmt.Println("Describe (promoted):", reflect.ValueOf(u).MethodByName("Describe").Call(nil)[0])
	fmt.Println("missing method valid:", reflect.ValueOf(u).MethodByName("Nope").IsValid())

	// Method expressions through Type.Method.
	m, _ := reflect.TypeOf(Rect{}).MethodByName("Area")
	fmt.Println("Method via type:", m.Func.Call([]reflect.Value{reflect.ValueOf(Rect{3, 4})})[0].Float())

	// Functions: Call with many argument shapes.
	fns := []any{
		strings.ToUpper, strings.Repeat, fmt.Sprintf,
		func(a, b int, c float64, d string, e bool, f int8, g uint64) string {
			return fmt.Sprint(a, b, c, d, e, f, g)
		},
		func(r Rect, p *Rect, s []int) (float64, int, error) { return r.Area() + p.Area(), len(s), nil },
	}
	args := [][]reflect.Value{
		{reflect.ValueOf("shout")},
		{reflect.ValueOf("ab"), reflect.ValueOf(3)},
		{reflect.ValueOf("%d-%s-%v"), reflect.ValueOf(5), reflect.ValueOf("x"), reflect.ValueOf(true)},
		{reflect.ValueOf(1), reflect.ValueOf(2), reflect.ValueOf(3.5), reflect.ValueOf("four"), reflect.ValueOf(true), reflect.ValueOf(int8(-6)), reflect.ValueOf(uint64(1) << 63)},
		{reflect.ValueOf(Rect{2, 3}), reflect.ValueOf(&Rect{1, 1}), reflect.ValueOf([]int{1, 2, 3})},
	}
	for i, f := range fns {
		out := reflect.ValueOf(f).Call(args[i])
		var parts []string
		for _, o := range out {
			parts = append(parts, fmt.Sprintf("%v", o.Interface()))
		}
		fmt.Printf("call %d %s -> %v\n", i, reflect.TypeOf(f), parts)
	}

	// MakeFunc: a generic swap and an adder built at run time.
	swap := func(in []reflect.Value) []reflect.Value { return []reflect.Value{in[1], in[0]} }
	var intSwap func(int, int) (int, int)
	var strSwap func(string, string) (string, string)
	reflect.ValueOf(&intSwap).Elem().Set(reflect.MakeFunc(reflect.TypeOf(intSwap), swap))
	reflect.ValueOf(&strSwap).Elem().Set(reflect.MakeFunc(reflect.TypeOf(strSwap), swap))
	a1, a2 := intSwap(1, 2)
	s1, s2 := strSwap("left", "right")
	fmt.Println("MakeFunc swap", a1, a2, s1, s2)
	adderT := reflect.FuncOf([]reflect.Type{reflect.TypeOf(0), reflect.TypeOf([]int{})}, []reflect.Type{reflect.TypeOf(0)}, true)
	adder := reflect.MakeFunc(adderT, func(in []reflect.Value) []reflect.Value {
		t := in[0].Int()
		for i := 0; i < in[1].Len(); i++ {
			t += in[1].Index(i).Int()
		}
		return []reflect.Value{reflect.ValueOf(int(t))}
	})
	fmt.Println("MakeFunc variadic adder", adderT, adder.Call([]reflect.Value{reflect.ValueOf(100), reflect.ValueOf(1), reflect.ValueOf(2)})[0])

	// Building values.
	sl := reflect.MakeSlice(reflect.TypeOf([]float64{}), 3, 10)
	for i := 0; i < sl.Len(); i++ {
		sl.Index(i).SetFloat(float64(i) * 1.5)
	}
	sl = reflect.AppendSlice(sl, reflect.ValueOf([]float64{9, 8}))
	fmt.Println("MakeSlice", sl.Interface(), sl.Len(), sl.Cap())
	np := reflect.New(reflect.TypeOf(Rect{}))
	np.Elem().Field(0).SetFloat(5)
	np.Elem().FieldByName("H").SetFloat(6)
	fmt.Printf("New Rect %+v area %v\n", np.Interface(), np.Interface().(Shape).Area())
	ch := reflect.MakeChan(reflect.TypeOf(make(chan string)), 2)
	ch.Send(reflect.ValueOf("one"))
	ok = ch.TrySend(reflect.ValueOf("two"))
	full := ch.TrySend(reflect.ValueOf("three"))
	r1, _ := ch.Recv()
	fmt.Println("MakeChan", ok, full, r1, ch.Len())
	fmt.Println("Zero", reflect.Zero(reflect.TypeOf(User{})).Interface().(User).Name == "", reflect.Zero(reflect.TypeOf(3.0)))
	fmt.Println("Indirect", reflect.Indirect(reflect.ValueOf(&Rect{7, 8})).Field(1))
	arrT := reflect.ArrayOf(4, reflect.TypeOf(byte(0)))
	arr := reflect.New(arrT).Elem()
	reflect.Copy(arr, reflect.ValueOf([]byte("wasm!")))
	fmt.Println("ArrayOf", arrT, arr.Interface())
	stT := reflect.StructOf([]reflect.StructField{
		{Name: "A", Type: reflect.TypeOf(0), Tag: `json:"a"`},
		{Name: "B", Type: reflect.TypeOf("")},
	})
	st := reflect.New(stT).Elem()
	st.Field(0).SetInt(5)
	st.Field(1).SetString("five")
	fmt.Printf("StructOf %v %+v\n", stT, st.Interface())

	// Maps: sorted keys, MapRange.
	mv := reflect.ValueOf(map[string]int{"pear": 3, "apple": 1, "fig": 7, "kiwi": 2})
	keys := mv.MapKeys()
	sort.Slice(keys, func(i, j int) bool { return keys[i].String() < keys[j].String() })
	for _, k := range keys {
		fmt.Println("map", k, mv.MapIndex(k))
	}
	it := mv.MapRange()
	total := 0
	for it.Next() {
		total += int(it.Value().Int()) * len(it.Key().String())
	}
	fmt.Println("MapRange weighted total", total)
	fmt.Println("missing key valid:", mv.MapIndex(reflect.ValueOf("zzz")).IsValid())

	// Conversions.
	cv := reflect.ValueOf(Celsius(36.6))
	fmt.Println("Convert", cv.Convert(reflect.TypeOf(int(0))), cv.Convert(reflect.TypeOf(float32(0))), cv.Kind(), cv.Type())
	fmt.Println("Convert int->string", reflect.ValueOf(65).Convert(reflect.TypeOf("")))
	fmt.Println("Convert []byte->string", reflect.ValueOf([]byte("bytes")).Convert(reflect.TypeOf("")))
	fmt.Println("OverflowInt int8 200:", reflect.ValueOf(int8(0)).OverflowInt(200), "100:", reflect.ValueOf(int8(0)).OverflowInt(100))

	// DeepEqual.
	type T struct {
		A []int
		M map[string][]string
		P *int
	}
	x, y := 5, 5
	pairs := [][2]any{
		{[]int{1, 2}, []int{1, 2}},
		{[]int{1, 2}, []int{2, 1}},
		{[]int{}, []int(nil)},
		{map[int]string{1: "a"}, map[int]string{1: "a"}},
		{T{[]int{1}, map[string][]string{"k": {"v"}}, &x}, T{[]int{1}, map[string][]string{"k": {"v"}}, &y}},
		{T{P: &x}, T{P: nil}},
		{[2]any{1, "a"}, [2]any{1, "a"}},
		{int32(1), int64(1)},
		{nil, nil},
		{error(nil), nil},
		{func() {}, nil},
	}
	for i, p := range pairs {
		fmt.Printf("DeepEqual %2d %v\n", i, reflect.DeepEqual(p[0], p[1]))
	}
	type cyc struct {
		Next *cyc
		V    int
	}
	c1 := &cyc{V: 1}
	c1.Next = c1
	c2 := &cyc{V: 1}
	c2.Next = c2
	fmt.Println("DeepEqual cyclic:", reflect.DeepEqual(c1, c2))

	// Reflective dump.
	var b strings.Builder
	u.Next = &User{Name: "child", Tags: []string{}}
	dump(reflect.ValueOf(u), 0, &b)
	dump(reflect.ValueOf([3]Celsius{1, 2.5, -3}), 0, &b)
	dump(reflect.ValueOf(map[int]bool{3: true, 1: false, 2: true}), 0, &b)
	fmt.Print(b.String())

	// Value.IsZero, Comparable, Equal, SetZero.
	for _, x := range []any{0, 1, "", "a", []int(nil), []int{}, Rect{}, Rect{1, 0}, (*int)(nil)} {
		fmt.Printf("IsZero(%#v) = %v\n", x, reflect.ValueOf(x).IsZero())
	}
	rv := reflect.ValueOf(&Rect{1, 2}).Elem()
	rv.SetZero()
	fmt.Println("SetZero", rv.Interface(), reflect.ValueOf(3).Equal(reflect.ValueOf(3)))
	fmt.Println("TypeFor", reflect.TypeFor[map[string]Celsius](), reflect.TypeFor[Shape]().Kind())
}
