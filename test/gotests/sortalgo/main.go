// Hand-written sorting and combinatorics, checked against sort.Ints: bubble,
// insertion, selection, shell, merge (top-down and bottom-up), quicksort
// (Lomuto and Hoare partitions, median of three), heapsort, counting and
// LSD radix sort, each over the same inputs of several shapes and sizes,
// printing a checksum and the first elements.  Then next-permutation
// enumeration, combinations by recursion, subsets by bitmask, the N-queens
// count for N up to 9, and a Sudoku solved by backtracking.  Tight loops of
// compare-and-swap through slices, recursion, and index arithmetic.
package main

import (
	"fmt"
	"os"
	"slices"
	"sort"
)

var seed uint64 = 88172645463325252

func init() {
	if len(os.Args) > 1000 {
		seed = 1
	}
}

func xorshift() uint64 {
	seed ^= seed << 13
	seed ^= seed >> 7
	seed ^= seed << 17
	return seed
}

func bubble(a []int) {
	for n := len(a); n > 1; n-- {
		swapped := false
		for i := 1; i < n; i++ {
			if a[i-1] > a[i] {
				a[i-1], a[i] = a[i], a[i-1]
				swapped = true
			}
		}
		if !swapped {
			return
		}
	}
}

func insertion(a []int) {
	for i := 1; i < len(a); i++ {
		v := a[i]
		j := i - 1
		for j >= 0 && a[j] > v {
			a[j+1] = a[j]
			j--
		}
		a[j+1] = v
	}
}

func selection(a []int) {
	for i := range a {
		m := i
		for j := i + 1; j < len(a); j++ {
			if a[j] < a[m] {
				m = j
			}
		}
		a[i], a[m] = a[m], a[i]
	}
}

func shell(a []int) {
	for gap := len(a) / 2; gap > 0; gap /= 2 {
		for i := gap; i < len(a); i++ {
			v := a[i]
			j := i
			for j >= gap && a[j-gap] > v {
				a[j] = a[j-gap]
				j -= gap
			}
			a[j] = v
		}
	}
}

func mergeTD(a []int) {
	if len(a) < 2 {
		return
	}
	mid := len(a) / 2
	mergeTD(a[:mid])
	mergeTD(a[mid:])
	tmp := make([]int, 0, len(a))
	i, j := 0, mid
	for i < mid && j < len(a) {
		if a[i] <= a[j] {
			tmp = append(tmp, a[i])
			i++
		} else {
			tmp = append(tmp, a[j])
			j++
		}
	}
	tmp = append(tmp, a[i:mid]...)
	tmp = append(tmp, a[j:]...)
	copy(a, tmp)
}

func mergeBU(a []int) {
	buf := make([]int, len(a))
	for w := 1; w < len(a); w *= 2 {
		for lo := 0; lo < len(a); lo += 2 * w {
			mid := min(lo+w, len(a))
			hi := min(lo+2*w, len(a))
			i, j, k := lo, mid, lo
			for i < mid && j < hi {
				if a[i] <= a[j] {
					buf[k] = a[i]
					i++
				} else {
					buf[k] = a[j]
					j++
				}
				k++
			}
			k += copy(buf[k:], a[i:mid])
			copy(buf[k:], a[j:hi])
		}
		a, buf = buf, a
	}
	// After an odd number of passes the result is in the other buffer.
	_ = a
}

func mergeBUInPlace(a []int) {
	orig := a
	buf := make([]int, len(a))
	src, dst := a, buf
	for w := 1; w < len(a); w *= 2 {
		for lo := 0; lo < len(a); lo += 2 * w {
			mid := min(lo+w, len(a))
			hi := min(lo+2*w, len(a))
			i, j, k := lo, mid, lo
			for i < mid && j < hi {
				if src[i] <= src[j] {
					dst[k] = src[i]
					i++
				} else {
					dst[k] = src[j]
					j++
				}
				k++
			}
			k += copy(dst[k:], src[i:mid])
			copy(dst[k:], src[j:hi])
		}
		src, dst = dst, src
	}
	copy(orig, src)
}

