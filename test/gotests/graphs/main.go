// Graph algorithms.  Breadth-first search for shortest paths through a maze
// (with the path drawn back onto the grid), iterative and recursive
// depth-first search for connected components, Dijkstra with
// container/heap on a generated weighted graph, Bellman-Ford with a
// negative edge, Floyd-Warshall all-pairs distances, Kahn's topological sort
// with cycle detection, Kruskal's minimum spanning tree over union-find with
// path compression and union by rank, Tarjan's strongly connected
// components, and bipartite checking.  Adjacency lists in slices of slices,
// queues as slices, and heaps through the container/heap interface.
package main

import (
	"container/heap"
	"fmt"
	"os"
	"sort"
	"strings"
)

var salt = 0

func init() {
	if len(os.Args) > 1000 {
		salt = 1
	}
}

var maze = []string{
	"S.#.......#.........",
	".##.#####.#.#######.",
	"....#...#...#.....#.",
	"###.#.#.#####.###.#.",
	"#...#.#.......#.#.#.",
	"#.###.#########.#.#.",
	"#.#...#.........#...",
	"#.#.###.#########.##",
	"#...#.............#E",
}

type pt struct{ r, c int }

func bfs(grid []string) (int, []string) {
	h, w := len(grid), len(grid[0])
	var start, end pt
	for r := range grid {
		for c := range grid[r] {
			switch grid[r][c] {
			case 'S':
				start = pt{r, c}
			case 'E':
				end = pt{r, c}
			}
		}
	}
	dist := map[pt]int{start: 0}
	prev := map[pt]pt{}
	q := []pt{start}
	dirs := []pt{{0, 1}, {1, 0}, {0, -1}, {-1, 0}}
	for len(q) > 0 {
		cur := q[0]
		q = q[1:]
		if cur == end {
			break
		}
		for _, d := range dirs {
			n := pt{cur.r + d.r, cur.c + d.c}
			if n.r < 0 || n.r >= h || n.c < 0 || n.c >= w || grid[n.r][n.c] == '#' {
				continue
			}
			if _, seen := dist[n]; seen {
				continue
			}
			dist[n] = dist[cur] + 1
			prev[n] = cur
			q = append(q, n)
		}
	}
	d, ok := dist[end]
	if !ok {
		return -1, nil
	}
	rows := make([][]byte, h)
	for r := range grid {
		rows[r] = []byte(grid[r])
	}
	for p := prev[end]; p != start; p = prev[p] {
		rows[p.r][p.c] = '*'
	}
	out := make([]string, h)
	for r := range rows {
		out[r] = string(rows[r])
	}
	return d, out
}

type edge struct{ to, w int }

func genGraph(n int) [][]edge {
	g := make([][]edge, n)
	x := uint32(7 + salt)
	for i := 0; i < n; i++ {
		for k := 0; k < 3; k++ {
			x = x*1103515245 + 12345
			j := int(x>>16) % n
			if j != i {
				w := int(x>>8)%50 + 1
				g[i] = append(g[i], edge{j, w})
				g[j] = append(g[j], edge{i, w})
			}
		}
	}
	return g
}

type item struct{ node, dist int }
type pq []item

func (p pq) Len() int { return len(p) }
func (p pq) Less(i, j int) bool {
	if p[i].dist != p[j].dist {
		return p[i].dist < p[j].dist
	}
	return p[i].node < p[j].node
}
func (p pq) Swap(i, j int) { p[i], p[j] = p[j], p[i] }
func (p *pq) Push(x any)   { *p = append(*p, x.(item)) }
func (p *pq) Pop() any {
	old := *p
	it := old[len(old)-1]
	*p = old[:len(old)-1]
	return it
}

func dijkstra(g [][]edge, src int) ([]int, []int) {
	const inf = 1 << 30
	dist := make([]int, len(g))
	prev := make([]int, len(g))
	for i := range dist {
		dist[i], prev[i] = inf, -1
	}
	dist[src] = 0
	h := &pq{{src, 0}}
	pops := 0
	for h.Len() > 0 {
		it := heap.Pop(h).(item)
		pops++
		if it.dist > dist[it.node] {
			continue
		}
		for _, e := range g[it.node] {
			nd := it.dist + e.w
			if nd < dist[e.to] || (nd == dist[e.to] && it.node < prev[e.to]) {
				if nd < dist[e.to] {
					heap.Push(h, item{e.to, nd})
				}
				dist[e.to] = nd
				prev[e.to] = it.node
			}
		}
	}
	return dist, prev
}

