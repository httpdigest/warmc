// The parts of reflect TinyGo supports.  Type identity, Kind, Name, String,
// Size, element/key types, struct fields with names, offsets and tags,
// method counts; Values read and written through pointers (SetInt,
// SetString, SetFloat, SetBool, Field and Index), slices and maps built
// with MakeSlice/MakeMap/Append/SetMapIndex, New and Zero, Convert between
// numeric kinds, and DeepEqual over nested values including cycles and nil
// against empty.  A generic walker prints any value by recursing on its
// Kind, which is how encoders and printers use reflection; TinyGo's type
// metadata is laid out in data segments the walker reads at run time.
package main

import (
	"fmt"
	"os"
	"reflect"
	"sort"
	"strings"
)

type Inner struct {
	X, Y int16
}

type Record struct {
	Name    string         `json:"name" db:"n"`
	Age     int            `json:"age,omitempty"`
	Score   float64        `json:"score"`
	Tags    []string       `json:"tags"`
	Attrs   map[string]int `json:"attrs"`
	In      Inner          `json:"in"`
	Ptr     *Inner         `json:"ptr"`
	Active  bool           `json:"active"`
	private int
	Arr     [3]uint8
}

func (r Record) Hello() string { return "hi " + r.Name }
func (r *Record) Birthday()    { r.Age++ }

type node struct {
	V    int
	Next *node
}

var bias = 0

func init() {
	if len(os.Args) > 1000 {
		bias = 1
	}
}

func walk(v reflect.Value, depth int) string {
	if depth > 6 {
		return "..."
	}
	switch v.Kind() {
	case reflect.Invalid:
		return "invalid"
	case reflect.Bool:
		return fmt.Sprint(v.Bool())
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return fmt.Sprint(v.Int())
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return fmt.Sprint(v.Uint())
	case reflect.Float32, reflect.Float64:
		return fmt.Sprint(v.Float())
	case reflect.String:
		return fmt.Sprintf("%q", v.String())
	case reflect.Slice, reflect.Array:
		if v.Kind() == reflect.Slice && v.IsNil() {
			return "nil"
		}
		parts := []string{}
		for i := 0; i < v.Len(); i++ {
			parts = append(parts, walk(v.Index(i), depth+1))
		}
		return "[" + strings.Join(parts, " ") + "]"
	case reflect.Map:
		if v.IsNil() {
			return "nilmap"
		}
		// MapKeys comes back in map iteration order, which is randomised:
		// sort by the printed form of each key.
		parts := []string{}
		for _, k := range v.MapKeys() {
			parts = append(parts, walk(k, depth+1)+":"+walk(v.MapIndex(k), depth+1))
		}
		sort.Strings(parts)
		return "{" + strings.Join(parts, ", ") + "}"
	case reflect.Pointer:
		if v.IsNil() {
			return "nilptr"
		}
		return "&" + walk(v.Elem(), depth+1)
	case reflect.Interface:
		if v.IsNil() {
			return "nilif"
		}
		return walk(v.Elem(), depth+1)
	case reflect.Struct:
		t := v.Type()
		parts := []string{}
		for i := 0; i < t.NumField(); i++ {
			f := t.Field(i)
			if !f.IsExported() {
				parts = append(parts, f.Name+"=(unexported)")
				continue
			}
			parts = append(parts, f.Name+"="+walk(v.Field(i), depth+1))
		}
		return t.Name() + "{" + strings.Join(parts, " ") + "}"
	}
	return "<" + v.Kind().String() + ">"
}

