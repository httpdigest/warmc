// c[i] = a[i] + a[i+1]: one array read at two offsets one element apart.
// Contiguous, but the two reads are not the same vector, so the second has
// to come from an unaligned load or from combining two aligned ones.
package main

//go:noinline
func kern(a, c []int32) {
	for i := 0; i < len(c)-1; i++ {
		c[i] = a[i] + a[i+1]
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
	for r := 0; r < 40000; r++ {
		a[r&(n-1)] = int32(r)
		kern(a, c)
		total += int(c[r&(n-2)])
	}
	println("total =", total)
}
