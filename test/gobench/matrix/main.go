// A 2D slice of slices: two indirections and two bounds checks per element,
// which is what Go's []([]T) really is and what a flat array kernel misses.
package main

//go:noinline
func mul(a, b, c [][]float64, n int) {
	for i := 0; i < n; i++ {
		for k := 0; k < n; k++ {
			aik := a[i][k]
			for j := 0; j < n; j++ {
				c[i][j] += aik * b[k][j]
			}
		}
	}
}

func main() {
	const n = 64
	a := make([][]float64, n)
	b := make([][]float64, n)
	c := make([][]float64, n)
	for i := 0; i < n; i++ {
		a[i] = make([]float64, n)
		b[i] = make([]float64, n)
		c[i] = make([]float64, n)
		for j := 0; j < n; j++ {
			a[i][j] = float64((i*j)%7) + 1
			b[i][j] = float64((i+j)%5) + 1
		}
	}
	for r := 0; r < 700; r++ {
		a[r%n][0] = float64(r%9) + 1
		mul(a, b, c, n)
	}
	total := 0
	for i := 0; i < n; i++ {
		total += int(c[i][i])
	}
	println("total =", total)
}
