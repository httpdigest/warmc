// Every other element: c[i] = a[2*i] + a[2*i+1].  Not contiguous, so a single
// vector load cannot serve it -- warmc loads two vectors and deinterleaves
// them with UZP1 and UZP2, which is why this is 0.13x and not a refusal.
// Here to keep that working: the obvious implementation of a stride is to
// give up on it.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		c[i] = a[2*i] + a[2*i+1]
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i*7 + 1)
	}
	total := 0
	for r := 0; r < 320000; r++ {
		a[r&(n-1)] = int32(r)
		kern(a, c[:n/2])
		total += int(c[(r&(n-1))/2])
	}
	println("total =", total)
}
