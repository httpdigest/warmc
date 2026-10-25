// division without --strict: MIN_VALUE / -1 is MIN_VALUE there as well
// (test/teavm/patches/integer-division.patch), where a zero divisor still
// traps, as a null dereference does.  See division.
public class Main {
    static final int[] INTS = {Integer.MIN_VALUE, Integer.MIN_VALUE + 1, -7, -2, -1, 1, 2, 7,
        Integer.MAX_VALUE};
    static final long[] LONGS = {Long.MIN_VALUE, Long.MIN_VALUE + 1, Integer.MIN_VALUE, -7, -1, 1, 7,
        1L << 40, Long.MAX_VALUE};

    public static void main(String[] args) {
        for (int a : INTS) {
            for (int b : INTS) {
                System.out.println(a + " " + b + ": " + a / b + " " + a % b + " "
                        + Integer.divideUnsigned(a, b) + " " + Integer.remainderUnsigned(a, b));
            }
            System.out.println(a + ": " + (a / -1) + " " + (a % -1) + " " + (a / 3) + " " + (a % 3));
        }
        for (long a : LONGS) {
            for (long b : LONGS) {
                System.out.println(a + " " + b + ": " + a / b + " " + a % b + " "
                        + Long.divideUnsigned(a, b) + " " + Long.remainderUnsigned(a, b));
            }
            System.out.println(a + ": " + (a / -1L) + " " + (a % -1L) + " " + (a / 3L) + " " + (a % 3L));
        }
    }
}
