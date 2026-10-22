// container/list, container/heap and container/ring.  The doubly linked list
// is driven through every mutator (PushFront/Back, InsertBefore/After,
// MoveToFront/Back, MoveBefore/After, Remove, PushBackList) and walked in
// both directions after each step, and used as the recency list of an LRU
// cache.  The heap is a priority queue with Fix and Remove at arbitrary
// positions, and a k-way merge of sorted runs.  The ring is filled, moved,
// linked and unlinked, and solves the Josephus problem.  All values travel
// through `any`, so every element is boxed and asserted back.
package main

import (
	"container/heap"
	"container/list"
	"container/ring"
	"fmt"
	"os"
	"strings"
)

var n0 = 10

func init() {
	if len(os.Args) > 1000 {
		n0 = 11
	}
}

func dump(l *list.List) string {
	var f, b []string
	for e := l.Front(); e != nil; e = e.Next() {
		f = append(f, fmt.Sprint(e.Value))
	}
	for e := l.Back(); e != nil; e = e.Prev() {
		b = append(b, fmt.Sprint(e.Value))
	}
	return fmt.Sprintf("%d [%s] [%s]", l.Len(), strings.Join(f, " "), strings.Join(b, " "))
}

type lru struct {
	cap   int
	order *list.List
	items map[string]*list.Element
	hits  int
	miss  int
}

type entry struct {
	key string
	val int
}

func (c *lru) get(k string) (int, bool) {
	if e, ok := c.items[k]; ok {
		c.order.MoveToFront(e)
		c.hits++
		return e.Value.(*entry).val, true
	}
	c.miss++
	return 0, false
}

func (c *lru) put(k string, v int) {
	if e, ok := c.items[k]; ok {
		e.Value.(*entry).val = v
		c.order.MoveToFront(e)
		return
	}
	c.items[k] = c.order.PushFront(&entry{k, v})
	if c.order.Len() > c.cap {
		last := c.order.Back()
		c.order.Remove(last)
		delete(c.items, last.Value.(*entry).key)
	}
}

func (c *lru) keys() []string {
	var ks []string
	for e := c.order.Front(); e != nil; e = e.Next() {
		ks = append(ks, e.Value.(*entry).key)
	}
	return ks
}

type task struct {
	name  string
	prio  int
	index int
}

type taskQueue []*task

func (q taskQueue) Len() int { return len(q) }
func (q taskQueue) Less(i, j int) bool {
	if q[i].prio != q[j].prio {
		return q[i].prio > q[j].prio
	}
	return q[i].name < q[j].name
}
func (q taskQueue) Swap(i, j int) {
	q[i], q[j] = q[j], q[i]
	q[i].index = i
	q[j].index = j
}
func (q *taskQueue) Push(x any) {
	t := x.(*task)
	t.index = len(*q)
	*q = append(*q, t)
}
func (q *taskQueue) Pop() any {
	old := *q
	t := old[len(old)-1]
	old[len(old)-1] = nil
	t.index = -1
	*q = old[:len(old)-1]
	return t
}

type cursor struct{ run, pos, val int }
type mergeHeap []cursor

func (h mergeHeap) Len() int { return len(h) }
func (h mergeHeap) Less(i, j int) bool {
	return h[i].val < h[j].val || (h[i].val == h[j].val && h[i].run < h[j].run)
}
func (h mergeHeap) Swap(i, j int) { h[i], h[j] = h[j], h[i] }
func (h *mergeHeap) Push(x any)   { *h = append(*h, x.(cursor)) }
func (h *mergeHeap) Pop() any {
	old := *h
	x := old[len(old)-1]
	*h = old[:len(old)-1]
	return x
}

func ringString(r *ring.Ring) string {
	var parts []string
	r.Do(func(v any) { parts = append(parts, fmt.Sprint(v)) })
	return strings.Join(parts, ",")
}

