// complex128 arithmetic: Mandelbrot escape counts, Newton's method for
// z^3 = 1, which needs complex division, and a radix-2 FFT whose
// butterflies are complex multiplies.  TinyGo passes a complex128 as two
// f64 arguments and expands every operator inline, so no runtime call is
// left in any of the three loops.  A multiply is four `f64.mul` and two adds
// or subtracts.  A division is the whole sequence Go's semantics need, with
// self-comparisons (`f64.ne`) to catch NaN and `f64.copysign` for the
// infinite cases around the `f64.div`s, most of it branches that are never
// taken.
//
// It is floating-point code with a different layout from the float64
// kernels: values that come in pairs, loaded and stored together, and a
// dependency chain through both halves of each.
package main

//go:noinline
func escape(c complex128, limit int) int {
	z := complex(0, 0)
	for i := 0; i < limit; i++ {
		z = z*z + c
		if real(z)*real(z)+imag(z)*imag(z) > 4 {
			return i
		}
	}
	return limit
}

// One radix-2 FFT pass over the whole vector, in place.  tw holds the
// twiddle factors for the largest butterfly.
//
//go:noinline
func fft(a []complex128, tw []complex128) {
	n := len(a)
	for i, j := 1, 0; i < n; i++ {
		bit := n >> 1
		for ; j&bit != 0; bit >>= 1 {
			j ^= bit
		}
		j ^= bit
		if i < j {
			a[i], a[j] = a[j], a[i]
		}
	}
	for size := 2; size <= n; size <<= 1 {
		half := size >> 1
		step := n / size
		for start := 0; start < n; start += size {
			for k := 0; k < half; k++ {
				w := tw[k*step]
				u := a[start+k]
				v := a[start+k+half] * w
				a[start+k] = u + v
				a[start+k+half] = u - v
			}
		}
	}
}

// Newton's iteration for z^3 - 1, which needs complex division.
//
//go:noinline
func newton(z complex128) int {
	for i := 0; i < 32; i++ {
		z2 := z * z
		d := (z*z2 - 1) / (3 * z2)
		z -= d
		if real(d)*real(d)+imag(d)*imag(d) < 1e-18 {
			if real(z) > 0 {
				return i
			}
			if imag(z) > 0 {
				return i + 64
			}
			return i + 128
		}
	}
	return 255
}

func main() {
	const n = 1024
	// Twiddles by repeated multiplication of the first root, which avoids a
	// call into math's sin and cos: cos(2pi/1024) and -sin(2pi/1024).
	tw := make([]complex128, n/2)
	root := complex(0.9999811752826011, -0.006135884649154475)
	w := complex(1, 0)
	for k := range tw {
		tw[k] = w
		w *= root
	}
	a := make([]complex128, n)

	total := 0
	for r := 0; r < 96; r++ {
		for py := 0; py < 96; py++ {
			for px := 0; px < 128; px++ {
				c := complex(-2.2+float64(px)*0.025+float64(r)*0.001, -1.2+float64(py)*0.025)
				total += escape(c, 120)
			}
		}
		for py := 0; py < 48; py++ {
			for px := 0; px < 48; px++ {
				total += newton(complex(-1.5+float64(px)*0.0625, -1.5+float64(py)*0.0625+float64(r)*0.01))
			}
		}
		for k := 0; k < 12; k++ {
			for i := range a {
				a[i] = complex(float64((i*7+k+r)%13)-6, float64((i*3+k)%5)-2)
			}
			fft(a, tw)
			for i := 0; i < n; i += 5 {
				total += int(real(a[i])*4) ^ int(imag(a[i])*4)
			}
		}
	}
	println("total =", total)
}
