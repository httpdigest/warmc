// Recursive data structures.  An AVL tree with insertion and deletion (every
// rotation case), printed in shape after each change and checked for
// balance and order; a singly linked list reversed iteratively and
// recursively, merge-sorted, split, and checked for a cycle with Floyd's
// algorithm; a doubly linked list with removal from the middle; an n-ary
// tree built from paths with recursive size and depth; and expression trees
// evaluated, differentiated symbolically and simplified.  Pointer-heavy code:
// field loads through freshly allocated nodes, recursion on both children,
// and nil checks everywhere.
package main

import (
	"fmt"
	"os"
	"sort"
	"strings"
)

// ---- AVL ----

type avl struct {
	key         int
	height      int
	left, right *avl
}

func h(n *avl) int {
	if n == nil {
		return 0
	}
	return n.height
}

func fix(n *avl) { n.height = 1 + max(h(n.left), h(n.right)) }

func rotR(n *avl) *avl {
	l := n.left
	n.left = l.right
	l.right = n
	fix(n)
	fix(l)
	return l
}

func rotL(n *avl) *avl {
	r := n.right
	n.right = r.left
	r.left = n
	fix(n)
	fix(r)
	return r
}

var rotations = map[string]int{}

func balance(n *avl) *avl {
	fix(n)
	bf := h(n.left) - h(n.right)
	if bf > 1 {
		if h(n.left.left) < h(n.left.right) {
			rotations["LR"]++
			n.left = rotL(n.left)
		} else {
			rotations["LL"]++
		}
		return rotR(n)
	}
	if bf < -1 {
		if h(n.right.right) < h(n.right.left) {
			rotations["RL"]++
			n.right = rotR(n.right)
		} else {
			rotations["RR"]++
		}
		return rotL(n)
	}
	return n
}

func insert(n *avl, k int) *avl {
	if n == nil {
		return &avl{key: k, height: 1}
	}
	if k < n.key {
		n.left = insert(n.left, k)
	} else if k > n.key {
		n.right = insert(n.right, k)
	} else {
		return n
	}
	return balance(n)
}

func minNode(n *avl) *avl {
	for n.left != nil {
		n = n.left
	}
	return n
}

func remove(n *avl, k int) *avl {
	if n == nil {
		return nil
	}
	switch {
	case k < n.key:
		n.left = remove(n.left, k)
	case k > n.key:
		n.right = remove(n.right, k)
	default:
		if n.left == nil {
			return n.right
		}
		if n.right == nil {
			return n.left
		}
		m := minNode(n.right)
		n.key = m.key
		n.right = remove(n.right, m.key)
	}
	return balance(n)
}

func shape(n *avl) string {
	if n == nil {
		return "."
	}
	if n.left == nil && n.right == nil {
		return fmt.Sprint(n.key)
	}
	return fmt.Sprintf("(%s %d %s)", shape(n.left), n.key, shape(n.right))
}

func check(n *avl, lo, hi int) (int, bool) {
	if n == nil {
		return 0, true
	}
	if n.key <= lo || n.key >= hi {
		return 0, false
	}
	lh, lok := check(n.left, lo, n.key)
	rh, rok := check(n.right, n.key, hi)
	d := lh - rh
	return 1 + max(lh, rh), lok && rok && d >= -1 && d <= 1 && n.height == 1+max(lh, rh)
}

func inorder(n *avl, out []int) []int {
	if n == nil {
		return out
	}
	out = inorder(n.left, out)
	out = append(out, n.key)
	return inorder(n.right, out)
}

// ---- singly linked list ----

type node struct {
	val  int
	next *node
}

func fromSlice(xs []int) *node {
	var head *node
	for i := len(xs) - 1; i >= 0; i-- {
		head = &node{xs[i], head}
	}
	return head
}

func (l *node) String() string {
	var sb strings.Builder
	sb.WriteByte('[')
	for n := l; n != nil; n = n.next {
		if n != l {
			sb.WriteByte(' ')
		}
		sb.WriteString(fmt.Sprint(n.val))
	}
	sb.WriteByte(']')
	return sb.String()
}

func reverseIter(l *node) *node {
	var prev *node
	for l != nil {
		l.next, prev, l = prev, l, l.next
	}
	return prev
}

func reverseRec(l *node) *node {
	if l == nil || l.next == nil {
		return l
	}
	rest := reverseRec(l.next)
	l.next.next = l
	l.next = nil
	return rest
}

func split(l *node) (*node, *node) {
	slow, fast := l, l.next
	for fast != nil && fast.next != nil {
		slow, fast = slow.next, fast.next.next
	}
	second := slow.next
	slow.next = nil
	return l, second
}

