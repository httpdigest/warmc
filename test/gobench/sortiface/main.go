// sort.Sort and sort.Stable through sort.Interface, with Len, Less and Swap
// on named slice types, sort.Reverse wrapping sort.IntSlice, and
// sort.Search with a closure.  sortints covers sort.Slice, whose comparison
// is a closure; here each Less and Swap in pdqsort, insertionSort and
// symMerge is a call to an `$invoke` thunk TinyGo generates per interface
// method, which tests the type code against each concrete type that
// implements it (sort.Reverse's wrapper recursing into the thunk again) and
// links a frame onto the collector's shadow stack before calling the method.
// The closure given to sort.Search is inlined.
//
// Sorting records by several keys through sort.Interface is the classic Go
// shape, and every comparison pays for a dispatch thunk and a shadow-stack
// frame, which is mostly memory traffic around a small comparison.
package main

import "sort"

type record struct {
	dept  int32
	age   int32
	score int32
	id    int32
}

// byDeptScore orders by department, then by score descending, then by id.
type byDeptScore []record

func (s byDeptScore) Len() int      { return len(s) }
func (s byDeptScore) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s byDeptScore) Less(i, j int) bool {
	a, b := &s[i], &s[j]
	if a.dept != b.dept {
		return a.dept < b.dept
	}
	if a.score != b.score {
		return a.score > b.score
	}
	return a.id < b.id
}

// byAge is used for a stable sort, so ties keep their earlier order.
type byAge []record

func (s byAge) Len() int           { return len(s) }
func (s byAge) Swap(i, j int)      { s[i], s[j] = s[j], s[i] }
func (s byAge) Less(i, j int) bool { return s[i].age < s[j].age }

func main() {
	const n = 3000
	src := make([]record, n)
	rs := make([]record, n)
	x := uint32(99991)
	for i := range src {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		src[i] = record{int32(x % 13), int32(18 + (x>>8)%50), int32((x >> 16) % 1000), int32(i)}
	}
	ages := make([]int, n)
	total := 0
	for r := 0; r < 62; r++ {
		src[(r*37)%n].score = int32(r * 11)
		copy(rs, src)
		sort.Sort(byDeptScore(rs))
		for i := 0; i < n; i += 97 {
			total += int(rs[i].id) + int(rs[i].dept)*7
		}
		if !sort.IsSorted(byDeptScore(rs)) {
			total = -1
		}
		sort.Stable(byAge(rs))
		for i := 0; i < n; i += 89 {
			total += int(rs[i].id) ^ int(rs[i].age)
		}
		for i := range rs {
			ages[i] = int(rs[i].age)*10000 + int(rs[i].id)
		}
		// Binary search through a closure.
		for q := 0; q < 200; q++ {
			want := 180000 + q*1700
			j := sort.Search(n, func(i int) bool { return ages[i] >= want })
			total += j
		}
		sort.Sort(sort.Reverse(sort.IntSlice(ages)))
		total += ages[0] - ages[n-1] + ages[n/2]
	}
	println("total =", total)
}