func quickLomuto(a []int) {
	if len(a) < 2 {
		return
	}
	p := a[len(a)-1]
	i := 0
	for j := 0; j < len(a)-1; j++ {
		if a[j] < p {
			a[i], a[j] = a[j], a[i]
			i++
		}
	}
	a[i], a[len(a)-1] = a[len(a)-1], a[i]
	quickLomuto(a[:i])
	quickLomuto(a[i+1:])
}

func quickHoare(a []int) {
	for len(a) > 16 {
		m := len(a) / 2
		if a[m] < a[0] {
			a[m], a[0] = a[0], a[m]
		}
		if a[len(a)-1] < a[0] {
			a[len(a)-1], a[0] = a[0], a[len(a)-1]
		}
		if a[len(a)-1] < a[m] {
			a[len(a)-1], a[m] = a[m], a[len(a)-1]
		}
		p := a[m]
		i, j := -1, len(a)
		for {
			for i++; a[i] < p; i++ {
			}
			for j--; a[j] > p; j-- {
			}
			if i >= j {
				break
			}
			a[i], a[j] = a[j], a[i]
		}
		// Recurse into the smaller half, loop on the larger.
		if j+1 < len(a)-j-1 {
			quickHoare(a[:j+1])
			a = a[j+1:]
		} else {
			quickHoare(a[j+1:])
			a = a[:j+1]
		}
	}
	insertion(a)
}

func heapsort(a []int) {
	sift := func(i, n int) {
		for {
			c := 2*i + 1
			if c >= n {
				return
			}
			if c+1 < n && a[c+1] > a[c] {
				c++
			}
			if a[i] >= a[c] {
				return
			}
			a[i], a[c] = a[c], a[i]
			i = c
		}
	}
	for i := len(a)/2 - 1; i >= 0; i-- {
		sift(i, len(a))
	}
	for n := len(a) - 1; n > 0; n-- {
		a[0], a[n] = a[n], a[0]
		sift(0, n)
	}
}

func counting(a []int, lo, hi int) {
	cnt := make([]int, hi-lo+1)
	for _, v := range a {
		cnt[v-lo]++
	}
	k := 0
	for i, c := range cnt {
		for ; c > 0; c-- {
			a[k] = i + lo
			k++
		}
	}
}

func radix(a []int) {
	// LSD radix on non-negative values, 8 bits at a time.
	buf := make([]int, len(a))
	for shift := uint(0); shift < 32; shift += 8 {
		var cnt [257]int
		for _, v := range a {
			cnt[(v>>shift)&0xff+1]++
		}
		for i := 1; i < 257; i++ {
			cnt[i] += cnt[i-1]
		}
		for _, v := range a {
			d := (v >> shift) & 0xff
			buf[cnt[d]] = v
			cnt[d]++
		}
		copy(a, buf)
	}
}

func sum(a []int) int {
	h := 0
	for i, v := range a {
		h = (h*1000003 + v*(i%7+1)) & 0x3fffffff
	}
	return h
}

func nextPerm(a []int) bool {
	i := len(a) - 2
	for i >= 0 && a[i] >= a[i+1] {
		i--
	}
	if i < 0 {
		return false
	}
	j := len(a) - 1
	for a[j] <= a[i] {
		j--
	}
	a[i], a[j] = a[j], a[i]
	for l, r := i+1, len(a)-1; l < r; l, r = l+1, r-1 {
		a[l], a[r] = a[r], a[l]
	}
	return true
}

func combos(n, k int) [][]int {
	var out [][]int
	var cur []int
	var rec func(start int)
	rec = func(start int) {
		if len(cur) == k {
			out = append(out, slices.Clone(cur))
			return
		}
		for i := start; i <= n-(k-len(cur)); i++ {
			cur = append(cur, i)
			rec(i + 1)
			cur = cur[:len(cur)-1]
		}
	}
	rec(0)
	return out
}

func queens(n int) int {
	count := 0
	var place func(row int, cols, d1, d2 uint32)
	place = func(row int, cols, d1, d2 uint32) {
		if row == n {
			count++
			return
		}
		free := ^(cols | d1 | d2) & (1<<n - 1)
		for free != 0 {
			bit := free & -free
			free ^= bit
			place(row+1, cols|bit, (d1|bit)<<1, (d2|bit)>>1)
		}
	}
	place(0, 0, 0, 0)
	return count
}

