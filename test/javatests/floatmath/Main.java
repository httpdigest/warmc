// Floating point: Double and Float toString across magnitudes, NaN and the
// infinities in arithmetic and comparison, -0.0, conversions both ways with
// saturation, rounding functions, Math's transcendental functions printed in
// full (sin, cos, tan, asin, acos, atan, atan2, exp, log, pow, sqrt, cbrt,
// hypot), parsing, compare and equals on boxed doubles, and float arithmetic
// rounded to float at each step.
//
// The functions TeaVM leaves to its host come from test/teavm/FdLibm.java
// under test/teavm/wasi.py; the rest are TeaVM's own Java over them.
public class Main {
    static void row(String label, double... xs) {
        StringBuilder sb = new StringBuilder(label);
        for (double x : xs) {
            sb.append(' ').append(x);
        }
        System.out.println(sb);
    }

    public static void main(String[] args) {
        System.out.println("== toString ==");
        row("doubles", 0.1, 1.0 / 3, 100.0, 1e7, 1.0e-3, 1e-4, 123456789.123, 1e21, 1e-300, Double.MAX_VALUE,
                Double.MIN_VALUE, Double.MIN_NORMAL, -0.0, 2.0 / 3 * 3);
        System.out.println("floats " + 0.1f + " " + (1.0f / 3) + " " + 1e10f + " " + Float.MIN_VALUE + " " + Float.MAX_VALUE
                + " " + (float) Math.PI + " " + 16777217f);

        System.out.println("== special values ==");
        double nan = Double.NaN, inf = Double.POSITIVE_INFINITY;
        row("arith", inf - inf, inf * 0, 1 / inf, -1 / 0.0, 0.0 / 0.0, nan + 1, Math.sqrt(-1), inf + 1e308);
        System.out.println("compare " + (nan == nan) + " " + (nan != nan) + " " + (nan < 1) + " " + Double.isNaN(nan)
                + " " + Double.compare(nan, inf) + " " + Double.compare(-0.0, 0.0) + " " + (-0.0 == 0.0)
                + " " + Double.valueOf(nan).equals(nan) + " " + Double.valueOf(-0.0).equals(0.0));
        System.out.println("min max " + Math.min(-0.0, 0.0) + " " + Math.max(-0.0, 0.0) + " " + Math.min(nan, 1) + " "
                + Math.max(1.0f, Float.NaN) + " " + Math.abs(-0.0) + " " + Math.copySign(3, -0.0));

        System.out.println("== conversions ==");
        double[] vs = {3.99, -3.99, 2.5, -2.5, 1e20, -1e20, nan, inf, -inf, 2147483647.5, -2147483648.9};
        StringBuilder sb = new StringBuilder();
        for (double v : vs) {
            sb.append((int) v).append('/').append((long) v).append('/').append((short) v).append('/').append((char) v + 0)
                    .append(' ');
        }
        System.out.println(sb.toString().trim());
        System.out.println((double) Long.MAX_VALUE + " " + (float) Long.MAX_VALUE + " " + (double) (1L << 53) + " "
                + (double) ((1L << 53) + 1) + " " + (float) 16777217 + " " + (long) (double) Long.MIN_VALUE);
        System.out.println(Double.doubleToLongBits(1.0) + " " + Double.doubleToRawLongBits(-0.0) + " "
                + Float.floatToIntBits(1.5f) + " " + Double.longBitsToDouble(0x400921FB54442D18L) + " "
                + Float.intBitsToFloat(0x7f800000));

        System.out.println("== rounding ==");
        for (double v : new double[] {2.5, 3.5, -2.5, -0.5, 0.49999999999999994, 1e16 + 1, -7.1}) {
            System.out.print(Math.round(v) + "," + Math.floor(v) + "," + Math.ceil(v) + " ");
        }
        System.out.println();
        System.out.println(Math.round(2.5f) + " " + Math.round(-2.5f) + " " + Math.floorDiv(-7, 2) + " " + Math.floorMod(-7, 2));

        System.out.println("== functions ==");
        double[] xs = {0.0, 0.5, 1.0, -1.25, 3.0, 10.0, 1e-8, 100.0, Math.PI / 2, 1e6};
        for (double x : xs) {
            row("x=" + x, Math.sin(x), Math.cos(x), Math.tan(x), Math.atan(x), Math.exp(x / 10), Math.log(Math.abs(x) + 1),
                    Math.sqrt(Math.abs(x)), Math.cbrt(x));
        }
        row("inverse", Math.asin(0.5), Math.acos(0.5), Math.asin(-1), Math.acos(-1), Math.asin(2), Math.atan2(1, 1),
                Math.atan2(-1, -1), Math.atan2(0.0, -0.0), Math.atan2(-0.0, 1));
        row("pow", Math.pow(2, 10), Math.pow(2, 0.5), Math.pow(-8, 1.0 / 3), Math.pow(-2, 3), Math.pow(0, 0), Math.pow(10, -3),
                Math.pow(1.0000001, 1e7), Math.pow(nan, 0), Math.pow(1, nan), Math.pow(-0.0, -1));
        row("exp log", Math.exp(1), Math.exp(-745.2), Math.exp(709.8), Math.log(Math.E), Math.log(0), Math.log(-1),
                Math.log(Double.MIN_VALUE), Math.hypot(3, 4), Math.hypot(1e300, 1e300), Math.toRadians(180));
        double big = 1e22;
        row("large args", Math.sin(big), Math.cos(big), Math.tan(1e300), Math.sin(-1e15), Math.cos(Math.PI * 1e8));

        System.out.println("== parsing ==");
        String[] ins = {"3.25", "-0", "1e10", ".5", "5.", "NaN", "-Infinity", "0x1.8p1", "  42  ", "1_0"};
        for (String in : ins) {
            try {
                System.out.print(Double.parseDouble(in) + " ");
            } catch (NumberFormatException e) {
                System.out.print("NFE ");
            }
        }
        System.out.println();
        System.out.println(Float.parseFloat("3.4028236e38") + " " + Double.valueOf("2.5e-3") + " " + Double.toString(123e-20));

        System.out.println("== float steps ==");
        float f = 0;
        for (int i = 0; i < 1000; i++) {
            f += 0.1f;
        }
        double d = 0;
        for (int i = 0; i < 1000; i++) {
            d += 0.1;
        }
        float prod = 1;
        for (int i = 1; i < 30; i++) {
            prod *= 1.1f;
        }
        System.out.println(f + " " + d + " " + prod + " " + (float) (0.1 + 0.2) + " " + (0.1f + 0.2f) + " " + (0.1f + 0.2));
    }
}
