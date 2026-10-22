// The slices and maps packages: slices.SortFunc and BinarySearchFunc with a
// three-key comparator, slices.Compact and Index, and a map's keys collected in
// order with slices.Sorted(maps.Keys(m)).
//
// The generic counterpart of sortints.  sort.Slice there swaps through a
// reflection-built swapper; slices.SortFunc is stenciled for the element's GC
// shape, so its pdqsort moves the 32-byte entry structs directly -- through a
// write barrier, since one field is a string -- and only the comparator is a
// function value, called by call_indirect.  slices.Sorted over strings is
// stenciled for the ordered string shape and compares with a direct call to
// runtime.cmpstring.  maps.Keys returns an iter.Seq, so collecting it is a
// range-over-func loop driving the runtime's map iterator, which starts at a
// random slot; sorting the keys is what makes the checksum fixed.
package main

import (
	"cmp"
	"fmt"
	"maps"
	"slices"
	"strings"
)

type entry struct {
	key  string
	rank int32
	val  uint64
}

func cmpEntry(a, b entry) int {
	if c := cmp.Compare(a.rank, b.rank); c != 0 {
		return c
	}
	if c := strings.Compare(a.key, b.key); c != 0 {
		return c
	}
	return cmp.Compare(a.val, b.val)
}

type probe struct {
	rank int32
	key  string
}

// Orders an entry against a (rank, key) target, ignoring val, so a search
// lands on the first of a run of equal keys.
func cmpProbe(e entry, p probe) int {
	if c := cmp.Compare(e.rank, p.rank); c != 0 {
		return c
	}
	return strings.Compare(e.key, p.key)
}

func main() {
	const n = 1024
	es := make([]entry, n)
	m := make(map[string]uint64, n)
	keys := make([]string, n)
	for i := range keys {
		keys[i] = fmt.Sprintf("k%05d", (i*7919)%10007)
	}
	var acc uint64
	x := uint64(88172645463325252)
	for r := 0; r < 260; r++ {
		clear(m)
		for i := range es {
			x ^= x << 13
			x ^= x >> 7
			x ^= x << 17
			es[i] = entry{keys[(x>>8)%n], int32(x>>40) & 255, x}
			m[es[i].key] += x >> 32
		}
		slices.SortFunc(es, cmpEntry)
		// Probe for entries by (rank, key), present or not.
		for i := 0; i < n; i += 3 {
			p := probe{int32(i & 255), keys[i]}
			if i&1 == 0 {
				e := es[(i*37)%n]
				p = probe{e.rank, e.key}
			}
			if j, ok := slices.BinarySearchFunc(es, p, cmpProbe); ok {
				acc += es[j].val >> 48
			} else {
				acc += uint64(j)
			}
		}
		ranks := make([]int32, n)
		for i := range es {
			ranks[i] = es[i].rank
		}
		ranks = slices.Compact(ranks)
		acc = acc*31 + uint64(len(ranks))
		ks := slices.Sorted(maps.Keys(m))
		for i, k := range ks {
			acc += m[k] ^ uint64(i)
		}
		acc += uint64(slices.Index(ks, keys[r]) + 1)
	}
	fmt.Println("acc =", acc)
}
