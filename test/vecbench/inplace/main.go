// One slice read and written in the same loop.  The load and the store alias
// exactly, which is the case a dependence test must accept -- refusing it
// would give up on half of all real loops.
package main

//go:noinline
func kern(a []int32) {
	for i := range a {
		a[i] = a[i]*3 + 1
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	for i := range a {
		a[i] = int32(i%97 + 1)
	}
	total := 0
	for r := 0; r < 200000; r++ {
		a[r&(n-1)] = int32(r%97 + 1)
		kern(a)
		total += int(a[r&(n-1)] & 0xffff)
	}
	println("total =", total)
}
