// complex64 and complex128.  Construction with complex(), real() and imag(),
// the four operators (Go's division scales to avoid overflow and handles
// infinities and NaNs specially, in runtime code TinyGo carries), equality,
// conversion between the two widths, and math/cmplx: Abs, Phase, Polar/Rect,
// Sqrt, Exp, Log, Pow, Sin/Cos/Tan, Asin/Acos/Atan, Sinh/Cosh/Tanh, Conj,
// Inf/IsInf/IsNaN.  Then complex arithmetic in context: roots of unity, a
// radix-2 FFT and its inverse round trip, Mandelbrot escape counts, and
// polynomial roots by Durand-Kerner.
package main

import (
	"fmt"
	"math"
	"math/cmplx"
	"os"
)

var scale = 1.0

func init() {
	if len(os.Args) > 1000 {
		scale = 2
	}
}

func c(z complex128) string {
	re, im := real(z), imag(z)
	f := func(x float64) string {
		if math.IsNaN(x) {
			return "NaN"
		}
		return fmt.Sprintf("%.6g", x)
	}
	return "(" + f(re) + "," + f(im) + ")"
}

func fft(a []complex128, invert bool) {
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
	for length := 2; length <= n; length <<= 1 {
		ang := 2 * math.Pi / float64(length)
		if invert {
			ang = -ang
		}
		wl := complex(math.Cos(ang), math.Sin(ang))
		for i := 0; i < n; i += length {
			w := complex(1, 0)
			for j := 0; j < length/2; j++ {
				u, v := a[i+j], a[i+j+length/2]*w
				a[i+j], a[i+j+length/2] = u+v, u-v
				w *= wl
			}
		}
	}
	if invert {
		for i := range a {
			a[i] /= complex(float64(n), 0)
		}
	}
}

func mandel(z0 complex128, maxIter int) int {
	z := complex(0, 0)
	for i := 0; i < maxIter; i++ {
		z = z*z + z0
		if real(z)*real(z)+imag(z)*imag(z) > 4 {
			return i
		}
	}
	return maxIter
}

func durandKerner(coef []complex128) []complex128 {
	n := len(coef) - 1
	eval := func(x complex128) complex128 {
		r := complex(0, 0)
		for _, k := range coef {
			r = r*x + k
		}
		return r
	}
	roots := make([]complex128, n)
	seed := complex(0.4, 0.9)
	roots[0] = 1
	for i := 1; i < n; i++ {
		roots[i] = roots[i-1] * seed
	}
	for iter := 0; iter < 200; iter++ {
		for i := range roots {
			den := complex(1, 0)
			for j := range roots {
				if i != j {
					den *= roots[i] - roots[j]
				}
			}
			roots[i] -= eval(roots[i]) / den
		}
	}
	return roots
}

