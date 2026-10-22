// Struct embedding and interface embedding.  rect embeds base by value, box
// embeds rect and disk embeds a *counter, so methods and fields are promoted
// through one level, two levels and a pointer; shape is an interface built
// from two embedded ones, and solid embeds shape.  TinyGo compiles the whole
// program at once, so the interface calls in survey are not `call_indirect`:
// each is a chain of `i32.eq` tests of the type code against the concrete
// types that have the method, with the matching method inlined into its arm,
// and the assertion to solid is the same kind of chain.  A promoted field or
// method is a load at the embedded field's offset, and the promoted pointer
// method touch is inlined into touchAll.
//
// This is how TinyGo programs dispatch.  The cost is a compare chain and
// loads through boxed receivers rather than a table call, which typeswitch
// shows for an explicit switch and this shows for method calls.
package main

// Interface embedding: shape is sizer plus namer, and solid adds one more.
type sizer interface{ size() int }
type namer interface{ name() int }
type shape interface {
	sizer
	namer
}
type solid interface {
	shape
	volume(depth int) int
}

// A base with a value-receiver method and a pointer-receiver method.
type base struct {
	id, hits int
}

func (b base) name() int { return b.id * 31 }
func (b *base) touch()   { b.hits++ }

// rect embeds base by value, so name is promoted and touch is promoted
// through the address of the embedded field.
type rect struct {
	base
	w, h int
}

func (r rect) size() int { return r.w * r.h }

// box embeds rect, so it inherits base's methods two levels down, and
// overrides size; its own volume method makes it a solid.
type box struct {
	rect
	d int
}

func (b box) size() int            { return 2 * (b.w*b.h + b.w*b.d + b.h*b.d) }
func (b box) volume(depth int) int { return b.rect.size() * (b.d + depth) }

// counter is embedded by pointer, so its fields and methods are reached
// through one more load.
type counter struct{ n int }

func (c *counter) add(k int) { c.n += k }

type disk struct {
	*counter
	base
	r int
}

func (d disk) size() int  { d.add(1); return 3 * d.r * d.r }
func (d *disk) name() int { return d.base.name() + d.r }

//go:noinline
func survey(xs []shape) int {
	s := 0
	for _, x := range xs {
		s += x.size()*3 + x.name()
		if v, ok := x.(solid); ok {
			s += v.volume(s & 3)
		}
	}
	return s
}

//go:noinline
func touchAll(rs []rect, bs []box) int {
	s := 0
	for i := range rs {
		rs[i].touch()
		s += rs[i].hits + rs[i].name()
	}
	for i := range bs {
		bs[i].touch()
		s += bs[i].hits + bs[i].rect.base.id
	}
	return s
}

func main() {
	shared := &counter{}
	rs := make([]rect, 64)
	bs := make([]box, 64)
	ds := make([]disk, 64)
	for i := range rs {
		rs[i] = rect{base{i, 0}, i%7 + 1, i%5 + 1}
		bs[i] = box{rect{base{i + 100, 0}, i%3 + 1, i%4 + 1}, i%6 + 1}
		ds[i] = disk{shared, base{i + 200, 0}, i%9 + 1}
	}
	xs := make([]shape, 0, 192)
	for i := 0; i < 64; i++ {
		xs = append(xs, rs[i], bs[i], &ds[i])
	}
	total := 0
	for r := 0; r < 240000; r++ {
		rs[r&63].w = r & 15
		xs[(r*3)%len(xs)] = rs[r&63]
		total += survey(xs)
		total += touchAll(rs, bs)
	}
	println("total =", total, "n =", shared.n)
}