func main() {
	fmt.Println("-- list")
	l := list.New()
	e1 := l.PushBack(1)
	e2 := l.PushBack(2)
	e0 := l.PushFront(0)
	fmt.Println(dump(l))
	e15 := l.InsertAfter(1.5, e1)
	l.InsertBefore("start", e0)
	fmt.Println(dump(l))
	l.MoveToBack(e0)
	fmt.Println(dump(l))
	l.MoveToFront(e2)
	fmt.Println(dump(l))
	l.MoveBefore(e15, e2)
	l.MoveAfter(e1, e0)
	fmt.Println(dump(l))
	fmt.Println(l.Remove(e15), dump(l))
	fmt.Println(l.Remove(e15), dump(l)) // removing twice is a no-op
	other := list.New()
	for i := 0; i < 3; i++ {
		other.PushBack(fmt.Sprint("o", i))
	}
	l.PushBackList(other)
	l.PushFrontList(other)
	fmt.Println(dump(l))
	for e := l.Front(); e != nil; {
		next := e.Next()
		if s, ok := e.Value.(string); ok && strings.HasPrefix(s, "o") {
			l.Remove(e)
		}
		e = next
	}
	fmt.Println(dump(l))
	l.Init()
	fmt.Println(dump(l), l.Front() == nil)
	fmt.Println("-- LRU")
	c := &lru{cap: 4, order: list.New(), items: map[string]*list.Element{}}
	ops := "a b c a d e a f b c g a e"
	for i, k := range strings.Fields(ops) {
		if _, ok := c.get(k); !ok {
			c.put(k, i*n0)
		}
		fmt.Println(k, c.keys())
	}
	v, ok := c.get("a")
	fmt.Println(v, ok, c.hits, c.miss)
	fmt.Println("-- priority queue")
	tq := taskQueue{}
	tasks := map[string]*task{}
	for i, name := range strings.Fields("write test build lint deploy review merge plan") {
		t := &task{name: name, prio: (i * 7) % 5}
		tasks[name] = t
		heap.Push(&tq, t)
	}
	tasks["deploy"].prio = 10
	heap.Fix(&tq, tasks["deploy"].index)
	tasks["write"].prio = -1
	heap.Fix(&tq, tasks["write"].index)
	removed := heap.Remove(&tq, tasks["lint"].index).(*task)
	fmt.Println("removed", removed.name, removed.index)
	for tq.Len() > 0 {
		t := heap.Pop(&tq).(*task)
		fmt.Print(t.name, ":", t.prio, " ")
	}
	fmt.Println()
	runs := [][]int{}
	for r := 0; r < 5; r++ {
		var run []int
		for i := 0; i < 4+r; i++ {
			run = append(run, r+i*(r+2))
		}
		runs = append(runs, run)
	}
	h := &mergeHeap{}
	for r, run := range runs {
		heap.Push(h, cursor{r, 0, run[0]})
	}
	var merged []int
	for h.Len() > 0 {
		cur := heap.Pop(h).(cursor)
		merged = append(merged, cur.val)
		if cur.pos+1 < len(runs[cur.run]) {
			heap.Push(h, cursor{cur.run, cur.pos + 1, runs[cur.run][cur.pos+1]})
		}
	}
	fmt.Println(merged)
	fmt.Println("-- ring")
	r := ring.New(n0 - 4)
	for i := 0; i < r.Len(); i++ {
		r.Value = i * i
		r = r.Next()
	}
	fmt.Println(r.Len(), ringString(r))
	fmt.Println(ringString(r.Move(2)), ringString(r.Move(-1)))
	s := ring.New(3)
	for i := 0; i < 3; i++ {
		s.Value = fmt.Sprint("s", i)
		s = s.Next()
	}
	r.Link(s)
	fmt.Println(r.Len(), ringString(r))
	un := r.Unlink(4)
	fmt.Println(r.Len(), ringString(r), un.Len(), ringString(un))
	for _, k := range []int{2, 3, 7} {
		people := ring.New(n0 + 31)
		for i := 1; i <= people.Len(); i++ {
			people.Value = i
			people = people.Next()
		}
		people = people.Prev() // so that the first Move lands on person k
		var order []int
		for people.Len() > 1 {
			people = people.Move(k - 1)
			gone := people.Unlink(1)
			order = append(order, gone.Value.(int))
		}
		fmt.Println("josephus", k, people.Value, order[:8])
	}
}
