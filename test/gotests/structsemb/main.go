// Structs, methods and embedding.  Value receivers get a copy and pointer
// receivers the original; calling a pointer method on an addressable value
// takes its address implicitly, and a value method through a pointer
// dereferences it.  Embedding by value and by pointer promotes fields and
// methods through several levels, an outer method shadows the embedded one
// (which stays reachable by its qualified name), and an embedded interface
// supplies methods a struct only partly overrides.  Struct equality,
// anonymous structs, conversions between identical layouts, zero values,
// padding-dependent layouts and %+v printing are covered too.
package main

import (
	"fmt"
	"os"
	"unsafe"
)

type Counter struct {
	n    int
	hits []int
}

func (c Counter) Get() int         { return c.n }
func (c Counter) IncCopy() Counter { c.n++; return c }
func (c *Counter) Inc()            { c.n++; c.hits = append(c.hits, c.n) }
func (c *Counter) Add(k int) *Counter {
	c.n += k
	return c
}

type Named struct {
	Name string
}

func (n Named) Describe() string { return "named " + n.Name }
func (n *Named) Rename(s string) { n.Name = s }

type Animal struct {
	Named
	Legs int
}

func (a Animal) Sound() string { return "..." }

type Dog struct {
	Animal
	*Counter
	Breed string
}

func (d Dog) Sound() string { return "woof (" + d.Animal.Sound() + ")" }
func (d Dog) Describe() string {
	return "dog " + d.Name + "/" + d.Named.Describe()
}

type Speaker interface{ Sound() string }

type Robot struct {
	Speaker // embedded interface
	Model   string
}

func (r Robot) Sound() string {
	if r.Speaker == nil {
		return "beep"
	}
	return "beep+" + r.Speaker.Sound()
}

type Loud struct{ Speaker }

type Mixed struct {
	a bool
	b int64
	c bool
	d int16
	e byte
	f float32
}

type Packed struct {
	b       int64
	f       float32
	d       int16
	a, c, e bool
}

type Vec struct{ X, Y float64 }

func (v Vec) Add(w Vec) Vec       { return Vec{v.X + w.X, v.Y + w.Y} }
func (v Vec) Scale(k float64) Vec { return Vec{v.X * k, v.Y * k} }
func (v *Vec) Normalize()         { l := v.X*v.X + v.Y*v.Y; v.X /= l; v.Y /= l }
func (v Vec) String() string      { return fmt.Sprintf("(%g,%g)", v.X, v.Y) }

type Other struct{ X, Y float64 }

type Node struct {
	Val   int
	Left  *Node
	Right *Node
}

func (n *Node) Sum() int {
	if n == nil { // methods can be called on nil pointers
		return 0
	}
	return n.Val + n.Left.Sum() + n.Right.Sum()
}

var legs = 4

func init() {
	if len(os.Args) > 1000 {
		legs = 3
	}
}

