// Fixed-size arrays as values: a [16]byte state passed to functions and
// returned from them by value and compared with ==, and a [4]int32 vector
// and a [4][4]int32 matrix passed the same way.  TinyGo flattens a small
// array argument into one wasm parameter per element, so permute takes
// eighteen i32 parameters -- a pointer for the result, sixteen bytes and the
// permutation -- and mulmat twenty-one.  The result comes back through memory
// the caller provides and is reloaded a byte at a time with `i32.load8_u`,
// and == on [16]byte stores both arrays back to the stack and calls
// runtime.memequal.
//
// No other kernel has calls this wide: more arguments than a machine has
// argument registers, and sixteen values live across every call in order's
// loop, so it measures argument passing and register pressure at call
// boundaries rather than arithmetic.
package main

type state [16]byte

// permute returns a new state; both the argument and the result are
// arrays passed by value.
//
//go:noinline
func permute(s state, p *[16]byte) state {
	var out state
	for i, j := range p {
		out[i] = s[j]
	}
	return out
}

// rotate turns the 4x4 grid a quarter turn, again array in, array out.
//
//go:noinline
func rotate(s state) state {
	var out state
	for y := 0; y < 4; y++ {
		for x := 0; x < 4; x++ {
			out[x*4+3-y] = s[y*4+x]
		}
	}
	return out
}

// order finds how many applications of a move bring the state back to
// where it started, comparing whole arrays with ==.
//
//go:noinline
func order(start state, p *[16]byte, turn bool) int {
	s := start
	for n := 1; n < 100000; n++ {
		s = permute(s, p)
		if turn {
			s = rotate(s)
		}
		if s == start {
			return n
		}
	}
	return -1
}

type vec4 [4]int32

//go:noinline
func add4(a, b vec4) vec4 {
	return vec4{a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]}
}

//go:noinline
func mulmat(m [4]vec4, v vec4) vec4 {
	var out vec4
	for i := 0; i < 4; i++ {
		out[i] = m[i][0]*v[0] + m[i][1]*v[1] + m[i][2]*v[2] + m[i][3]*v[3]
	}
	return out
}

func main() {
	perms := make([][16]byte, 32)
	x := uint32(12345)
	for k := range perms {
		for i := range perms[k] {
			perms[k][i] = byte(i)
		}
		// Fisher-Yates from a fixed seed.
		for i := 15; i > 0; i-- {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			j := int(x % uint32(i+1))
			perms[k][i], perms[k][j] = perms[k][j], perms[k][i]
		}
	}
	var m [4]vec4
	for i := range m {
		for j := range m[i] {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			m[i][j] = int32(x%5) - 2
		}
	}
	total := 0
	for r := 0; r < 96; r++ {
		var start state
		for i := range start {
			start[i] = byte((i*5 + r) & 15)
		}
		for k := range perms {
			total += order(start, &perms[k], false)*3 + order(start, &perms[k], true)
		}
		// A table of arrays compared by value.
		seen := make([]state, 0, 64)
		s := start
		for i := 0; i < 2000; i++ {
			s = permute(rotate(s), &perms[i&31])
			dup := false
			for _, t := range seen {
				if t == s {
					dup = true
					break
				}
			}
			if !dup && len(seen) < cap(seen) {
				seen = append(seen, s)
			}
			total += int(s[i&15])
		}
		total += len(seen)
		v := vec4{1, int32(r), 3, 4}
		for i := 0; i < 100000; i++ {
			v = add4(mulmat(m, v), vec4{int32(i), -1, 1, int32(i & 7)})
			v = vec4{v[0] & 0xffff, v[1] & 0xffff, v[2] & 0xffff, v[3] & 0xffff}
		}
		total += int(v[0] + v[1] + v[2] + v[3])
	}
	println("total =", total)
}
