// A store the loop only sometimes performs.  Vectorisable by reading the
// destination, blending, and writing it all back -- which is only sound
// because the whole vector's range was already proven in bounds.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		if a[i] > 0 {
			c[i] = a[i]
		}
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i%201) - 100
	}
	total := 0
	for r := 0; r < 40000; r++ {
		a[r&(n-1)] = int32(r%201) - 100
		kern(a, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
