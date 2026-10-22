// container/heap, container/list and container/ring, each driven through a
// realistic use: a priority queue with in-place priority updates
// (heap.Fix) and removal (heap.Remove), a k-way merge of sorted lists, a
// min-heap of intervals for meeting-room allocation, Dijkstra on a small grid
// graph; a doubly linked list used as an LRU with MoveToFront, InsertBefore/
// After, MoveAfter, PushBackList and removal while iterating; and a ring
// used for a Josephus elimination, rotations with Move, Link/Unlink and Do.
//
// Each operation's effect is printed.  The heap code calls back through the
// heap.Interface methods on every sift step, so this is mostly interface
// dispatch over small slices.
package main

import (
	"container/heap"
	"container/list"
	"container/ring"
	"fmt"
	"sort"
	"strings"
)

type Item struct {
	value    string
	priority int
	index    int
}

type PQ []*Item

func (pq PQ) Len() int { return len(pq) }
func (pq PQ) Less(i, j int) bool {
	if pq[i].priority != pq[j].priority {
		return pq[i].priority > pq[j].priority
	}
	return pq[i].value < pq[j].value
}
func (pq PQ) Swap(i, j int) {
	pq[i], pq[j] = pq[j], pq[i]
	pq[i].index = i
	pq[j].index = j
}
func (pq *PQ) Push(x any) {
	it := x.(*Item)
	it.index = len(*pq)
	*pq = append(*pq, it)
}
func (pq *PQ) Pop() any {
	old := *pq
	n := len(old)
	it := old[n-1]
	old[n-1] = nil
	it.index = -1
	*pq = old[:n-1]
	return it
}

type IntHeap []int

func (h IntHeap) Len() int           { return len(h) }
func (h IntHeap) Less(i, j int) bool { return h[i] < h[j] }
func (h IntHeap) Swap(i, j int)      { h[i], h[j] = h[j], h[i] }
func (h *IntHeap) Push(x any)        { *h = append(*h, x.(int)) }
func (h *IntHeap) Pop() any {
	old := *h
	x := old[len(old)-1]
	*h = old[:len(old)-1]
	return x
}

type cursor struct{ list, pos, val int }
type mergeHeap []cursor

func (h mergeHeap) Len() int { return len(h) }
func (h mergeHeap) Less(i, j int) bool {
	return h[i].val < h[j].val || (h[i].val == h[j].val && h[i].list < h[j].list)
}
func (h mergeHeap) Swap(i, j int) { h[i], h[j] = h[j], h[i] }
func (h *mergeHeap) Push(x any)   { *h = append(*h, x.(cursor)) }
func (h *mergeHeap) Pop() any {
	old := *h
	x := old[len(old)-1]
	*h = old[:len(old)-1]
	return x
}

type dist struct{ node, d int }
type distHeap []dist

func (h distHeap) Len() int { return len(h) }
func (h distHeap) Less(i, j int) bool {
	return h[i].d < h[j].d || (h[i].d == h[j].d && h[i].node < h[j].node)
}
func (h distHeap) Swap(i, j int) { h[i], h[j] = h[j], h[i] }
func (h *distHeap) Push(x any)   { *h = append(*h, x.(dist)) }
func (h *distHeap) Pop() any {
	old := *h
	x := old[len(old)-1]
	*h = old[:len(old)-1]
	return x
}

func listString(l *list.List) string {
	var parts []string
	for e := l.Front(); e != nil; e = e.Next() {
		parts = append(parts, fmt.Sprint(e.Value))
	}
	return "[" + strings.Join(parts, " ") + "]"
}

func listBackString(l *list.List) string {
	var parts []string
	for e := l.Back(); e != nil; e = e.Prev() {
		parts = append(parts, fmt.Sprint(e.Value))
	}
	return "[" + strings.Join(parts, " ") + "]"
}

func ringString(r *ring.Ring) string {
	var parts []string
	r.Do(func(v any) { parts = append(parts, fmt.Sprint(v)) })
	return "(" + strings.Join(parts, " ") + ")"
}

