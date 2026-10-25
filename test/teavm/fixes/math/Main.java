// Math.rint, Math.round and Math.ulp as Java has them (test/teavm/patches/
// math-rint-round-ulp.patch): rint rounds half to even and keeps the sign of
// a zero, round is floor(x + 1/2) taken exactly, and ulp is the gap to the
// next double from x's binade.  Doubles are printed by their bits, which
// leaves Double.toString out of it.
public class Main {
    static final double[] DOUBLES = {
        0.0, -0.0, 0.3, -0.3, 0.5, -0.5, 0.7, -0.7, 1.0, -1.0, 1.5, -1.5, 2.5, -2.5, 3.5, -3.5,
        0.49999999999999994, -0.49999999999999994, 0.5000000000000001, -0.5000000000000001,
        2.5000000000000004, -2.4999999999999996, 1.0000000000000002, 0.9999999999999999,
        4503599627370495.5, -4503599627370495.5, 4503599627370496.0, 4503599627370497.0,
        9007199254740993.0, -9007199254740993.0, 1e15 + 0.5, 1e300, -1e300, 1e19, -1e19,
        9.223372036854776e18, -9.223372036854776e18, 2147483647.5, -2147483648.5,
        Double.MIN_VALUE, -Double.MIN_VALUE, Double.MIN_NORMAL, 2.2250738585072009e-308,
        Double.MAX_VALUE, -Double.MAX_VALUE, Double.NaN, Double.POSITIVE_INFINITY,
        Double.NEGATIVE_INFINITY, Math.PI, -Math.E, 1e-300, 6.02e23
    };
    static final float[] FLOATS = {
        0.0f, -0.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 0.49999997f, -0.49999997f,
        8388607.5f, -8388607.5f, 8388609f, 16777215f, 16777217f, 1e10f, -1e10f, 2.1474836e9f,
        -2.1474836e9f, Float.MIN_VALUE, Float.MIN_NORMAL, Float.MAX_VALUE, Float.NaN,
        Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, 3.4028235e38f, 1.1754942e-38f
    };

    static String bits(double d) {
        return Long.toHexString(Double.doubleToRawLongBits(d));
    }

    static String bits(float f) {
        return Integer.toHexString(Float.floatToRawIntBits(f));
    }

    public static void main(String[] args) {
        for (double d : DOUBLES) {
            System.out.println(bits(d) + ": rint " + bits(Math.rint(d)) + ", round " + Math.round(d)
                    + ", ulp " + bits(Math.ulp(d)) + ", StrictMath " + bits(StrictMath.rint(d)) + " "
                    + StrictMath.round(d) + " " + bits(StrictMath.ulp(d)));
        }
        for (float f : FLOATS) {
            System.out.println(bits(f) + ": round " + Math.round(f) + ", ulp " + bits(Math.ulp(f))
                    + ", rint " + bits((float) Math.rint(f)));
        }
        // in a loop, where the argument is not a constant
        long sum = 0;
        double x = -8.0;
        for (int i = 0; i < 64; i++) {
            sum = sum * 31 + Math.round(x) + (long) Math.rint(x);
            x += 0.25;
        }
        System.out.println("sum " + sum);
    }
}
