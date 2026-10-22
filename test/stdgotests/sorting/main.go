// Sorting and searching across sort, slices, maps and cmp: sort.Ints /
// Strings / Float64s (NaN ordering), sort.Sort over a custom Interface,
// sort.Stable vs sort.Slice on records with equal keys, sort.Search and
// SearchInts, sort.Reverse; slices.Sort, SortFunc with multi-key cmp.Or
// comparisons, SortStableFunc, BinarySearch and BinarySearchFunc, Index,
// Contains, Insert, Delete, DeleteFunc, Compact, CompactFunc, Reverse, Max,
// Min, Equal, Compare, Chunk, Repeat, Concat, Grow/Clip; maps.Keys/Values
// collected and sorted, Clone, Copy, Equal, EqualFunc, DeleteFunc; and
// cmp.Compare/Less over floats with NaN.
//
// pdqsort switches between insertion sort, heapsort and partitioning based on
// length and on how sorted the input looks, so inputs of many lengths and
// shapes (sorted, reversed, all equal, sawtooth, random) are sorted and
// verified.
package main

import (
	"cmp"
	"fmt"
	"maps"
	"math"
	"slices"
	"sort"
	"strings"
)

type person struct {
	Name string
	Age  int
	City string
}

type byAge []person

func (a byAge) Len() int           { return len(a) }
func (a byAge) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a byAge) Less(i, j int) bool { return a[i].Age < a[j].Age }

type lcg struct{ s uint64 }

func (l *lcg) next() int {
	l.s = l.s*6364136223846793005 + 1442695040888963407
	return int(l.s >> 33)
}

func checksum(xs []int) int {
	h := 0
	for i, x := range xs {
		h = (h*31 + x*(i+1)) % 1000000007
	}
	return h
}

