// Map semantics.  Insert, overwrite, lookup with comma-ok, delete (present
// and absent), len, reads and deletes on a nil map, and clear.  Keys of
// every comparable kind: ints, strings, floats (+0 and -0 are one key, each
// NaN is a key of its own that no lookup finds), structs, arrays, pointers
// and interfaces holding different dynamic types.  Values that are structs
// (copied out, modified, stored back), slices and maps.  Deleting during a
// range is allowed, and the survivors are printed in sorted order, as is
// every listing here: iteration order is randomised and only the contents
// are defined.  A map grown to thousands of entries and shrunk again checks
// rehashing.
package main

import (
	"fmt"
	"maps"
	"math"
	"os"
	"slices"
	"sort"
	"strings"
)

type point struct {
	x, y int
}

type key struct {
	name string
	id   int
	pt   point
}

var words = strings.Fields(`the quick brown fox jumps over the lazy dog and
the dog barks back at the fox while the quick cat watches the brown dog`)

func init() {
	if len(os.Args) > 1000 {
		words[0] = "a"
	}
}

func sortedKeys[K interface{ ~int | ~string }, V any](m map[K]V) []K {
	ks := make([]K, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	slices.Sort(ks)
	return ks
}

func main() {
	fmt.Println("-- word counts")
	counts := map[string]int{}
	for _, w := range words {
		counts[w]++
	}
	for _, k := range sortedKeys(counts) {
		fmt.Println(k, counts[k])
	}
	fmt.Println(len(counts), counts) // fmt prints maps in key order
	v, ok := counts["fox"]
	v2, ok2 := counts["wolf"]
	fmt.Println(v, ok, v2, ok2)
	delete(counts, "the")
	delete(counts, "missing")
	fmt.Println(len(counts), counts["the"])
	fmt.Println("-- nil map")
	var nm map[string]int
	nv, nok := nm["x"]
	delete(nm, "x")
	fmt.Println(nm == nil, len(nm), nv, nok)
	for range nm {
		fmt.Println("never")
	}
	fmt.Println("-- float keys")
	fm := map[float64]string{}
	negz := math.Copysign(0, -1)
	fm[0] = "zero"
	fm[negz] = "negzero"
	nan := math.NaN()
	fm[nan] = "nan1"
	fm[nan] = "nan2"
	fm[1.5] = "one.five"
	_, found := fm[nan]
	fmt.Println(len(fm), fm[0], found)
	nans := 0
	for k := range fm {
		if k != k {
			nans++
		}
	}
	fmt.Println("nans", nans)
	delete(fm, nan) // removes nothing
	fmt.Println(len(fm))
	clear(fm)
	fmt.Println(len(fm))
	fmt.Println("-- struct and array keys")
	sm := map[key]int{}
	for i := 0; i < 30; i++ {
		k := key{name: words[i%len(words)], id: i % 4, pt: point{i % 2, i % 3}}
		sm[k] += i
	}
	type kv struct {
		k key
		v int
	}
	var list []kv
	for k, v := range sm {
		list = append(list, kv{k, v})
	}
	sort.Slice(list, func(i, j int) bool {
		a, b := list[i].k, list[j].k
		if a.name != b.name {
			return a.name < b.name
		}
		if a.id != b.id {
			return a.id < b.id
		}
		if a.pt.x != b.pt.x {
			return a.pt.x < b.pt.x
		}
		return a.pt.y < b.pt.y
	})
	for _, e := range list {
		fmt.Printf("%+v = %d\n", e.k, e.v)
	}
	am := map[[3]byte]int{}
	for _, w := range words {
		var k [3]byte
		copy(k[:], w)
		am[k] += len(w)
	}
	akeys := slices.Collect(maps.Keys(am))
	slices.SortFunc(akeys, func(a, b [3]byte) int { return strings.Compare(string(a[:]), string(b[:])) })
	for _, k := range akeys {
		fmt.Printf("%q:%d ", k, am[k])
	}
	fmt.Println()
	fmt.Println("-- interface keys")
	im := map[any]string{}
	im[1] = "int 1"
	im[int64(1)] = "int64 1"
	im[uint8(1)] = "uint8 1"
	im[1.0] = "float 1"
	im["1"] = "string 1"
	im[point{1, 1}] = "point"
	im[[2]int{1, 1}] = "array"
	im[true] = "bool"
	im[nil] = "nil"
	im[1] = "int 1 again"
	fmt.Println(len(im), im[1], im[int64(1)], im[uint8(1)], im[1.0], im["1"],
		im[point{1, 1}], im[[2]int{1, 1}], im[true], im[nil], im[int32(1)] == "")
	fmt.Println("-- pointer keys")
	p1, p2 := &point{1, 2}, &point{1, 2}
	pm := map[*point]int{p1: 1, p2: 2}
	pm[p1] += 10
	fmt.Println(len(pm), pm[p1], pm[p2], pm[&point{1, 2}])
	fmt.Println("-- struct values")
	vm := map[string]point{"a": {1, 2}}
	pt := vm["a"]
	pt.x = 100
	fmt.Println(vm["a"], pt)
	vm["a"] = pt
	fmt.Println(vm["a"], vm["zz"])
	ptrm := map[string]*point{"a": {1, 2}}
	ptrm["a"].y = 50
	fmt.Println(*ptrm["a"])
	fmt.Println("-- slice and map values")
	groups := map[int][]string{}
	for _, w := range words {
		groups[len(w)] = append(groups[len(w)], w)
	}
	for _, k := range sortedKeys(groups) {
		fmt.Println(k, groups[k])
	}
	nested := map[string]map[string]int{}
	for i, w := range words {
		inner, ok := nested[w[:1]]
		if !ok {
			inner = map[string]int{}
			nested[w[:1]] = inner
		}
		inner[w] += i
	}
	for _, k := range sortedKeys(nested) {
		fmt.Println(k, nested[k])
	}
	fmt.Println("-- delete during range")
	dm := map[int]int{}
	for i := 0; i < 200; i++ {
		dm[i] = i * i
	}
	for k := range dm {
		if k%3 != 0 {
			delete(dm, k)
		}
	}
	fmt.Println(len(dm))
	rest := sortedKeys(dm)
	fmt.Println(rest[:10], rest[len(rest)-5:])
	for k := range dm {
		if k%12 == 0 {
			// k+3 may or may not have been visited yet; it is never a
			// multiple of 12, so the survivors do not depend on the order.
			delete(dm, k)
			delete(dm, k+3)
		}
	}
	fmt.Println(len(dm), sortedKeys(dm)[:5])
	maps.DeleteFunc(dm, func(k, v int) bool { return k > 100 })
	fmt.Println(sortedKeys(dm))
	fmt.Println("-- growth and shrink")
	big := map[int]int{}
	for i := 0; i < 5000; i++ {
		big[i*7919%10007] = i
	}
	sum := 0
	for k, v := range big {
		sum += k ^ v
	}
	fmt.Println(len(big), sum)
	for i := 0; i < 5000; i += 2 {
		delete(big, i*7919%10007)
	}
	sum = 0
	for k, v := range big {
		sum += k*3 + v
	}
	fmt.Println(len(big), sum, big[7919], big[2*7919%10007])
	for i := 0; i < 1000; i++ {
		big[-i] = i
	}
	fmt.Println(len(big), big[-999])
	fmt.Println("-- clone, equal, collect")
	c := maps.Clone(counts)
	fmt.Println(maps.Equal(c, counts))
	c["new"] = 1
	fmt.Println(maps.Equal(c, counts), len(c), len(counts))
	inv := map[int][]string{}
	for k, v := range counts {
		inv[v] = append(inv[v], k)
	}
	for _, k := range sortedKeys(inv) {
		vs := inv[k]
		slices.Sort(vs)
		fmt.Println(k, vs)
	}
	sortedVals := slices.Sorted(maps.Values(counts))
	fmt.Println(sortedVals)
	collected := maps.Collect(func(yield func(string, int) bool) {
		for i, w := range words[:6] {
			if !yield(w, i) {
				return
			}
		}
	})
	fmt.Println(collected)
}
