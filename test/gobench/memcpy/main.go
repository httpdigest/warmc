// copy(), which is memory.copy in wasm and lives in the runtime rather than
// in generated code.
//
// A million iterations rather than a round hundred thousand: both engines run
// this at the machine's L2 bandwidth ceiling and differ by well under a
// percent, so at 200000 the kernel's fixed cost and a +-6% noise band were
// larger than anything being measured and the reported ratio swung between
// 1.02x and 1.13x across runs.
package main

//go:noinline
func blit(dst, src []byte) { copy(dst, src) }

func main() {
	a := make([]byte, 65536)
	b := make([]byte, 65536)
	for i := range a {
		a[i] = byte(i)
	}
	for r := 0; r < 1000000; r++ {
		a[r&65535] = byte(r)
		blit(b, a)
	}
	var s int
	for i := range b {
		s += int(b[i])
	}
	println("s =", s)
}