func main() {
	// Shapes and lengths.
	rng := &lcg{42}
	shapes := map[string]func(n int) []int{
		"sorted": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = i
			}
			return s
		},
		"reversed": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = n - i
			}
			return s
		},
		"equal": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = 7
			}
			return s
		},
		"sawtooth": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = i % 17
			}
			return s
		},
		"random": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = rng.next() % 1000
			}
			return s
		},
		"organ": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = min(i, n-i)
			}
			return s
		},
	}
	names := slices.Sorted(maps.Keys(shapes))
	for _, name := range names {
		for _, n := range []int{0, 1, 2, 5, 12, 13, 50, 200, 1000} {
			a := shapes[name](n)
			b := slices.Clone(a)
			c := slices.Clone(a)
			sort.Ints(a)
			slices.Sort(b)
			sort.Slice(c, func(i, j int) bool { return c[i] > c[j] })
			ok := sort.IntsAreSorted(a) && slices.Equal(a, b) && slices.IsSortedFunc(c, func(x, y int) int { return cmp.Compare(y, x) })
			fmt.Printf("%-8s n=%4d ok=%v sum=%d\n", name, n, ok, checksum(a))
		}
	}

	// Strings and floats with NaN.
	strs := []string{"banana", "Apple", "cherry", "apple", "", "Banana", "äpfel", "zebra", "a"}
	sort.Strings(strs)
	fmt.Printf("strings %q\n", strs)
	slices.SortFunc(strs, func(a, b string) int {
		return cmp.Or(cmp.Compare(strings.ToLower(a), strings.ToLower(b)), cmp.Compare(a, b))
	})
	fmt.Printf("case-insensitive %q\n", strs)
	fl := []float64{3.5, math.NaN(), -1, math.Inf(1), 0, math.Inf(-1), math.NaN(), -0.5, 2}
	f2 := slices.Clone(fl)
	sort.Float64s(fl)
	slices.Sort(f2)
	fmt.Println("floats sort.Float64s", fl)
	fmt.Println("floats slices.Sort  ", f2)
	fmt.Println("cmp.Compare NaN", cmp.Compare(math.NaN(), 1.0), cmp.Compare(1.0, math.NaN()), cmp.Compare(math.NaN(), math.NaN()), cmp.Less(math.NaN(), math.Inf(-1)))
	fmt.Println("slices.Max/Min floats", slices.Max([]float64{1, 3, 2}), slices.Min([]float64{1, -3, 2}))

	// Stability.
	people := []person{
		{"Alice", 30, "Paris"}, {"Bob", 25, "Berlin"}, {"Carol", 30, "Austin"}, {"Dave", 25, "Paris"},
		{"Eve", 35, "Berlin"}, {"Frank", 30, "Berlin"}, {"Grace", 25, "Austin"}, {"Heidi", 35, "Paris"},
	}
	st := slices.Clone(people)
	sort.Stable(byAge(st))
	fmt.Println("sort.Stable by age:")
	for _, p := range st {
		fmt.Printf("  %-6s %d %s\n", p.Name, p.Age, p.City)
	}
	ss := slices.Clone(people)
	slices.SortStableFunc(ss, func(a, b person) int { return cmp.Compare(a.City, b.City) })
	fmt.Println("SortStableFunc by city:")
	for _, p := range ss {
		fmt.Printf("  %-6s %d %s\n", p.Name, p.Age, p.City)
	}
	multi := slices.Clone(people)
	slices.SortFunc(multi, func(a, b person) int {
		return cmp.Or(cmp.Compare(b.Age, a.Age), cmp.Compare(a.City, b.City), strings.Compare(a.Name, b.Name))
	})
	fmt.Println("multi-key (age desc, city, name):")
	for _, p := range multi {
		fmt.Printf("  %-6s %d %s\n", p.Name, p.Age, p.City)
	}
	rev := slices.Clone(people)
	sort.Sort(sort.Reverse(byAge(rev)))
	fmt.Println("reverse is sorted desc:", sort.IsSorted(sort.Reverse(byAge(rev))))

	// Searching.
	sorted := []int{1, 3, 3, 3, 5, 8, 13, 21, 34}
	for _, x := range []int{0, 1, 3, 4, 34, 35} {
		i := sort.SearchInts(sorted, x)
		j, found := slices.BinarySearch(sorted, x)
		k := sort.Search(len(sorted), func(i int) bool { return sorted[i] > x })
		fmt.Printf("search %2d: SearchInts=%d BinarySearch=%d,%v upper=%d\n", x, i, j, found, k)
	}
	pi, pf := slices.BinarySearchFunc(people[:0], 30, func(p person, t int) int { return cmp.Compare(p.Age, t) })
	fmt.Println("BinarySearchFunc empty", pi, pf)
	sortedPeople := slices.Clone(people)
	slices.SortFunc(sortedPeople, func(a, b person) int { return strings.Compare(a.Name, b.Name) })
	idx, found := slices.BinarySearchFunc(sortedPeople, "Eve", func(p person, n string) int { return strings.Compare(p.Name, n) })
	fmt.Println("BinarySearchFunc Eve", idx, found)
	fi, ff := sort.Find(len(sorted), func(i int) int { return cmp.Compare(13, sorted[i]) })
	fmt.Println("sort.Find 13", fi, ff)

	// slices editing.
	s := []int{10, 20, 30, 40, 50}
	fmt.Println("Index", slices.Index(s, 30), slices.Index(s, 99), "IndexFunc", slices.IndexFunc(s, func(x int) bool { return x > 25 }), "Contains", slices.Contains(s, 50), slices.ContainsFunc(s, func(x int) bool { return x < 0 }))
	s = slices.Insert(s, 2, 21, 22)
	fmt.Println("Insert", s)
	s = slices.Insert(s, len(s), 99)
	fmt.Println("Insert at end", s)
	s = slices.Delete(s, 1, 3)
	fmt.Println("Delete", s)
	s = slices.DeleteFunc(s, func(x int) bool { return x%20 == 0 })
	fmt.Println("DeleteFunc", s)
	s = slices.Replace(s, 0, 2, 7, 8, 9)
	fmt.Println("Replace", s)
	c := []int{1, 1, 2, 2, 2, 3, 1, 1, 4}
	fmt.Println("Compact", slices.Compact(slices.Clone(c)))
	fmt.Printf("CompactFunc %q\n", slices.CompactFunc([]string{"a", "A", "b", "B", "b", "c"}, strings.EqualFold))
	r := slices.Clone(c)
	slices.Reverse(r)
	fmt.Println("Reverse", r, "Max", slices.Max(c), "Min", slices.Min(c), "MaxFunc", slices.MaxFunc(people, func(a, b person) int { return cmp.Compare(a.Age, b.Age) }).Name)
	fmt.Println("Equal", slices.Equal([]int{1}, []int{1}), slices.Equal([]int{}, nil), "Compare", slices.Compare([]int{1, 2}, []int{1, 3}), slices.Compare([]int{1, 2, 3}, []int{1, 2}))
	fmt.Println("EqualFunc", slices.EqualFunc([]int{1, 2}, []string{"1", "2"}, func(a int, b string) bool { return fmt.Sprint(a) == b }))
	var chunks []string
	for ch := range slices.Chunk([]int{1, 2, 3, 4, 5, 6, 7}, 3) {
		chunks = append(chunks, fmt.Sprint(ch))
	}
	fmt.Println("Chunk", chunks)
	fmt.Println("Repeat", slices.Repeat([]string{"x", "y"}, 3), "Concat", slices.Concat([]int{1}, nil, []int{2, 3}, []int{}))
	g := slices.Grow([]int{1}, 10)
	fmt.Println("Grow len", len(g), "cap>=11", cap(g) >= 11, "Clip cap", cap(slices.Clip(g)))
	fmt.Println("Collect(Values)", slices.Collect(slices.Values([]int{3, 1, 2})), "Sorted(Values)", slices.Sorted(slices.Values([]int{3, 1, 2})))
	var backward []string
	for i, v := range slices.Backward([]string{"a", "b", "c"}) {
		backward = append(backward, fmt.Sprint(i, v))
	}
	fmt.Println("Backward", backward)
	fmt.Println("AppendSeq", slices.AppendSeq([]int{0}, slices.Values([]int{1, 2})))

	// maps.
	m := map[string]int{"one": 1, "two": 2, "three": 3, "four": 4, "five": 5}
	keys := slices.Collect(maps.Keys(m))
	slices.Sort(keys)
	vals := slices.Sorted(maps.Values(m))
	fmt.Println("keys", keys, "values", vals)
	m2 := maps.Clone(m)
	fmt.Println("Equal after Clone", maps.Equal(m, m2))
	m2["six"] = 6
	fmt.Println("Equal after add", maps.Equal(m, m2), len(m), len(m2))
	maps.DeleteFunc(m2, func(k string, v int) bool { return v%2 == 0 })
	fmt.Println("DeleteFunc evens", slices.Sorted(maps.Keys(m2)))
	dst := map[string]int{"one": 100, "zero": 0}
	maps.Copy(dst, m)
	fmt.Println("Copy", slices.Sorted(maps.Keys(dst)), dst["one"])
	fmt.Println("EqualFunc", maps.EqualFunc(map[int]string{1: "a"}, map[int][]byte{1: []byte("a")}, func(a string, b []byte) bool { return a == string(b) }))
	inverted := maps.Collect(func(yield func(int, string) bool) {
		for k, v := range m {
			if !yield(v, k) {
				return
			}
		}
	})
	for _, k := range slices.Sorted(maps.Keys(inverted)) {
		fmt.Printf("inverted %d=%s\n", k, inverted[k])
	}
	fmt.Println("Insert/All", len(maps.Collect(maps.All(m))))
	ins := map[int]int{}
	maps.Insert(ins, slices.All([]int{5, 6, 7}))
	fmt.Println("maps.Insert from slices.All", ins)

	// Large random sort cross-checked three ways.
	big := make([]int, 5000)
	for i := range big {
		big[i] = rng.next()%100000 - 50000
	}
	a1 := slices.Clone(big)
	a2 := slices.Clone(big)
	a3 := slices.Clone(big)
	sort.Ints(a1)
	slices.Sort(a2)
	sort.SliceStable(a3, func(i, j int) bool { return a3[i] < a3[j] })
	fmt.Println("big sorts agree", slices.Equal(a1, a2) && slices.Equal(a2, a3), "first", a1[:5], "last", a1[len(a1)-5:], "sum", checksum(a1))
	uniq := slices.Compact(a1)
	fmt.Println("unique", len(uniq))
}