func merge(a, b *node) *node {
	dummy := &node{}
	t := dummy
	for a != nil && b != nil {
		if a.val <= b.val {
			t.next, a = a, a.next
		} else {
			t.next, b = b, b.next
		}
		t = t.next
	}
	if a != nil {
		t.next = a
	} else {
		t.next = b
	}
	return dummy.next
}

func mergeSort(l *node) *node {
	if l == nil || l.next == nil {
		return l
	}
	a, b := split(l)
	return merge(mergeSort(a), mergeSort(b))
}

func hasCycle(l *node) (bool, int) {
	slow, fast := l, l
	steps := 0
	for fast != nil && fast.next != nil {
		slow, fast = slow.next, fast.next.next
		steps++
		if slow == fast {
			return true, steps
		}
	}
	return false, steps
}

// ---- doubly linked list ----

type dnode struct {
	val        string
	prev, next *dnode
}

type dlist struct{ head, tail *dnode }

func (d *dlist) push(v string) *dnode {
	n := &dnode{val: v, prev: d.tail}
	if d.tail != nil {
		d.tail.next = n
	} else {
		d.head = n
	}
	d.tail = n
	return n
}

func (d *dlist) remove(n *dnode) {
	if n.prev != nil {
		n.prev.next = n.next
	} else {
		d.head = n.next
	}
	if n.next != nil {
		n.next.prev = n.prev
	} else {
		d.tail = n.prev
	}
}

func (d *dlist) both() string {
	var f, b []string
	for n := d.head; n != nil; n = n.next {
		f = append(f, n.val)
	}
	for n := d.tail; n != nil; n = n.prev {
		b = append(b, n.val)
	}
	return strings.Join(f, ",") + " / " + strings.Join(b, ",")
}

// ---- n-ary tree ----

type dir struct {
	name     string
	size     int
	children map[string]*dir
}

func (d *dir) add(path string, size int) {
	parts := strings.Split(path, "/")
	cur := d
	for _, p := range parts[:len(parts)-1] {
		next, ok := cur.children[p]
		if !ok {
			next = &dir{name: p, children: map[string]*dir{}}
			cur.children[p] = next
		}
		cur = next
	}
	cur.children[parts[len(parts)-1]] = &dir{name: parts[len(parts)-1], size: size}
}

func (d *dir) total() int {
	t := d.size
	for _, c := range d.children {
		t += c.total()
	}
	return t
}

func (d *dir) print(indent string) {
	fmt.Printf("%s%s %d\n", indent, d.name, d.total())
	names := make([]string, 0, len(d.children))
	for n := range d.children {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		d.children[n].print(indent + "  ")
	}
}

// ---- expression trees ----

type expr interface {
	eval(x float64) float64
	deriv() expr
	String() string
}

type num float64
type variable struct{}
type add struct{ a, b expr }
type mul struct{ a, b expr }
type pow struct {
	a expr
	n int
}

func (n num) eval(float64) float64 { return float64(n) }
func (n num) deriv() expr          { return num(0) }
func (n num) String() string       { return fmt.Sprint(float64(n)) }

func (variable) eval(x float64) float64 { return x }
func (variable) deriv() expr            { return num(1) }
func (variable) String() string         { return "x" }

func (e add) eval(x float64) float64 { return e.a.eval(x) + e.b.eval(x) }
func (e add) deriv() expr            { return simplify(add{e.a.deriv(), e.b.deriv()}) }
func (e add) String() string         { return "(" + e.a.String() + " + " + e.b.String() + ")" }

func (e mul) eval(x float64) float64 { return e.a.eval(x) * e.b.eval(x) }
func (e mul) deriv() expr {
	return simplify(add{simplify(mul{e.a.deriv(), e.b}), simplify(mul{e.a, e.b.deriv()})})
}
func (e mul) String() string { return e.a.String() + "*" + e.b.String() }

func (e pow) eval(x float64) float64 {
	r := 1.0
	for i := 0; i < e.n; i++ {
		r *= e.a.eval(x)
	}
	return r
}
func (e pow) deriv() expr {
	return simplify(mul{simplify(mul{num(e.n), simplify(pow{e.a, e.n - 1})}), e.a.deriv()})
}
func (e pow) String() string { return e.a.String() + "^" + fmt.Sprint(e.n) }