func main() {
	fmt.Println("-- operators")
	zs := []complex128{0, 1, 1i, complex(3*scale, 4), complex(-1.5, 0.5), complex(1e300, 1e300),
		complex(1e-300, -1e-300), complex(math.Inf(1), 0), complex(0, math.NaN())}
	for _, a := range zs[:7] {
		for _, b := range zs {
			fmt.Println(c(a), c(b), c(a+b), c(a-b), c(a*b), c(a/b), a == b)
		}
	}
	fmt.Println("-- complex64")
	for _, a := range []complex64{1 + 2i, complex(float32(scale)/3, -0.1), 1e20 + 1e20i} {
		b := complex64(complex(0.5, -2))
		w := complex128(a)
		fmt.Println(a, a*b, a/b, real(a)*imag(b), w*complex128(b), complex64(w*w), a*a == complex64(w*w))
	}
	fmt.Println("-- cmplx functions")
	args := []complex128{complex(scale, 0), -1, 1i, complex(2, -3), complex(-0.5, 0.25), complex(0, -2)}
	for _, z := range args {
		r, th := cmplx.Polar(z)
		fmt.Println(c(z), fmt.Sprintf("%.6g %.6g %.6g", cmplx.Abs(z), cmplx.Phase(z), r*math.Cos(th)),
			c(cmplx.Rect(r, th)), c(cmplx.Conj(z)), c(cmplx.Sqrt(z)), c(cmplx.Exp(z)), c(cmplx.Log(z)))
		fmt.Println("   ", c(cmplx.Pow(z, 2.5)), c(cmplx.Pow(z, z)), c(cmplx.Sin(z)), c(cmplx.Cos(z)),
			c(cmplx.Tan(z)), c(cmplx.Asin(z)), c(cmplx.Acos(z)), c(cmplx.Atan(z)))
		fmt.Println("   ", c(cmplx.Sinh(z)), c(cmplx.Cosh(z)), c(cmplx.Tanh(z)), c(cmplx.Asinh(z)),
			c(cmplx.Log10(z)), c(cmplx.Sqrt(z)*cmplx.Sqrt(z)))
	}
	fmt.Println(cmplx.IsInf(cmplx.Inf()), cmplx.IsNaN(cmplx.NaN()), cmplx.IsInf(complex(1, math.Inf(-1))),
		cmplx.IsNaN(complex(math.NaN(), math.Inf(1))), c(cmplx.Sqrt(cmplx.Inf())))
	fmt.Println("-- roots of unity")
	for n := 1; n <= 8; n++ {
		sum := complex(0, 0)
		prod := complex(1, 0)
		for k := 0; k < n; k++ {
			w := cmplx.Exp(complex(0, 2*math.Pi*float64(k)/float64(n)))
			sum += w
			prod *= w
		}
		fmt.Println(n, fmt.Sprintf("%.3f %.3f", cmplx.Abs(sum), real(prod)))
	}
	fmt.Println("-- FFT")
	n := 16
	sig := make([]complex128, n)
	for i := range sig {
		sig[i] = complex(math.Sin(2*math.Pi*3*float64(i)/float64(n))+0.5*math.Cos(2*math.Pi*5*float64(i)/float64(n))*scale, 0)
	}
	spec := append([]complex128(nil), sig...)
	fft(spec, false)
	for i, v := range spec {
		if cmplx.Abs(v) > 1e-9 {
			fmt.Printf("bin %d %s %.6f\n", i, c(v), cmplx.Abs(v))
		}
	}
	back := append([]complex128(nil), spec...)
	fft(back, true)
	maxErr := 0.0
	for i := range sig {
		maxErr = math.Max(maxErr, cmplx.Abs(back[i]-sig[i]))
	}
	fmt.Println("round trip ok:", maxErr < 1e-12)
	// Polynomial multiplication through the FFT: (1+2x+3x^2)(4+5x).
	pa := []complex128{1, 2, 3, 0, 0, 0, 0, 0}
	pb := []complex128{4, 5, 0, 0, 0, 0, 0, 0}
	fft(pa, false)
	fft(pb, false)
	for i := range pa {
		pa[i] *= pb[i]
	}
	fft(pa, true)
	for _, v := range pa {
		fmt.Print(math.Round(real(v)), " ")
	}
	fmt.Println()
	fmt.Println("-- mandelbrot")
	for y := 1.2; y >= -1.2; y -= 0.2 {
		line := ""
		for x := -2.0; x <= 0.6; x += 0.08 {
			k := mandel(complex(x*scale, y), 60)
			line += string(" .:-=+*#%@"[min(k/6, 9)])
		}
		fmt.Println(line)
	}
	fmt.Println("-- polynomial roots")
	// (x-1)(x+2)(x-3i)(x+3i) = x^4 + x^3 + 7x^2 + 9x - 18
	roots := durandKerner([]complex128{1, 1, 7, 9, -18})
	for _, r := range roots {
		fmt.Print(c(complex(math.Round(real(r)*1e6)/1e6, math.Round(imag(r)*1e6)/1e6)), " ")
	}
	fmt.Println()
}
