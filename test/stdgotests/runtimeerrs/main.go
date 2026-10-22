// Run-time panics the Go runtime raises on its own, each recovered and
// printed with its exact error string: index out of range (constant and
// variable indices, arrays, slices, strings), every slice-bounds form
// (s[a:], s[:b], s[a:b], s[a:b:c], with a > b and b > cap), nil map write,
// integer division and modulo by zero at every width, nil pointer
// dereference through fields, methods and arrays, failed type assertions
// (concrete, interface, missing method), send on and close of a closed
// channel, close of a nil channel, negative make lengths, negative shift
// counts, slice-to-array conversion length mismatch, and unhashable map keys.
//
// On wasm address 0 is an ordinary readable address, so Go checks for nil
// explicitly; the index and bounds checks are explicit branches into
// runtime.panicIndex and friends with the operands passed along so the
// message can name them.  Each case also confirms the panic value satisfies
// runtime.Error.
package main

import (
	"errors"
	"fmt"
	"runtime"
)

type shape interface{ Area() float64 }
type namer interface{ Name() string }
type sq struct{ s float64 }

func (q sq) Area() float64 { return q.s * q.s }

type node struct {
	val  int
	next *node
}

func (n *node) Val() int { return n.val }

var sink any

func try(name string, f func()) {
	defer func() {
		r := recover()
		if r == nil {
			fmt.Printf("%-28s no panic\n", name)
			return
		}
		re, isRE := r.(runtime.Error)
		msg := fmt.Sprint(r)
		if isRE {
			msg = re.Error()
		}
		var ae *runtime.TypeAssertionError
		isTA := false
		if e, ok := r.(error); ok {
			isTA = errors.As(e, &ae)
		}
		fmt.Printf("%-28s runtime.Error=%-5v typeassert=%-5v %s\n", name, isRE, isTA, msg)
	}()
	f()
}

//go:noinline
func idx(i int) int { return i }

//go:noinline
func zero() int { return 0 }