func bellmanFord(n int, edges [][3]int, src int) ([]int, bool) {
	const inf = 1 << 30
	d := make([]int, n)
	for i := range d {
		d[i] = inf
	}
	d[src] = 0
	for i := 0; i < n-1; i++ {
		for _, e := range edges {
			if d[e[0]] < inf && d[e[0]]+e[2] < d[e[1]] {
				d[e[1]] = d[e[0]] + e[2]
			}
		}
	}
	for _, e := range edges {
		if d[e[0]] < inf && d[e[0]]+e[2] < d[e[1]] {
			return d, true
		}
	}
	return d, false
}

func floyd(n int, edges [][3]int) [][]int {
	const inf = 1 << 20
	d := make([][]int, n)
	for i := range d {
		d[i] = make([]int, n)
		for j := range d[i] {
			if i != j {
				d[i][j] = inf
			}
		}
	}
	for _, e := range edges {
		d[e[0]][e[1]] = min(d[e[0]][e[1]], e[2])
	}
	for k := 0; k < n; k++ {
		for i := 0; i < n; i++ {
			for j := 0; j < n; j++ {
				if d[i][k]+d[k][j] < d[i][j] {
					d[i][j] = d[i][k] + d[k][j]
				}
			}
		}
	}
	return d
}

func topo(n int, deps map[int][]int) ([]int, bool) {
	indeg := make([]int, n)
	for _, vs := range deps {
		for _, v := range vs {
			indeg[v]++
		}
	}
	var q, order []int
	for i := 0; i < n; i++ {
		if indeg[i] == 0 {
			q = append(q, i)
		}
	}
	for len(q) > 0 {
		sort.Ints(q) // smallest available first: a unique order
		u := q[0]
		q = q[1:]
		order = append(order, u)
		for _, v := range deps[u] {
			indeg[v]--
			if indeg[v] == 0 {
				q = append(q, v)
			}
		}
	}
	return order, len(order) == n
}

type uf struct{ parent, rank []int }

func newUF(n int) *uf {
	u := &uf{make([]int, n), make([]int, n)}
	for i := range u.parent {
		u.parent[i] = i
	}
	return u
}

func (u *uf) find(x int) int {
	for u.parent[x] != x {
		u.parent[x] = u.parent[u.parent[x]]
		x = u.parent[x]
	}
	return x
}

func (u *uf) union(a, b int) bool {
	ra, rb := u.find(a), u.find(b)
	if ra == rb {
		return false
	}
	if u.rank[ra] < u.rank[rb] {
		ra, rb = rb, ra
	}
	u.parent[rb] = ra
	if u.rank[ra] == u.rank[rb] {
		u.rank[ra]++
	}
	return true
}

func kruskal(n int, g [][]edge) (int, int) {
	var es [][3]int
	for i, adj := range g {
		for _, e := range adj {
			if i < e.to {
				es = append(es, [3]int{e.w, i, e.to})
			}
		}
	}
	sort.Slice(es, func(i, j int) bool {
		a, b := es[i], es[j]
		if a[0] != b[0] {
			return a[0] < b[0]
		}
		if a[1] != b[1] {
			return a[1] < b[1]
		}
		return a[2] < b[2]
	})
	u := newUF(n)
	total, used := 0, 0
	for _, e := range es {
		if u.union(e[1], e[2]) {
			total += e[0]
			used++
		}
	}
	return total, used
}

func components(g [][]edge) []int {
	comp := make([]int, len(g))
	for i := range comp {
		comp[i] = -1
	}
	c := 0
	for s := range g {
		if comp[s] >= 0 {
			continue
		}
		stack := []int{s}
		comp[s] = c
		for len(stack) > 0 {
			u := stack[len(stack)-1]
			stack = stack[:len(stack)-1]
			for _, e := range g[u] {
				if comp[e.to] < 0 {
					comp[e.to] = c
					stack = append(stack, e.to)
				}
			}
		}
		c++
	}
	return comp
}

