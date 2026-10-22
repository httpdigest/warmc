// Maps keyed by structs and by arrays, and a map whose values are slices
// grown by append through the map.  TinyGo picks a map implementation per
// key type.  The struct keys go to the generic map, which reaches a hash
// function and an equality function the compiler synthesises for that one
// key type (`hashmapKeyHash.struct{int32; int32}` and its `KeyEqual`
// partner) through `call_indirect`, and the hash calls runtime.hash32 once
// per field.  The [4]byte key goes to the binary map, which hashes the raw
// bytes and compares them with runtime.memequal.  The map of slices is
// string-keyed, and every `m[k] = append(m[k], i)` is a lookup, an append
// and a store back.
//
// No other kernel has a composite map key, and the per-type hash and
// equality called indirectly from inside the runtime are a shape only
// TinyGo produces.  Everything is read back by lookup in a fixed order,
// never by ranging over a map: each map's hash seed comes from
// runtime.fastrand, which start-up seeds from random_get.
package main

type point struct{ x, y int32 }

type cell struct {
	p    point
	kind uint8
}

func main() {
	const n = 1024
	pts := make([]point, n)
	keys := make([][4]byte, n)
	names := []string{"red", "green", "blue", "cyan", "magenta", "yellow", "black"}
	x := uint32(7)
	for i := range pts {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		pts[i] = point{int32(x & 63), int32((x >> 6) & 63)}
		keys[i] = [4]byte{byte(x >> 12), byte(x>>20) & 7, byte(i & 3), 1}
	}
	total := 0
	for r := 0; r < 150; r++ {
		// Struct keys: count visits per grid point.
		visits := make(map[point]int, 256)
		for _, p := range pts {
			visits[p]++
			visits[point{p.y, p.x}] += 2
		}
		// A key that is itself a struct holding a struct.
		kinds := make(map[cell]int32)
		for i, p := range pts {
			kinds[cell{p, uint8(i & 3)}] += int32(i)
		}
		// Array keys.
		arr := make(map[[4]byte]int)
		for i, k := range keys {
			arr[k] += i ^ r
		}
		// A map of slices, grown by append through the map.
		groups := make(map[string][]int)
		for i, p := range pts {
			name := names[(int(p.x)+r)%len(names)]
			groups[name] = append(groups[name], i)
		}

		// Read everything back in a fixed order, never by ranging over a map.
		for i, p := range pts {
			total += visits[p] + visits[point{p.x + 1, p.y}]
			total += int(kinds[cell{p, uint8(i & 3)}] & 0xffff)
			total += arr[keys[(i*7)&(n-1)]] & 0xfff
		}
		for _, name := range names {
			g := groups[name]
			total += len(g)
			if len(g) > 0 {
				total += g[len(g)/2] + g[len(g)-1]
			}
		}
		total += len(visits) + len(kinds) + len(arr) + len(groups)
		pts[r&(n-1)] = point{int32(r), int32(r * 3 & 63)}
	}
	println("total =", total)
}
