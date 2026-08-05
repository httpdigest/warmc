// A nested loop: the inner one is vectorisable and the outer one is not.
// Whether the inner loop is reached at all is the question.
package main

//go:noinline
func kern(m []int32, rows, cols int, out []int32) {
	for r := 0; r < rows; r++ {
		var s int32
		row := m[r*cols : r*cols+cols]
		for i := range row {
			s += row[i]
		}
		out[r] = s
	}
}

func main() {
	const n = 4096
	a := make([]int32, n)
	c := make([]int32, n)
	for i := range a {
		a[i] = int32(i%97 + 1)
	}
	total := 0
	for r := 0; r < 20000; r++ {
		a[r&(n-1)] = int32(r%97 + 1)
		kern(a, 64, 64, c[:64])
		total += int(c[r&63])
	}
	println("total =", total)
}