func tarjan(n int, adj [][]int) [][]int {
	index := make([]int, n)
	low := make([]int, n)
	on := make([]bool, n)
	for i := range index {
		index[i] = -1
	}
	var stack []int
	var sccs [][]int
	next := 0
	var visit func(v int)
	visit = func(v int) {
		index[v], low[v] = next, next
		next++
		stack = append(stack, v)
		on[v] = true
		for _, w := range adj[v] {
			if index[w] < 0 {
				visit(w)
				low[v] = min(low[v], low[w])
			} else if on[w] {
				low[v] = min(low[v], index[w])
			}
		}
		if low[v] == index[v] {
			var scc []int
			for {
				w := stack[len(stack)-1]
				stack = stack[:len(stack)-1]
				on[w] = false
				scc = append(scc, w)
				if w == v {
					break
				}
			}
			sort.Ints(scc)
			sccs = append(sccs, scc)
		}
	}
	for v := 0; v < n; v++ {
		if index[v] < 0 {
			visit(v)
		}
	}
	return sccs
}

func bipartite(n int, adj [][]int) bool {
	color := make([]int, n)
	for s := 0; s < n; s++ {
		if color[s] != 0 {
			continue
		}
		color[s] = 1
		q := []int{s}
		for len(q) > 0 {
			u := q[0]
			q = q[1:]
			for _, v := range adj[u] {
				if color[v] == 0 {
					color[v] = -color[u]
					q = append(q, v)
				} else if color[v] == color[u] {
					return false
				}
			}
		}
	}
	return true
}

func main() {
	fmt.Println("-- BFS maze")
	d, path := bfs(maze)
	fmt.Println("distance", d)
	fmt.Println(strings.Join(path, "\n"))
	blocked := append([]string{}, maze...)
	blocked[7] = "#.#.###.#########.#E"
	blocked[8] = "#...#.............##"
	d2, _ := bfs(blocked)
	fmt.Println("blocked distance", d2)
	fmt.Println("-- Dijkstra")
	g := genGraph(60)
	dist, prev := dijkstra(g, 0)
	fmt.Println(dist)
	for _, t := range []int{17, 33, 59} {
		var p []int
		for v := t; v != -1; v = prev[v] {
			p = append(p, v)
		}
		fmt.Println(t, dist[t], p)
	}
	fmt.Println("-- components")
	sparse := make([][]edge, 30)
	for i := 0; i < 30; i++ {
		if i%5 != 4 {
			sparse[i] = append(sparse[i], edge{i + 1, 1})
			sparse[i+1] = append(sparse[i+1], edge{i, 1})
		}
	}
	fmt.Println(components(sparse))
	fmt.Println(components(g)[:10])
	fmt.Println("-- Bellman-Ford and Floyd-Warshall")
	edges := [][3]int{{0, 1, 4}, {0, 2, 5}, {1, 2, -3}, {2, 3, 4}, {3, 1, 2}, {1, 4, 6}, {4, 5, -1}, {3, 5, 3}}
	bd, neg := bellmanFord(6, edges, 0)
	fmt.Println(bd, neg)
	_, neg2 := bellmanFord(6, append(edges, [3]int{3, 2, -5}), 0)
	fmt.Println(neg2)
	for _, row := range floyd(6, edges) {
		fmt.Println(row)
	}
	fmt.Println("-- topological sort")
	deps := map[int][]int{0: {2, 3}, 1: {3, 4}, 2: {5}, 3: {5, 6}, 4: {6}, 5: {7}, 6: {7}}
	order, ok := topo(8, deps)
	fmt.Println(order, ok)
	deps[7] = []int{1}
	order, ok = topo(8, deps)
	fmt.Println(order, ok)
	fmt.Println("-- Kruskal")
	total, used := kruskal(60, g)
	fmt.Println(total, used)
	fmt.Println("-- Tarjan SCC")
	adj := [][]int{{1}, {2}, {0, 3}, {4}, {5, 7}, {6}, {4}, {8}, {7}, {9}, {}}
	fmt.Println(tarjan(len(adj), adj))
	fmt.Println("-- bipartite")
	even := [][]int{{1, 3}, {0, 2}, {1, 3}, {2, 0}}
	odd := [][]int{{1, 2}, {0, 2}, {0, 1}}
	fmt.Println(bipartite(4, even), bipartite(3, odd))
}