func sudoku(g *[81]int) (bool, int) {
	steps := 0
	var solve func() bool
	solve = func() bool {
		steps++
		pos := -1
		for i, v := range g {
			if v == 0 {
				pos = i
				break
			}
		}
		if pos < 0 {
			return true
		}
		r, c := pos/9, pos%9
		for d := 1; d <= 9; d++ {
			ok := true
			for k := 0; k < 9 && ok; k++ {
				br, bc := r/3*3+k/3, c/3*3+k%3
				if g[r*9+k] == d || g[k*9+c] == d || g[br*9+bc] == d {
					ok = false
				}
			}
			if ok {
				g[pos] = d
				if solve() {
					return true
				}
				g[pos] = 0
			}
		}
		return false
	}
	return solve(), steps
}

func main() {
	type algo struct {
		name string
		f    func([]int)
	}
	algos := []algo{
		{"bubble", bubble}, {"insertion", insertion}, {"selection", selection},
		{"shell", shell}, {"mergeTD", mergeTD}, {"mergeBU", mergeBUInPlace},
		{"lomuto", quickLomuto}, {"hoare", quickHoare}, {"heap", heapsort},
		{"counting", func(a []int) { counting(a, 0, 9999) }}, {"radix", radix},
	}
	shapes := []string{"random", "sorted", "reversed", "few", "organ"}
	for _, n := range []int{0, 1, 2, 7, 31, 200, 700} {
		for _, sh := range shapes {
			in := make([]int, n)
			for i := range in {
				switch sh {
				case "random":
					in[i] = int(xorshift() % 10000)
				case "sorted":
					in[i] = i * 3
				case "reversed":
					in[i] = (n - i) * 2
				case "few":
					in[i] = int(xorshift() % 4)
				case "organ":
					in[i] = min(i, n-i)
				}
			}
			want := slices.Clone(in)
			sort.Ints(want)
			line := fmt.Sprintf("%d %-8s %d:", n, sh, sum(want))
			for _, a := range algos {
				if a.name == "bubble" || a.name == "selection" {
					if n > 200 {
						continue
					}
				}
				got := slices.Clone(in)
				a.f(got)
				if !slices.Equal(got, want) {
					line += " " + a.name + "=WRONG"
				}
			}
			if n <= 7 {
				line += fmt.Sprint(" ", want)
			} else {
				line += fmt.Sprint(" ", want[:5], want[n-3:])
			}
			fmt.Println(line)
		}
	}
	unused := []int{3, 1, 2}
	mergeBU(unused)
	fmt.Println("-- permutations")
	p := []int{1, 2, 2, 3}
	count := 0
	for {
		count++
		fmt.Print(p, " ")
		if !nextPerm(p) {
			break
		}
	}
	fmt.Println(count)
	q := []int{0, 1, 2, 3, 4, 5}
	n := 1
	for nextPerm(q) {
		n++
		if n%120 == 0 {
			fmt.Print(q, " ")
		}
	}
	fmt.Println(n)
	fmt.Println("-- combinations and subsets")
	for _, c := range combos(5, 3) {
		fmt.Print(c, " ")
	}
	fmt.Println(len(combos(10, 4)), len(combos(12, 6)))
	set := []string{"a", "b", "c", "d"}
	for mask := 0; mask < 1<<len(set); mask++ {
		s := ""
		for i := range set {
			if mask&(1<<i) != 0 {
				s += set[i]
			}
		}
		fmt.Print("{", s, "} ")
	}
	fmt.Println()
	fmt.Println("-- queens")
	for k := 1; k <= 9; k++ {
		fmt.Print(queens(k), " ")
	}
	fmt.Println()
	fmt.Println("-- sudoku")
	puzzle := "530070000600195000098000060800060003400803001700020006060000280000419005000080079"
	var g [81]int
	for i, ch := range puzzle {
		g[i] = int(ch - '0')
	}
	ok, steps := sudoku(&g)
	fmt.Println(ok, steps)
	for r := 0; r < 9; r++ {
		fmt.Println(g[r*9 : r*9+9])
	}
}