func main() {
	fmt.Println("-- types")
	vals := []any{1, int8(2), uint16(3), int64(4), 5.5, float32(6.5), "s", true, []int{1}, [2]string{},
		map[string]bool{}, &Inner{}, Inner{}, Record{}, 'r', byte(1), complex(1, 2), (*int)(nil)}
	for _, v := range vals {
		t := reflect.TypeOf(v)
		fmt.Printf("%-22s kind=%-10s name=%-8q size=%d comparable=%v\n", t.String(), t.Kind(), t.Name(),
			t.Size(), t.Comparable())
	}
	rt := reflect.TypeOf(Record{})
	fmt.Println(rt.NumField(), rt.NumMethod(), reflect.TypeOf(&Record{}).NumMethod())
	for i := 0; i < rt.NumField(); i++ {
		f := rt.Field(i)
		fmt.Printf("%-8s %-16s off=%-3d exported=%-5v json=%q db=%q\n", f.Name, f.Type.String(), f.Offset,
			f.IsExported(), f.Tag.Get("json"), f.Tag.Get("db"))
	}
	fmt.Println(reflect.TypeOf([]string{}).Elem().Kind(), reflect.TypeOf(map[string]int{}).Key().Kind(),
		reflect.TypeOf(map[string]int{}).Elem().Kind(), reflect.TypeOf(&Inner{}).Elem().Name(),
		reflect.TypeOf([4]int{}).Len())
	fmt.Println(reflect.TypeOf(1) == reflect.TypeOf(2), reflect.TypeOf(1) == reflect.TypeOf(int64(2)),
		reflect.TypeOf(Inner{}) == reflect.TypeOf(&Inner{}).Elem())
	fmt.Println("-- values")
	r := Record{Name: "ann", Age: 30 + bias, Score: 9.5, Tags: []string{"a", "b"},
		Attrs: map[string]int{"k": 1, "j": 2}, In: Inner{1, 2}, Ptr: &Inner{3, 4}, Arr: [3]uint8{7, 8, 9}}
	fmt.Println(walk(reflect.ValueOf(r), 0))
	v := reflect.ValueOf(&r).Elem()
	v.FieldByName("Name").SetString("bob")
	v.Field(1).SetInt(41)
	v.Field(2).SetFloat(-1.25)
	v.FieldByName("Active").SetBool(true)
	v.FieldByName("In").Field(0).SetInt(-100)
	v.FieldByName("Arr").Index(1).SetUint(200)
	v.FieldByName("Tags").Index(0).SetString("z")
	v.FieldByName("Ptr").Elem().Field(1).SetInt(44)
	fmt.Println(walk(v, 0))
	fmt.Println(r.Name, r.Age, r.In, *r.Ptr, r.Arr, v.Field(8).CanSet(), v.Field(1).CanSet(),
		reflect.ValueOf(r).Field(1).CanSet())
	fmt.Println("-- building")
	s := reflect.MakeSlice(reflect.TypeOf([]int{}), 0, 2)
	for i := 0; i < 5; i++ {
		s = reflect.Append(s, reflect.ValueOf(i*i))
	}
	fmt.Println(s.Len(), s.Interface())
	m := reflect.MakeMap(reflect.TypeOf(map[string]float64{}))
	m.SetMapIndex(reflect.ValueOf("pi"), reflect.ValueOf(3.14))
	m.SetMapIndex(reflect.ValueOf("e"), reflect.ValueOf(2.72))
	m.SetMapIndex(reflect.ValueOf("x"), reflect.ValueOf(1.0))
	m.SetMapIndex(reflect.ValueOf("x"), reflect.Value{}) // delete
	fmt.Println(m.Len(), walk(m, 0))
	p := reflect.New(reflect.TypeOf(Inner{}))
	p.Elem().Field(0).SetInt(5)
	fmt.Println(walk(p, 0), p.Interface().(*Inner).X)
	z := reflect.Zero(reflect.TypeOf(Record{}))
	fmt.Println(z.Field(0).String() == "", z.Field(3).IsNil(), z.IsZero(), reflect.ValueOf(r).IsZero())
	for _, x := range []any{int8(-5), uint32(70000), 2.9, float32(-1.5)} {
		xv := reflect.ValueOf(x)
		fmt.Println(xv.Convert(reflect.TypeOf(int64(0))).Int(), xv.Convert(reflect.TypeOf(float64(0))).Float(),
			xv.Convert(reflect.TypeOf(uint8(0))).Uint(), xv.Type().ConvertibleTo(reflect.TypeOf("")))
	}
	fmt.Println("-- DeepEqual")
	a1 := map[string][]int{"x": {1, 2}, "y": nil}
	a2 := map[string][]int{"x": {1, 2}, "y": nil}
	a3 := map[string][]int{"x": {1, 2}, "y": {}}
	n1 := &node{1, nil}
	n1.Next = n1
	n2 := &node{1, nil}
	n2.Next = n2
	n3 := &node{1, &node{1, nil}}
	fmt.Println(reflect.DeepEqual(a1, a2), reflect.DeepEqual(a1, a3), reflect.DeepEqual(n1, n2),
		reflect.DeepEqual(n1, n3), reflect.DeepEqual([]int(nil), []int{}), reflect.DeepEqual(r, r),
		reflect.DeepEqual(1, int64(1)), reflect.DeepEqual([]any{1, "a"}, []any{1, "a"}),
		reflect.DeepEqual(Inner{1, 2}, Inner{1, 2}), reflect.DeepEqual(&Inner{1, 2}, &Inner{1, 3}))
	fmt.Println("-- walking any value")
	things := []any{nil, 42, "str", []any{1, "two", []int{3}}, map[int]string{2: "b", 1: "a"},
		&node{7, &node{8, nil}}, [2]bool{true, false}, struct {
			A int
			b string
		}{1, "x"}, Inner{9, -9}, map[string]any{"nested": map[string]int{"deep": 1}}}
	for _, t := range things {
		fmt.Println(walk(reflect.ValueOf(t), 0))
	}
}
