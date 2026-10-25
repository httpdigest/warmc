/*
 * FdLibm.java against java.lang.StrictMath, on the JVM: StrictMath is
 * specified to give fdlibm's results, so every function must agree with it bit
 * for bit (any NaN agrees with any NaN).  Run by test/run_teavm.py:
 *
 *   java -cp CLASSES warmc.teavm.FdLibmCheck [COUNT]
 *
 * Each function gets the special values and boundaries its code branches on,
 * then COUNT random arguments drawn from every exponent.
 *
 * JDK 27 changed one branch of StrictMath.pow: for |y| > 2^31 and x within
 * 2^-20 of 1 it takes 1/ln2's head to 21 bits, as fdlibm's comment says it
 * has, where fdlibm 5.3's constant has 24 and its result can be an ulp out.
 * FdLibm.java is fdlibm 5.3, as JavaScript's engines port it and as every JDK
 * up to 25 computes it.  On a JDK with the change those arguments are left
 * out, and the run says so.
 */
package warmc.teavm;

import java.util.Random;
import java.util.function.DoubleBinaryOperator;
import java.util.function.DoubleUnaryOperator;

public final class FdLibmCheck {
    private static int failures;
    private static long checked;

    private static boolean same(double a, double b) {
        return Double.doubleToLongBits(a) == Double.doubleToLongBits(b);
    }

    private static void one(String name, DoubleUnaryOperator got, DoubleUnaryOperator want, double x) {
        double a = got.applyAsDouble(x);
        double b = want.applyAsDouble(x);
        checked++;
        if (!same(a, b) && failures++ < 20) {
            System.out.printf("FAIL %s(%s [%016x]) = %s, StrictMath %s%n", name, x,
                    Double.doubleToRawLongBits(x), a, b);
        }
    }

    // StrictMath.pow from JDK 27 on, whose near-one branch differs (above)
    private static boolean jdk27Pow;
    private static long skipped;

    private static boolean nearOneBranch(double x, double y) {
        int ix = (int) (Double.doubleToRawLongBits(x) >> 32) & 0x7fffffff;
        int iy = (int) (Double.doubleToRawLongBits(y) >> 32) & 0x7fffffff;
        return iy > 0x41e00000 && iy <= 0x7ff00000 && ix >= 0x3fefffff && ix <= 0x3ff00000;
    }

    private static void two(String name, DoubleBinaryOperator got, DoubleBinaryOperator want, double x, double y) {
        if (jdk27Pow && name.equals("pow") && nearOneBranch(x, y)) {
            skipped++;
            return;
        }
        double a = got.applyAsDouble(x, y);
        double b = want.applyAsDouble(x, y);
        checked++;
        if (!same(a, b) && failures++ < 20) {
            System.out.printf("FAIL %s(%s, %s) = %s, StrictMath %s%n", name, x, y, a, b);
        }
    }

    private static final double[] SPECIAL = {
        0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 0.25, 1.5, -1.5,
        Double.MIN_VALUE, -Double.MIN_VALUE, Double.MIN_NORMAL, -Double.MIN_NORMAL,
        Double.MAX_VALUE, -Double.MAX_VALUE, Double.POSITIVE_INFINITY,
        Double.NEGATIVE_INFINITY, Double.NaN, Math.PI, -Math.PI, Math.PI / 2,
        -Math.PI / 2, Math.PI / 4, 3 * Math.PI / 4, Math.E, 0.1, 1e-300, 1e300,
        709.782712893384, 709.79, -745.1332191019411, -745.14, 1e22, 1e-22,
        0x1p-27, 0x1p-28, 0x1p-29, 0x1p-57, 0x1p66, 0x1p19 * Math.PI, 0x1p20 * Math.PI,
        0.975, 0.4375, 1.1875, 2.4375, 0.6744, 0.3, 0.78125, 1e-8, 1 - 1e-16, 1 + 1e-15,
        0.6931471805599453, 0.34657359027997264, 1.0397207708399179, 1.0e9, 1.0e-9,
    };

