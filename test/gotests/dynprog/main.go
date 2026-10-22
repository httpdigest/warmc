// Dynamic programming.  Longest common subsequence with the table walked
// back to recover the subsequence, edit distance with the alignment, 0/1
// knapsack with the chosen items, coin change (number of ways and fewest
// coins), longest increasing subsequence in O(n log n) with reconstruction,
// matrix-chain multiplication with the parenthesisation, the number of
// paths through a grid with obstacles, partition into equal subsets with a
// bitset, and the Catalan and Bell numbers.  Two-dimensional tables in flat
// and nested slices: dense, data-dependent indexing and min/max chains.
package main

import (
	"fmt"
	"os"
	"strings"
)

var offset = 0

func init() {
	if len(os.Args) > 1000 {
		offset = 1
	}
}

func lcs(a, b string) (int, string) {
	n, m := len(a), len(b)
	t := make([][]int, n+1)
	for i := range t {
		t[i] = make([]int, m+1)
	}
	for i := 1; i <= n; i++ {
		for j := 1; j <= m; j++ {
			if a[i-1] == b[j-1] {
				t[i][j] = t[i-1][j-1] + 1
			} else {
				t[i][j] = max(t[i-1][j], t[i][j-1])
			}
		}
	}
	var out []byte
	for i, j := n, m; i > 0 && j > 0; {
		switch {
		case a[i-1] == b[j-1]:
			out = append(out, a[i-1])
			i--
			j--
		case t[i-1][j] >= t[i][j-1]:
			i--
		default:
			j--
		}
	}
	for i, j := 0, len(out)-1; i < j; i, j = i+1, j-1 {
		out[i], out[j] = out[j], out[i]
	}
	return t[n][m], string(out)
}

func editDistance(a, b string) (int, string) {
	n, m := len(a), len(b)
	w := m + 1
	d := make([]int, (n+1)*w) // flat table
	for i := 0; i <= n; i++ {
		d[i*w] = i
	}
	for j := 0; j <= m; j++ {
		d[j] = j
	}
	for i := 1; i <= n; i++ {
		for j := 1; j <= m; j++ {
			cost := 1
			if a[i-1] == b[j-1] {
				cost = 0
			}
			d[i*w+j] = min(d[(i-1)*w+j]+1, d[i*w+j-1]+1, d[(i-1)*w+j-1]+cost)
		}
	}
	var ops []byte
	i, j := n, m
	for i > 0 || j > 0 {
		switch {
		case i > 0 && j > 0 && a[i-1] == b[j-1] && d[i*w+j] == d[(i-1)*w+j-1]:
			ops = append(ops, '=')
			i, j = i-1, j-1
		case i > 0 && j > 0 && d[i*w+j] == d[(i-1)*w+j-1]+1:
			ops = append(ops, '~')
			i, j = i-1, j-1
		case i > 0 && d[i*w+j] == d[(i-1)*w+j]+1:
			ops = append(ops, '-')
			i--
		default:
			ops = append(ops, '+')
			j--
		}
	}
	for x, y := 0, len(ops)-1; x < y; x, y = x+1, y-1 {
		ops[x], ops[y] = ops[y], ops[x]
	}
	return d[n*w+m], string(ops)
}

func knapsack(weights, values []int, cap int) (int, []int) {
	n := len(weights)
	t := make([][]int, n+1)
	for i := range t {
		t[i] = make([]int, cap+1)
	}
	for i := 1; i <= n; i++ {
		for c := 0; c <= cap; c++ {
			t[i][c] = t[i-1][c]
			if weights[i-1] <= c {
				t[i][c] = max(t[i][c], t[i-1][c-weights[i-1]]+values[i-1])
			}
		}
	}
	var items []int
	for i, c := n, cap; i > 0; i-- {
		if t[i][c] != t[i-1][c] {
			items = append(items, i-1)
			c -= weights[i-1]
		}
	}
	return t[n][cap], items
}

