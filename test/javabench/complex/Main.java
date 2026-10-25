// The Mandelbrot set with an immutable complex number class.
//
// Every operation returns a new Complex, so the hot loop is allocation the
// program never needs: the same shape as ktbench/complex, where the difference
// between engines is whether those objects are made at all.
public class Main {
    static final class Complex {
        final double re, im;

        Complex(double re, double im) {
            this.re = re;
            this.im = im;
        }

        Complex plus(Complex o) {
            return new Complex(re + o.re, im + o.im);
        }

        Complex times(Complex o) {
            return new Complex(re * o.re - im * o.im, re * o.im + im * o.re);
        }

        double norm2() {
            return re * re + im * im;
        }
    }

    public static void main(String[] args) {
        int inside = 0;
        long iters = 0;
        int w = 640, h = 400;
        for (int py = 0; py < h; py++) {
            for (int px = 0; px < w; px++) {
                Complex c = new Complex(-2.0 + 3.0 * px / w, -1.2 + 2.4 * py / h);
                Complex z = new Complex(0, 0);
                int n = 0;
                while (n < 300 && z.norm2() <= 4.0) {
                    z = z.times(z).plus(c);
                    n++;
                }
                iters += n;
                if (n == 300) {
                    inside++;
                }
            }
        }
        System.out.println("inside = " + inside + " iters = " + iters);
    }
}