func main() {
	s := []int{1, 2, 3, 4, 5}
	arr := [4]string{"a", "b", "c", "d"}
	str := "hello"

	try("slice index 5", func() { sink = s[idx(5)] })
	try("slice index -1", func() { sink = s[idx(-1)] })
	try("slice index 100", func() { sink = s[idx(100)] })
	try("array index 4", func() { sink = arr[idx(4)] })
	try("string index 5", func() { sink = str[idx(5)] })
	try("nil slice index 0", func() { var n []int; sink = n[idx(0)] })
	try("slice write index 7", func() { s[idx(7)] = 1 })
	try("2d index", func() { g := [][]int{{1}, {2, 3}}; sink = g[1][idx(2)] })

	try("s[6:]", func() { sink = s[idx(6):] })
	try("s[:6]", func() { sink = s[:idx(6)] })
	try("s[3:2]", func() { sink = s[idx(3):idx(2)] })
	try("s[1:2:6]", func() { sink = s[1:2:idx(6)] })
	try("s[1:4:3]", func() { sink = s[1:idx(4):3] })
	try("s[3:1:4]", func() { sink = s[idx(3):1:4] })
	try("str[2:9]", func() { sink = str[2:idx(9)] })
	try("str[4:2]", func() { sink = str[idx(4):idx(2)] })
	try("arr[:5]", func() { sink = arr[:idx(5)] })
	try("s[-1:] via var", func() { sink = s[idx(-1):] })
	try("s[:cap] ok", func() { sink = s[:cap(s)] })
	try("s[5:] ok (empty)", func() { sink = s[idx(5):] })

	try("nil map write", func() { var m map[string]int; m["x"] = 1 })
	try("nil map read ok", func() { var m map[string]int; sink = m["x"] })
	try("nil map delete ok", func() { var m map[string]int; delete(m, "x") })

	try("int div by zero", func() { sink = 10 / zero() })
	try("int mod by zero", func() { sink = 10 % zero() })
	try("int8 div by zero", func() { sink = int8(10) / int8(zero()) })
	try("int16 mod by zero", func() { sink = int16(10) % int16(zero()) })
	try("int32 div by zero", func() { sink = int32(10) / int32(zero()) })
	try("int64 div by zero", func() { sink = int64(10) / int64(zero()) })
	try("uint8 div by zero", func() { sink = uint8(10) / uint8(zero()) })
	try("uint32 mod by zero", func() { sink = uint32(10) % uint32(zero()) })
	try("uint64 div by zero", func() { sink = uint64(10) / uint64(zero()) })
	try("float div by zero ok", func() { sink = 1.0 / float64(zero()) })

	try("nil ptr field read", func() { var p *node; sink = p.val })
	try("nil ptr field write", func() { var p *node; p.val = 3 })
	try("nil ptr method", func() { var p *node; sink = p.Val() })
	try("nil ptr chain", func() { p := &node{1, nil}; sink = p.next.next })
	try("nil array ptr index", func() { var p *[3]int; sink = p[idx(1)] })
	try("nil array ptr len ok", func() { var p *[3]int; sink = len(p) })
	try("nil func call", func() { var f func() int; sink = f() })
	try("nil iface method", func() { var sh shape; sink = sh.Area() })
	try("nil deref *int", func() { var p *int; sink = *p })

	try("assert string from int", func() { var x any = 5; sink = x.(string) })
	try("assert from nil iface", func() { var x any; sink = x.(int) })
	try("assert missing method", func() { var x any = sq{2}; sink = x.(namer) })
	try("assert iface to concrete", func() { var x shape = sq{2}; sink = x.(*sq) })
	try("assert ok form", func() { var x any = 5; _, ok := x.(string); sink = ok })
	try("assert struct ok", func() { var x shape = sq{3}; sink = x.(sq).Area() })

	try("send on closed chan", func() { c := make(chan int, 1); close(c); c <- 1 })
	try("close closed chan", func() { c := make(chan int); close(c); close(c) })
	try("close nil chan", func() { var c chan int; close(c) })

	try("make negative len", func() { sink = make([]int, idx(-1)) })
	try("make len > cap", func() { sink = make([]int, idx(5), idx(2)) })
	try("make huge len", func() { sink = make([]int64, idx(1<<62)) })
	try("make map negative ok?", func() { sink = make(map[int]int, idx(-1)) })
	try("make chan negative", func() { sink = make(chan int, idx(-1)) })

	try("negative shift", func() { x := 1; sink = x << idx(-1) })
	try("negative shift right", func() { x := int32(1); sink = x >> idx(-3) })
	try("huge shift ok", func() { x := 1; sink = x << idx(200) })

	try("slice to array short", func() { sink = [4]int(s[:idx(3)]) })
	try("slice to array ptr short", func() { sink = (*[6]int)(s) })
	try("slice to array ok", func() { sink = [3]int(s[:idx(3)]) })

	try("unhashable map key", func() {
		m := map[any]int{}
		var k any = []int{1}
		m[k] = 1
	})
	try("unhashable in iface compare", func() {
		var a, b any = []int{1}, []int{1}
		sink = a == b
	})
	try("map of func key compare", func() {
		m := map[any]bool{}
		m[any(func() {})] = true
	})

	try("custom panic error", func() { panic(fmt.Errorf("plain error")) })
	try("append to nil ok", func() { var n []int; sink = append(n, 1) })
	try("copy overlap ok", func() { copy(s[1:], s); sink = s })
	fmt.Println("s after overlapping copy", s)

	// Continue working after all of that.
	total := 0
	for i := range 100 {
		func() {
			defer func() {
				if recover() != nil {
					total += 1000
				}
			}()
			total += 100 / (i % 10)
		}()
	}
	fmt.Println("total after recovered divisions", total)
}
