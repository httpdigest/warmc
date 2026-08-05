// Bytes: sixteen lanes, the widest this machine offers, and the lane width
// where a scalar loop loses by the most.
package main

//go:noinline
func kern(a, b, c []byte) {
	for i := range c {
		c[i] = a[i] + b[i]
	}
}

func main() {
	const n = 4096
	a := make([]byte, n)
	b := make([]byte, n)
	c := make([]byte, n)
	for i := range a {
		a[i] = byte(i*7 + 1)
		b[i] = byte(i*3 + 2)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = byte(r)
		kern(a, b, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