func coinWays(coins []int, amount int) int64 {
	ways := make([]int64, amount+1)
	ways[0] = 1
	for _, c := range coins {
		for v := c; v <= amount; v++ {
			ways[v] += ways[v-c]
		}
	}
	return ways[amount]
}

func fewestCoins(coins []int, amount int) (int, []int) {
	const inf = 1 << 30
	best := make([]int, amount+1)
	last := make([]int, amount+1)
	for v := 1; v <= amount; v++ {
		best[v] = inf
		for _, c := range coins {
			if c <= v && best[v-c]+1 < best[v] {
				best[v] = best[v-c] + 1
				last[v] = c
			}
		}
	}
	if best[amount] >= inf {
		return -1, nil
	}
	var used []int
	for v := amount; v > 0; v -= last[v] {
		used = append(used, last[v])
	}
	return best[amount], used
}

func lis(xs []int) []int {
	tails := []int{} // indexes
	prev := make([]int, len(xs))
	for i, x := range xs {
		lo, hi := 0, len(tails)
		for lo < hi {
			mid := (lo + hi) / 2
			if xs[tails[mid]] < x {
				lo = mid + 1
			} else {
				hi = mid
			}
		}
		if lo > 0 {
			prev[i] = tails[lo-1]
		} else {
			prev[i] = -1
		}
		if lo == len(tails) {
			tails = append(tails, i)
		} else {
			tails[lo] = i
		}
	}
	out := make([]int, len(tails))
	k := tails[len(tails)-1]
	for i := len(out) - 1; i >= 0; i-- {
		out[i] = xs[k]
		k = prev[k]
	}
	return out
}

func matrixChain(dims []int) (int, string) {
	n := len(dims) - 1
	cost := make([][]int, n)
	split := make([][]int, n)
	for i := range cost {
		cost[i] = make([]int, n)
		split[i] = make([]int, n)
	}
	for length := 2; length <= n; length++ {
		for i := 0; i+length-1 < n; i++ {
			j := i + length - 1
			cost[i][j] = int(^uint(0) >> 1)
			for k := i; k < j; k++ {
				c := cost[i][k] + cost[k+1][j] + dims[i]*dims[k+1]*dims[j+1]
				if c < cost[i][j] {
					cost[i][j] = c
					split[i][j] = k
				}
			}
		}
	}
	var paren func(i, j int) string
	paren = func(i, j int) string {
		if i == j {
			return string(rune('A' + i))
		}
		return "(" + paren(i, split[i][j]) + paren(split[i][j]+1, j) + ")"
	}
	return cost[0][n-1], paren(0, n-1)
}

func gridPaths(rows []string) int64 {
	w := len(rows[0])
	p := make([]int64, w)
	p[0] = 1
	for _, r := range rows {
		for j := 0; j < w; j++ {
			if r[j] == '#' {
				p[j] = 0
			} else if j > 0 {
				p[j] += p[j-1]
			}
		}
	}
	return p[w-1]
}

func canPartition(xs []int) (bool, int) {
	total := 0
	for _, x := range xs {
		total += x
	}
	if total%2 == 1 {
		return false, total
	}
	var bits [64]uint64 // sums up to 4095
	bits[0] = 1
	for _, x := range xs {
		// shift the whole bitset left by x, OR into itself, high words first
		ws, bs := x/64, uint(x%64)
		for i := len(bits) - 1; i >= 0; i-- {
			var v uint64
			if i-ws >= 0 {
				v = bits[i-ws] << bs
				if bs != 0 && i-ws-1 >= 0 {
					v |= bits[i-ws-1] >> (64 - bs)
				}
			}
			bits[i] |= v
		}
	}
	half := total / 2
	return bits[half/64]>>(half%64)&1 == 1, total
}