func simplify(e expr) expr {
	switch t := e.(type) {
	case add:
		if a, ok := t.a.(num); ok && a == 0 {
			return t.b
		}
		if b, ok := t.b.(num); ok && b == 0 {
			return t.a
		}
		if a, ok := t.a.(num); ok {
			if b, ok := t.b.(num); ok {
				return a + b
			}
		}
	case mul:
		if a, ok := t.a.(num); ok && (a == 0) {
			return num(0)
		}
		if b, ok := t.b.(num); ok && (b == 0) {
			return num(0)
		}
		if a, ok := t.a.(num); ok && a == 1 {
			return t.b
		}
		if b, ok := t.b.(num); ok && b == 1 {
			return t.a
		}
		if a, ok := t.a.(num); ok {
			if b, ok := t.b.(num); ok {
				return a * b
			}
		}
	case pow:
		if t.n == 0 {
			return num(1)
		}
		if t.n == 1 {
			return t.a
		}
	}
	return e
}

var mult = 37

func init() {
	if len(os.Args) > 1000 {
		mult = 41
	}
}

func main() {
	fmt.Println("-- AVL")
	var root *avl
	keys := []int{}
	for i := 0; i < 40; i++ {
		k := (i * mult) % 101
		keys = append(keys, k)
		root = insert(root, k)
		if i < 12 || i%7 == 0 {
			ht, ok := check(root, -1, 1000)
			fmt.Println(i, k, ht, ok, shape(root))
		}
	}
	for _, k := range []int{1, 2, 3, 4, 5, 6, 7} {
		root = insert(root, 200+k) // ascending run: RR rotations
	}
	for _, k := range []int{-1, -3, -2} {
		root = insert(root, k+1000-1000) // rotations around the minimum
	}
	ht, ok := check(root, -1000, 1000)
	fmt.Println(ht, ok, len(inorder(root, nil)), inorder(root, nil)[:12])
	for i, k := range keys {
		if i%3 == 0 {
			root = remove(root, k)
			ht, ok := check(root, -1000, 1000)
			if i%9 == 0 {
				fmt.Println("del", k, ht, ok, shape(root))
			}
			if !ok {
				fmt.Println("BROKEN after deleting", k)
			}
		}
	}
	root = remove(root, 12345) // absent
	fmt.Println(inorder(root, nil))
	fmt.Println(rotations["LL"], rotations["RR"], rotations["LR"], rotations["RL"])
	fmt.Println("-- singly linked")
	xs := []int{}
	for i := 0; i < 25; i++ {
		xs = append(xs, (i*mult)%29-10)
	}
	l := fromSlice(xs)
	fmt.Println(l)
	l = reverseIter(l)
	fmt.Println(l)
	l = reverseRec(l)
	fmt.Println(l)
	l = mergeSort(l)
	fmt.Println(l)
	a, b := split(fromSlice([]int{1, 2, 3, 4, 5}))
	fmt.Println(a, b, merge(fromSlice([]int{1, 4, 9}), fromSlice([]int{2, 3, 10, 11})))
	cyc := fromSlice([]int{1, 2, 3, 4, 5, 6, 7})
	fmt.Println(hasCycle(cyc))
	tail := cyc
	for tail.next != nil {
		tail = tail.next
	}
	tail.next = cyc.next.next // 7 -> 3
	fmt.Println(hasCycle(cyc))
	tail.next = nil
	fmt.Println(cyc, mergeSort(nil) == nil)
	fmt.Println("-- doubly linked")
	var dl dlist
	nodes := []*dnode{}
	for _, w := range strings.Fields("a b c d e f g") {
		nodes = append(nodes, dl.push(w))
	}
	fmt.Println(dl.both())
	for _, i := range []int{3, 0, 6, 1} {
		dl.remove(nodes[i])
		fmt.Println(dl.both())
	}
	fmt.Println("-- directory tree")
	root2 := &dir{name: "/", children: map[string]*dir{}}
	files := map[string]int{
		"usr/bin/go": 100, "usr/bin/vi": 20, "usr/lib/libc.so": 300, "etc/hosts": 1,
		"etc/ssh/config": 5, "home/u/a.txt": 7, "home/u/docs/b.md": 11, "home/v/c": 13,
	}
	paths := make([]string, 0, len(files))
	for p := range files {
		paths = append(paths, p)
	}
	sort.Strings(paths)
	for _, p := range paths {
		root2.add(p, files[p])
	}
	root2.print("")
	fmt.Println("-- expressions")
	x := variable{}
	es := []expr{
		add{mul{num(3), pow{x, 2}}, add{mul{num(2), x}, num(1)}},
		mul{add{x, num(1)}, add{x, num(-1)}},
		pow{add{mul{num(2), x}, num(3)}, 3},
		mul{pow{x, 3}, pow{x, 2}},
	}
	for _, e := range es {
		d := e.deriv()
		dd := d.deriv()
		fmt.Println(e, "|", d, "|", dd)
		for _, v := range []float64{-2, 0, 0.5, 3} {
			fmt.Print(" ", e.eval(v), "/", d.eval(v), "/", dd.eval(v))
		}
		fmt.Println()
	}
}
