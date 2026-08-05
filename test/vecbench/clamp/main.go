// Two min/max operations chained elementwise, which is a clamp -- and unlike
// the reduction case these are not accumulators, so they have to be
// recognised in the body rather than as a carried value.
package main

//go:noinline
func kern(a, c []int32) {
	for i := range c {
		v := a[i]
		if v < -50 {
			v = -50
		}
		if v > 50 {
			v = 50
		}
		c[i] = v
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i%401) - 200
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r%401) - 200
		kern(a, c)
		total += int(c[r&(n-1)])
	}
	println("total =", total)
}