func catalanBell(n int) ([]int64, []int64) {
	cat := make([]int64, n)
	cat[0] = 1
	for i := 1; i < n; i++ {
		for j := 0; j < i; j++ {
			cat[i] += cat[j] * cat[i-1-j]
		}
	}
	bell := make([]int64, n)
	row := []int64{1}
	bell[0] = 1
	for i := 1; i < n; i++ {
		next := []int64{row[len(row)-1]}
		for _, v := range row {
			next = append(next, next[len(next)-1]+v)
		}
		row = next
		bell[i] = row[0]
	}
	return cat, bell
}

func main() {
	fmt.Println("-- LCS")
	pairs := [][2]string{{"ABCBDAB", "BDCABA"}, {"AGGTAB", "GXTXAYB"}, {"", "abc"},
		{"kitten", "sitting"}, {"the quick brown fox", "a quick brown dog jumps"},
		{strings.Repeat("ab", 30), strings.Repeat("ba", 25)}}
	for _, p := range pairs {
		n, s := lcs(p[0], p[1])
		d, ops := editDistance(p[0], p[1])
		fmt.Printf("%q %q lcs=%d %q edit=%d %s\n", p[0], p[1], n, s, d, ops)
	}
	fmt.Println("-- knapsack")
	w := []int{12, 7, 11, 8, 9, 6, 15, 3, 5, 14}
	v := []int{24, 13, 23, 15, 16, 11, 31, 4, 9, 25}
	for _, c := range []int{0, 10, 26, 50, 75, 100} {
		best, items := knapsack(w, v, c+offset)
		fmt.Println(c, best, items)
	}
	fmt.Println("-- coins")
	coins := []int{1, 2, 5, 10, 20, 50, 100, 200}
	for _, amt := range []int{0, 1, 7, 100, 200, 1000} {
		fmt.Print(amt, ":", coinWays(coins, amt+offset), " ")
	}
	fmt.Println()
	odd := []int{7, 13, 29}
	for _, amt := range []int{1, 20, 41, 99, 100, 1000} {
		n, used := fewestCoins(odd, amt)
		fmt.Println(amt, n, used)
	}
	fmt.Println("-- LIS")
	seqs := [][]int{{10, 9, 2, 5, 3, 7, 101, 18}, {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15},
		{5, 4, 3, 2, 1}, {1, 2, 3}}
	gen := make([]int, 200)
	for i := range gen {
		gen[i] = (i*7919 + 13) % 211
	}
	seqs = append(seqs, gen)
	for _, s := range seqs {
		r := lis(s)
		fmt.Println(len(r), r)
	}
	fmt.Println("-- matrix chain")
	for _, dims := range [][]int{{10, 30, 5, 60}, {40, 20, 30, 10, 30}, {30, 35, 15, 5, 10, 20, 25},
		{5, 10, 3, 12, 5, 50, 6, 8, 2, 40}} {
		c, p := matrixChain(dims)
		fmt.Println(c, p)
	}
	fmt.Println("-- grid paths")
	grids := [][]string{
		{"....", "....", "...."},
		{"..#.", "....", ".#..", "...."},
		{strings.Repeat(".", 16), strings.Repeat(".", 16), strings.Repeat(".", 16),
			strings.Repeat(".", 16), strings.Repeat(".", 16), strings.Repeat(".", 16),
			strings.Repeat(".", 16), strings.Repeat(".", 16), strings.Repeat(".", 16),
			strings.Repeat(".", 16), strings.Repeat(".", 16), strings.Repeat(".", 16)},
		{".#", "#."},
	}
	for _, g := range grids {
		fmt.Print(gridPaths(g), " ")
	}
	fmt.Println()
	fmt.Println("-- partition")
	for _, xs := range [][]int{{1, 5, 11, 5}, {1, 2, 3, 5}, {3, 1, 1, 2, 2, 1},
		{100, 200, 300, 400, 500, 600, 700, 63, 1}, {97, 89, 83, 79, 73, 71, 67, 61, 59, 53}} {
		ok, total := canPartition(xs)
		fmt.Println(xs, ok, total)
	}
	fmt.Println("-- catalan and bell")
	cat, bell := catalanBell(25)
	fmt.Println(cat)
	fmt.Println(bell)
}
