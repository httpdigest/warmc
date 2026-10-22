// container/heap as a priority queue: Dijkstra's shortest paths from 32
// sources over a generated 4,000-node graph, a ring plus six random chords per
// node.
//
// container/heap is written against heap.Interface, so every step of its up
// and down sifts is a Less and a Swap through an itab -- call_indirect into the
// pointer-receiver wrappers the compiler generates for pq's value methods.
// Push and Pop pass the element as `any`: each push boxes a 16-byte item with
// runtime.convTnoptr, an allocation, and each pop type-asserts it back.  The
// relaxation loop over adjacency slices is plain loads and compares.  The
// standard library's priority queue, and the interface-heavy opposite of a
// hand-written heap.
package main

import (
	"container/heap"
	"fmt"
)

type edge struct {
	to int32
	w  uint32
}

type item struct {
	node int32
	dist uint64
}

type pq []item

func (q pq) Len() int { return len(q) }
func (q pq) Less(i, j int) bool {
	if q[i].dist != q[j].dist {
		return q[i].dist < q[j].dist
	}
	return q[i].node < q[j].node
}
func (q pq) Swap(i, j int) { q[i], q[j] = q[j], q[i] }
func (q *pq) Push(x any)   { *q = append(*q, x.(item)) }
func (q *pq) Pop() any {
	old := *q
	n := len(old)
	it := old[n-1]
	*q = old[:n-1]
	return it
}

func main() {
	const n = 4000
	const deg = 6
	adj := make([][]edge, n)
	x := uint32(7)
	for u := 0; u < n; u++ {
		// A ring, so everything is reachable, plus random chords.
		adj[u] = append(adj[u], edge{int32((u + 1) % n), 1000})
		for k := 0; k < deg; k++ {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			adj[u] = append(adj[u], edge{int32(x % n), x>>20&1023 + 1})
		}
	}
	dist := make([]uint64, n)
	var acc uint64
	for src := 0; src < 32; src++ {
		for i := range dist {
			dist[i] = ^uint64(0)
		}
		s := (src * 977) % n
		dist[s] = 0
		q := &pq{{int32(s), 0}}
		for q.Len() > 0 {
			it := heap.Pop(q).(item)
			if it.dist > dist[it.node] {
				continue
			}
			for _, e := range adj[it.node] {
				if d := it.dist + uint64(e.w); d < dist[e.to] {
					dist[e.to] = d
					heap.Push(q, item{e.to, d})
				}
			}
		}
		for i, d := range dist {
			acc += d * uint64(i&15+1)
		}
	}
	fmt.Println("acc =", acc)
}