func main() {
	fmt.Println("-- receivers")
	c := Counter{}
	c.Inc() // (&c).Inc()
	c.Inc()
	c2 := c.IncCopy()
	fmt.Println(c.Get(), c2.Get(), c.hits, c2.hits)
	pc := &c
	fmt.Println(pc.Get(), pc.Add(5).Add(10).Get(), c.n)
	get := c.Get // method value: copies c now
	inc := c.Inc // bound to &c
	inc()
	inc()
	fmt.Println(get(), c.Get(), c.hits)
	getExpr := Counter.Get
	incExpr := (*Counter).Inc
	incExpr(&c)
	fmt.Println(getExpr(c), (*Counter).Get(&c))
	fmt.Println("-- embedding")
	d := Dog{Animal: Animal{Named{"rex"}, legs}, Counter: &Counter{}, Breed: "lab"}
	fmt.Println(d.Name, d.Legs, d.Breed, d.Animal.Named.Name)
	fmt.Println(d.Sound(), "|", d.Animal.Sound(), "|", d.Describe(), "|", d.Animal.Describe())
	d.Rename("max") // promoted pointer method on addressable embedded value
	d.Inc()         // promoted through *Counter
	d.Inc()
	fmt.Println(d.Name, d.Get(), d.n, d.Counter.hits)
	d2 := d // copies Animal, shares *Counter
	d2.Rename("copy")
	d2.Inc()
	fmt.Println(d.Name, d2.Name, d.Get(), d2.Get())
	var sp Speaker = d
	fmt.Println(sp.Sound())
	sp = d.Animal
	fmt.Println(sp.Sound())
	fmt.Printf("%+v\n", d.Animal)
	fmt.Println("-- embedded interfaces")
	r := Robot{Model: "r1"}
	fmt.Println(r.Sound())
	r.Speaker = d
	fmt.Println(r.Sound())
	l := Loud{r}
	fmt.Println(l.Sound(), l.Speaker.(Robot).Model)
	var chain Speaker = Loud{Loud{Robot{Speaker: Animal{}}}}
	fmt.Println(chain.Sound())
	fmt.Println("-- equality and anonymous structs")
	a1 := Animal{Named{"a"}, 4}
	a2 := Animal{Named{"a"}, 4}
	a3 := a1
	a3.Legs = 3
	fmt.Println(a1 == a2, a1 == a3, a1.Named == a3.Named)
	anon := struct {
		A int
		B string
	}{1, "x"}
	anon2 := struct {
		A int
		B string
	}{1, "x"}
	fmt.Println(anon == anon2, anon)
	points := []struct{ X, Y int }{{1, 2}, {3, 4}, {5, 6}}
	sum := 0
	for _, p := range points {
		sum += p.X * p.Y
	}
	fmt.Println(points, sum)
	v := Vec{3, 4}
	o := Other(v) // identical underlying type
	o.X = 30
	back := Vec(o)
	fmt.Println(v, o, back, v.Add(back).Scale(0.5))
	v.Normalize()
	fmt.Println(v)
	fmt.Println("-- zero values and pointers")
	var z Dog
	fmt.Println(z.Name == "", z.Legs, z.Counter == nil, z.Breed == "")
	var zn *Node
	fmt.Println(zn.Sum())
	tree := &Node{1, &Node{2, &Node{4, nil, nil}, nil}, &Node{3, nil, &Node{5, nil, nil}}}
	fmt.Println(tree.Sum(), tree.Left.Sum(), tree.Right.Right.Sum())
	np := &tree.Left.Left.Val
	*np = 40
	fmt.Println(tree.Sum())
	fmt.Println("-- layout")
	m := Mixed{true, 1 << 40, false, -3, 255, 1.5}
	pk := Packed{m.b, m.f, m.d, m.a, m.c, m.e != 0}
	fmt.Println(unsafe.Sizeof(m), unsafe.Sizeof(pk), unsafe.Alignof(m.b),
		unsafe.Offsetof(m.b), unsafe.Offsetof(m.d), unsafe.Offsetof(m.f),
		unsafe.Offsetof(pk.a), unsafe.Sizeof(struct{}{}))
	fmt.Printf("%+v\n%+v\n", m, pk)
	ms := make([]Mixed, 5)
	for i := range ms {
		ms[i] = Mixed{i%2 == 0, int64(i) << 33, i%3 == 0, int16(-i), byte(i * 50), float32(i) / 4}
	}
	ms[1], ms[3] = ms[3], ms[1]
	for _, x := range ms {
		fmt.Printf("%v ", x)
	}
	fmt.Println()
	fmt.Println("-- method values in a table")
	ops := []func(Vec) Vec{
		Vec{1, 1}.Add,
		Vec{-1, 2}.Add,
		func(w Vec) Vec { return w.Scale(2) },
	}
	cur := Vec{0.5, 0.25}
	for i := 0; i < 6; i++ {
		cur = ops[i%len(ops)](cur)
		fmt.Println(i, cur)
	}
}
