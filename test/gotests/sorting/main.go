// The sort and slices packages.  sort.Ints/Strings/Float64s (NaN sorts
// first), sort.Sort and sort.Stable over a custom sort.Interface with
// sort.Reverse, sort.Slice with a total order and sort.SliceStable with ties
// (stability is observable), sort.Search and the Search* helpers; slices.Sort,
// SortFunc with multi-key cmp.Compare/cmp.Or comparators, SortStableFunc,
// BinarySearch(Func), Index/Contains/Equal/Compare, Reverse, Min/Max,
// Compact(Func), Insert/Delete/Replace and Chunk.  pdqsort's insertion-sort,
// heapsort and partition paths are reached by sizing the inputs around its
// thresholds, with data from a fixed linear congruential sequence.
package main

import (
	"cmp"
	"fmt"
	"math"
	"os"
	"slices"
	"sort"
	"strings"
)

var seed uint32 = 12345

func init() {
	if len(os.Args) > 1000 {
		seed = 1
	}
}

func lcg() uint32 {
	seed = seed*1664525 + 1013904223
	return seed >> 8
}

type person struct {
	name string
	age  int
	city string
}

type byAge []person

func (p byAge) Len() int           { return len(p) }
func (p byAge) Less(i, j int) bool { return p[i].age < p[j].age }
func (p byAge) Swap(i, j int)      { p[i], p[j] = p[j], p[i] }

func checksum(xs []int) int {
	h := 0
	for i, x := range xs {
		h = h*31 + x*(i+1)
		h &= 0xffffff
	}
	return h
}

