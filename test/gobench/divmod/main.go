// Integer division and remainder, which wasm traps on for a zero divisor --
// so every one carries a check the compiler has to prove away or emit.
package main

//go:noinline
func hash(a []uint32, d uint32) uint32 {
	var h uint32
	for i := 0; i < len(a); i++ {
		h += a[i]/d + a[i]%(d+1)
	}
	return h
}

func main() {
	a := make([]uint32, 4096)
	for i := range a {
		a[i] = uint32(i)*2654435761 + 1
	}
	var t uint32
	for r := 0; r < 20000; r++ {
		a[r&4095] = uint32(r) + 1
		t += hash(a, uint32(r&15)+3)
	}
	println("t =", t)
}