func main() {
	// Priority queue with updates.
	items := map[string]int{"banana": 3, "apple": 2, "pear": 4, "kiwi": 1, "fig": 4, "plum": 0}
	names := make([]string, 0, len(items))
	for k := range items {
		names = append(names, k)
	}
	sort.Strings(names)
	pq := PQ{}
	refs := map[string]*Item{}
	for _, n := range names {
		it := &Item{value: n, priority: items[n]}
		refs[n] = it
		heap.Push(&pq, it)
	}
	refs["kiwi"].priority = 10
	heap.Fix(&pq, refs["kiwi"].index)
	refs["pear"].priority = -1
	heap.Fix(&pq, refs["pear"].index)
	removed := heap.Remove(&pq, refs["apple"].index).(*Item)
	fmt.Println("removed", removed.value, removed.priority, removed.index)
	heap.Push(&pq, &Item{value: "cherry", priority: 4})
	for pq.Len() > 0 {
		it := heap.Pop(&pq).(*Item)
		fmt.Printf("pq pop %-7s prio %2d\n", it.value, it.priority)
	}

	// IntHeap: heapsort and top-k.
	h := &IntHeap{5, 2, 8, 1, 9, 3, 7, 4, 6, 0, 5, 5}
	heap.Init(h)
	fmt.Println("heap after Init, min:", (*h)[0], "valid:", func() bool {
		for i := 1; i < h.Len(); i++ {
			if (*h)[(i-1)/2] > (*h)[i] {
				return false
			}
		}
		return true
	}())
	var sorted []int
	for h.Len() > 0 {
		sorted = append(sorted, heap.Pop(h).(int))
	}
	fmt.Println("heapsort", sorted)
	top := &IntHeap{}
	seed := uint32(7)
	for i := 0; i < 200; i++ {
		seed = seed*1664525 + 1013904223
		v := int(seed >> 20)
		heap.Push(top, v)
		if top.Len() > 5 {
			heap.Pop(top)
		}
	}
	topk := append([]int(nil), (*top)...)
	sort.Sort(sort.Reverse(sort.IntSlice(topk)))
	fmt.Println("top 5 of 200", topk)

	// K-way merge.
	lists := [][]int{{1, 4, 9, 16}, {2, 3, 5, 7, 11, 13}, {}, {0, 10, 20}, {4, 4, 4}}
	mh := &mergeHeap{}
	for i, l := range lists {
		if len(l) > 0 {
			heap.Push(mh, cursor{i, 0, l[0]})
		}
	}
	var merged []string
	for mh.Len() > 0 {
		c := heap.Pop(mh).(cursor)
		merged = append(merged, fmt.Sprintf("%d(L%d)", c.val, c.list))
		if c.pos+1 < len(lists[c.list]) {
			heap.Push(mh, cursor{c.list, c.pos + 1, lists[c.list][c.pos+1]})
		}
	}
	fmt.Println("k-way merge", strings.Join(merged, " "))

	// Meeting rooms.
	meetings := [][2]int{{0, 30}, {5, 10}, {15, 20}, {10, 25}, {25, 35}, {30, 40}, {2, 7}, {7, 12}}
	sort.Slice(meetings, func(i, j int) bool { return meetings[i][0] < meetings[j][0] })
	ends := &IntHeap{}
	maxRooms := 0
	for _, m := range meetings {
		for ends.Len() > 0 && (*ends)[0] <= m[0] {
			heap.Pop(ends)
		}
		heap.Push(ends, m[1])
		maxRooms = max(maxRooms, ends.Len())
		fmt.Printf("meeting %v rooms in use %d\n", m, ends.Len())
	}
	fmt.Println("rooms needed", maxRooms)

	// Dijkstra on a 6x6 grid with weights.
	const W = 6
	weight := func(n int) int { return (n*7+3)%9 + 1 }
	distArr := make([]int, W*W)
	for i := range distArr {
		distArr[i] = 1 << 30
	}
	prev := make([]int, W*W)
	distArr[0] = 0
	dh := &distHeap{{0, 0}}
	for dh.Len() > 0 {
		cur := heap.Pop(dh).(dist)
		if cur.d > distArr[cur.node] {
			continue
		}
		r, c := cur.node/W, cur.node%W
		for _, d := range [][2]int{{0, 1}, {1, 0}, {0, -1}, {-1, 0}} {
			nr, nc := r+d[0], c+d[1]
			if nr < 0 || nc < 0 || nr >= W || nc >= W {
				continue
			}
			nn := nr*W + nc
			nd := cur.d + weight(nn)
			if nd < distArr[nn] {
				distArr[nn] = nd
				prev[nn] = cur.node
				heap.Push(dh, dist{nn, nd})
			}
		}
	}
	for r := 0; r < W; r++ {
		fmt.Printf("dijkstra row %d: %v\n", r, distArr[r*W:(r+1)*W])
	}
	var path []int
	for n := W*W - 1; n != 0; n = prev[n] {
		path = append(path, n)
	}
	fmt.Println("path back from corner", path)

	// container/list.
	l := list.New()
	e4 := l.PushBack(4)
	e1 := l.PushFront(1)
	l.InsertBefore(3, e4)
	l.InsertAfter(2, e1)
	l.PushBack(5)
	fmt.Println("list", listString(l), "len", l.Len(), "back", listBackString(l))
	l.MoveToFront(e4)
	fmt.Println("MoveToFront(4)", listString(l))
	l.MoveToBack(e1)
	fmt.Println("MoveToBack(1)", listString(l))
	l.MoveAfter(e4, l.Back())
	fmt.Println("MoveAfter(4, back)", listString(l))
	l.MoveBefore(e1, l.Front())
	fmt.Println("MoveBefore(1, front)", listString(l))
	other := list.New()
	other.PushBack("x")
	other.PushBack("y")
	l.PushBackList(other)
	l.PushFrontList(other)
	fmt.Println("with other lists", listString(l), l.Len())
	for e := l.Front(); e != nil; {
		next := e.Next()
		if _, isStr := e.Value.(string); isStr {
			l.Remove(e)
		}
		e = next
	}
	fmt.Println("strings removed", listString(l), l.Len())
	fmt.Println("Remove returns", l.Remove(l.Front()), listString(l))
	// Removing an element of another list is a no-op.
	foreign := other.Front()
	l.Remove(foreign)
	fmt.Println("foreign remove no-op", listString(l), listString(other))

	// List-backed LRU of size 3.
	lru := list.New()
	idx := map[string]*list.Element{}
	access := func(k string) string {
		if e, ok := idx[k]; ok {
			lru.MoveToFront(e)
			return "hit"
		}
		idx[k] = lru.PushFront(k)
		if lru.Len() > 3 {
			old := lru.Back()
			lru.Remove(old)
			delete(idx, old.Value.(string))
			return "miss, evict " + old.Value.(string)
		}
		return "miss"
	}
	for _, k := range strings.Fields("a b c a d b e a c f") {
		res := access(k)
		fmt.Printf("lru access %s: %-12s %s\n", k, res, listString(lru))
	}

	// container/ring: Josephus problem.
	n, k := 13, 3
	r := ring.New(n)
	for i := 1; i <= n; i++ {
		r.Value = i
		r = r.Next()
	}
	fmt.Println("ring", ringString(r), "len", r.Len())
	r = r.Prev() // so that Move(k) lands on the k-th
	var order []int
	for r.Len() > 1 {
		r = r.Move(k - 1)
		removedR := r.Unlink(1)
		order = append(order, removedR.Value.(int))
	}
	fmt.Println("josephus order", order, "survivor", r.Value)

	// Link two rings, rotate, unlink.
	ra := ring.New(3)
	rb := ring.New(4)
	for i := 0; i < 3; i++ {
		ra.Value = "a" + fmt.Sprint(i)
		ra = ra.Next()
	}
	for i := 0; i < 4; i++ {
		rb.Value = "b" + fmt.Sprint(i)
		rb = rb.Next()
	}
	ra.Link(rb)
	fmt.Println("linked", ringString(ra), ra.Len())
	fmt.Println("Move(2)", ringString(ra.Move(2)), "Move(-1)", ringString(ra.Move(-1)))
	un := ra.Unlink(3)
	fmt.Println("after Unlink(3)", ringString(ra), "removed", ringString(un))
	sum := 0
	nr := ring.New(10)
	for i := 0; i < 10; i++ {
		nr.Value = i * i
		nr = nr.Next()
	}
	nr.Do(func(v any) { sum += v.(int) })
	fmt.Println("ring sum of squares", sum)
}
