// The same reduction counted downwards.  Addition is commutative, so the
// direction of travel does not change the answer and should not change
// whether it vectorises.
package main

//go:noinline
func kern(a []int32) int32 {
	var s int32
	for i := len(a) - 1; i >= 0; i-- {
		s += a[i] * 3
	}
	return s
}

func main() {
	const n = 4096
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(i%251 + 1)
	}
	total := 0
	for r := 0; r < 30000; r++ {
		a[r&(n-1)] = int32(r%251 + 1)
		total += int(kern(a))
	}
	println("total =", total)
}