func main() {
	fmt.Println("-- basic")
	ints := []int{5, -2, 9, 0, 3, 3, -7, 12, 1}
	sort.Ints(ints)
	fmt.Println(ints, sort.IntsAreSorted(ints), sort.SearchInts(ints, 3), sort.SearchInts(ints, 4))
	strs := strings.Fields("pear Apple fig banana apple Fig cherry 10 9 _x")
	sort.Strings(strs)
	fmt.Println(strs, sort.SearchStrings(strs, "fig"))
	fl := []float64{2.5, math.NaN(), -1, math.Inf(1), 0, math.Inf(-1), -0.5, math.NaN(), 1e-9}
	sort.Float64s(fl)
	fmt.Println(fl, sort.Float64sAreSorted(fl))
	fmt.Println("-- custom interface")
	people := []person{
		{"alice", 30, "paris"}, {"bob", 25, "rome"}, {"carol", 30, "oslo"},
		{"dave", 25, "paris"}, {"eve", 35, "rome"}, {"frank", 30, "rome"},
		{"grace", 25, "oslo"}, {"heidi", 40, "paris"},
	}
	p1 := slices.Clone(people)
	sort.Stable(byAge(p1))
	fmt.Println(p1)
	p2 := slices.Clone(people)
	sort.Stable(sort.Reverse(byAge(p2)))
	fmt.Println(p2)
	p3 := slices.Clone(people)
	sort.SliceStable(p3, func(i, j int) bool { return p3[i].city < p3[j].city })
	fmt.Println(p3)
	p4 := slices.Clone(people)
	sort.Slice(p4, func(i, j int) bool {
		if p4[i].age != p4[j].age {
			return p4[i].age > p4[j].age
		}
		return p4[i].name < p4[j].name
	})
	fmt.Println(p4)
	p5 := slices.Clone(people)
	slices.SortFunc(p5, func(a, b person) int {
		return cmp.Or(cmp.Compare(a.city, b.city), -cmp.Compare(a.age, b.age),
			strings.Compare(a.name, b.name))
	})
	fmt.Println(p5)
	p6 := slices.Clone(people)
	slices.SortStableFunc(p6, func(a, b person) int { return cmp.Compare(len(a.name), len(b.name)) })
	fmt.Println(p6)
	fmt.Println("-- search")
	sorted := []int{1, 3, 3, 3, 7, 9, 11, 20}
	for _, t := range []int{0, 1, 3, 4, 20, 21} {
		i := sort.Search(len(sorted), func(i int) bool { return sorted[i] >= t })
		j, found := slices.BinarySearch(sorted, t)
		k, found2 := sort.Find(len(sorted), func(i int) int { return cmp.Compare(t, sorted[i]) })
		fmt.Println(t, i, j, found, k, found2)
	}
	idx, ok := slices.BinarySearchFunc(p4, 30, func(p person, age int) int { return cmp.Compare(age, p.age) })
	fmt.Println(idx, ok)
	fmt.Println("-- slices helpers")
	xs := []int{4, 1, 4, 4, 2, 2, 9, 1, 1, 1, 7}
	fmt.Println(slices.Index(xs, 2), slices.Index(xs, 5), slices.Contains(xs, 9),
		slices.IndexFunc(xs, func(x int) bool { return x > 4 }), slices.Min(xs), slices.Max(xs))
	c := slices.Clone(xs)
	c = slices.Compact(c)
	fmt.Println(c, len(c))
	s := slices.Clone(xs)
	slices.Sort(s)
	fmt.Println(s, slices.Compact(slices.Clone(s)), slices.IsSorted(s))
	words := strings.Fields("a A b B b c C C d")
	words = slices.CompactFunc(words, strings.EqualFold)
	fmt.Println(words)
	r := slices.Clone(xs)
	slices.Reverse(r)
	fmt.Println(r, slices.Equal(r, xs), slices.Compare([]int{1, 2}, []int{1, 3}),
		slices.Compare([]int{1, 2, 0}, []int{1, 2}))
	r = slices.Replace(r, 1, 3, 100, 200, 300)
	fmt.Println(r)
	for ch := range slices.Chunk(xs, 4) {
		fmt.Print(ch, " ")
	}
	fmt.Println()
	fmt.Println(slices.MinFunc(people, func(a, b person) int { return cmp.Compare(a.name, b.name) }),
		slices.MaxFunc(people, func(a, b person) int { return cmp.Compare(a.age, b.age) }))
	fmt.Println("-- sizes around the algorithm's thresholds")
	for _, n := range []int{0, 1, 2, 5, 11, 12, 13, 33, 50, 51, 128, 1000, 5000} {
		a := make([]int, n)
		for i := range a {
			a[i] = int(lcg() % 1000)
		}
		b := slices.Clone(a)
		c := slices.Clone(a)
		d := slices.Clone(a)
		sort.Ints(a)
		slices.Sort(b)
		sort.Sort(sort.Reverse(sort.IntSlice(c)))
		slices.SortFunc(d, func(x, y int) int { return cmp.Compare(x%10, y%10)*1000 + cmp.Compare(x, y) })
		fmt.Println(n, slices.IsSorted(a), slices.Equal(a, b), checksum(a), checksum(c), checksum(d))
		if n > 0 && n <= 13 {
			fmt.Println("  ", a, c)
		}
	}
	fmt.Println("-- patterns")
	for _, pat := range []string{"ascending", "descending", "equal", "sawtooth", "organ"} {
		a := make([]int, 300)
		for i := range a {
			switch pat {
			case "ascending":
				a[i] = i
			case "descending":
				a[i] = 300 - i
			case "equal":
				a[i] = 7
			case "sawtooth":
				a[i] = i % 17
			case "organ":
				a[i] = min(i, 300-i)
			}
		}
		st := make([]struct{ k, pos int }, len(a))
		for i, v := range a {
			st[i] = struct{ k, pos int }{v % 5, i}
		}
		sort.SliceStable(st, func(i, j int) bool { return st[i].k < st[j].k })
		stable := true
		for i := 1; i < len(st); i++ {
			if st[i].k == st[i-1].k && st[i].pos < st[i-1].pos {
				stable = false
			}
		}
		sort.Ints(a)
		fmt.Println(pat, checksum(a), stable, st[0], st[299])
	}
}
