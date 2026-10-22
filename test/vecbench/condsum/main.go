// A sum of the elements whose flag is set: if keep[i] { s += a[i] }, with the
// flags a []bool.  LLVM does not if-convert this into a select.  It splits it
// into two nested loops instead: an inner one that skips over unset flags
// (i32.load8_u, i32.eqz, br_if back) and an outer one that adds the element
// it stopped at, with the loop bound recomputed by a select each time round.
//
// Vectorised, sixteen bools widen to four masks, which AND the values before
// they are summed.  warmc does not vectorise it yet: the inner loop is
// rejected because its body branches out, the outer because it does not end
// in a two-way branch.  It is also slower than wasmtime as a scalar loop: the
// inner loop ends in a conditional exit followed by an unconditional branch
// back, three branches per skipped flag where Cranelift's has two.  Here to
// show the gap, and because this is what LLVM makes of a masked sum.
package main

//go:noinline
func kern(a []int32, keep []bool) int32 {
	var s int32
	for i := range a {
		if keep[i] {
			s += a[i]
		}
	}
	return s
}

func main() {
	const n = 4096
	a := make([]int32, n)
	keep := make([]bool, n)
	for i := range a {
		a[i] = int32(i%251) - 100
		keep[i] = (i*7)%5 < 2
	}
	total := 0
	for r := 0; r < 50000; r++ {
		a[r&(n-1)] = int32(r%251) - 100
		keep[(r*13)&(n-1)] = r%3 == 0
		total += int(kern(a, keep))
	}
	println("total =", total)
}
