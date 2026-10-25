// Integer and Long's divideUnsigned and remainderUnsigned by zero, each the
// only thing in its try (test/teavm/patches/integer-division.patch): TeaVM's
// dependency analysis has to be told they throw ArithmeticException, or the
// catch has nothing to catch.  Built with --strict.
public class Main {
    static String message;

    static int quotient(int a, int b) {
        try {
            return Integer.divideUnsigned(a, b);
        } catch (ArithmeticException e) {
            message = e.getMessage();
            return -1;
        }
    }

    static int remainder(int a, int b) {
        try {
            return Integer.remainderUnsigned(a, b);
        } catch (ArithmeticException e) {
            message = e.getMessage();
            return -2;
        }
    }

    static long quotient(long a, long b) {
        try {
            return Long.divideUnsigned(a, b);
        } catch (ArithmeticException e) {
            message = e.getMessage();
            return -3;
        }
    }

    static long remainder(long a, long b) {
        try {
            return Long.remainderUnsigned(a, b);
        } catch (ArithmeticException e) {
            message = e.getMessage();
            return -4;
        }
    }

    public static void main(String[] args) {
        int[] ints = {-1, 7, 0};
        long[] longs = {-1, 7, 0};
        for (int b : ints) {
            message = null;
            int q = quotient(-7, b);
            String afterQuotient = message;
            message = null;
            int r = remainder(-7, b);
            System.out.println(b + ": " + q + " " + afterQuotient + ", " + r + " " + message);
        }
        for (long b : longs) {
            message = null;
            long q = quotient(-7L, b);
            String afterQuotient = message;
            message = null;
            long r = remainder(-7L, b);
            System.out.println(b + ": " + q + " " + afterQuotient + ", " + r + " " + message);
        }
    }
}