    public static void main(String[] args) {
        int count = args.length > 0 ? Integer.parseInt(args[0]) : 200000;
        Random rnd = new Random(1);
        // fdlibm 5.3 gives 0x0b44c5fb3a802ad5 here, JDK 27's StrictMath the next double up
        jdk27Pow = Double.doubleToRawLongBits(StrictMath.pow(1.000000002695306, -2.1669598768308884E11))
                != Double.doubleToRawLongBits(FdLibm.pow(1.000000002695306, -2.1669598768308884E11));

        String[] names = {"sin", "cos", "tan", "asin", "acos", "atan", "exp", "log"};
        DoubleUnaryOperator[] got = {FdLibm::sin, FdLibm::cos, FdLibm::tan, FdLibm::asin,
            FdLibm::acos, FdLibm::atan, FdLibm::exp, FdLibm::log};
        DoubleUnaryOperator[] want = {StrictMath::sin, StrictMath::cos, StrictMath::tan,
            StrictMath::asin, StrictMath::acos, StrictMath::atan, StrictMath::exp, StrictMath::log};
        for (int f = 0; f < names.length; f++) {
            for (double x : SPECIAL) {
                for (double d : new double[] {x, Math.nextUp(x), Math.nextDown(x)}) {
                    one(names[f], got[f], want[f], d);
                    one(names[f], got[f], want[f], -d);
                }
            }
            // the multiples of pi/2 that rem_pio2 treats one by one, and past them
            for (int k = -100; k <= 100; k++) {
                double x = k * (Math.PI / 2);
                one(names[f], got[f], want[f], x);
                one(names[f], got[f], want[f], Math.nextUp(x));
                one(names[f], got[f], want[f], Math.nextDown(x));
            }
            // near multiples of pi/2, as far from one as each round of
            // rem_pio2's reduction is for (it takes a second past 16 bits of
            // cancellation and a third past 49), and past the medium range
            for (int k = 1; k <= 4000; k += (k < 64 ? 1 : 37)) {
                for (int e = 1; e <= 60; e++) {
                    double x = k * (Math.PI / 2) * (1 + Math.scalb(1.0, -e));
                    one(names[f], got[f], want[f], x);
                    one(names[f], got[f], want[f], -x);
                }
            }
            for (int i = 0; i < count / 10; i++) {
                one(names[f], got[f], want[f], Math.scalb(1 + rnd.nextDouble(), 20 + rnd.nextInt(1000)));
                // exp's and log's edges: underflow to subnormal, overflow, near 1
                one(names[f], got[f], want[f], -745.2 + rnd.nextDouble() * 50);
                one(names[f], got[f], want[f], 700 + rnd.nextDouble() * 10);
                one(names[f], got[f], want[f], 1 + (rnd.nextDouble() - 0.5) * Math.scalb(1.0, -rnd.nextInt(40)));
                one(names[f], got[f], want[f], Double.MIN_VALUE * (1 + rnd.nextInt(1 << 20)));
            }
            for (int i = 0; i < count; i++) {
                one(names[f], got[f], want[f], Double.longBitsToDouble(rnd.nextLong()));
                // and the range most programs use
                one(names[f], got[f], want[f], (rnd.nextDouble() - 0.5) * Math.scalb(1.0, rnd.nextInt(40) - 20));
            }
        }

        for (double x : SPECIAL) {
            for (double y : SPECIAL) {
                two("atan2", FdLibm::atan2, StrictMath::atan2, x, y);
                two("pow", FdLibm::pow, StrictMath::pow, x, y);
            }
        }
        for (int i = 0; i < count; i++) {
            double x = Double.longBitsToDouble(rnd.nextLong());
            double y = Double.longBitsToDouble(rnd.nextLong());
            two("atan2", FdLibm::atan2, StrictMath::atan2, x, y);
            two("pow", FdLibm::pow, StrictMath::pow, x, y);
            double a = (rnd.nextDouble() - 0.5) * Math.scalb(1.0, rnd.nextInt(60) - 30);
            double b = (rnd.nextDouble() - 0.5) * Math.scalb(1.0, rnd.nextInt(16) - 4);
            two("atan2", FdLibm::atan2, StrictMath::atan2, a, b);
            two("pow", FdLibm::pow, StrictMath::pow, Math.abs(a), b);
            // integer powers of negative bases, odd and even, and near 1
            two("pow", FdLibm::pow, StrictMath::pow, -Math.abs(a), (double) (rnd.nextInt(41) - 20));
            two("pow", FdLibm::pow, StrictMath::pow, 1.0 + (rnd.nextDouble() - 0.5) * 1e-7,
                    (rnd.nextDouble() - 0.5) * 1e12);
        }

        if (failures > 0) {
            System.out.printf("fdlibm: %d of %d results differ from StrictMath%n", failures, checked);
            System.exit(1);
        }
        System.out.printf("fdlibm: %d results agree with StrictMath bit for bit%n", checked);
        if (jdk27Pow) {
            System.out.printf("fdlibm: %d pow arguments left out: this JDK's StrictMath.pow has JDK 27's"
                    + " change for |y| > 2^31 near x = 1%n", skipped);
        }
    }
}
