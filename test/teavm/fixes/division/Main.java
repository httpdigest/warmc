// Java's integer division where wasm's traps: MIN_VALUE / -1 is MIN_VALUE,
// and a zero divisor throws ArithmeticException (test/teavm/patches/
// integer-division.patch).  Built with --strict, where TeaVM makes it one;
// division_nonzero is the same without a zero divisor and without --strict.
// The operands come from arrays, so that TeaVM cannot fold them, or from
// literals, its constant divisors.
public class Main {
    static final int[] INTS = {Integer.MIN_VALUE, Integer.MIN_VALUE + 1, -7, -2, -1, 0, 1, 2, 7,
        Integer.MAX_VALUE};
    static final long[] LONGS = {Long.MIN_VALUE, Long.MIN_VALUE + 1, Integer.MIN_VALUE, -7, -1, 0, 1, 7,
        1L << 40, Long.MAX_VALUE};

    static String quotient(int a, int b) {
        try {
            return Integer.toString(a / b);
        } catch (ArithmeticException e) {
            return e.getClass().getName() + ": " + e.getMessage();
        }
    }

    static String remainder(int a, int b) {
        try {
            return Integer.toString(a % b);
        } catch (ArithmeticException e) {
            return e.getClass().getName() + ": " + e.getMessage();
        }
    }

    static String quotient(long a, long b) {
        try {
            return Long.toString(a / b);
        } catch (ArithmeticException e) {
            return e.getClass().getName() + ": " + e.getMessage();
        }
    }

    static String remainder(long a, long b) {
        try {
            return Long.toString(a % b);
        } catch (ArithmeticException e) {
            return e.getClass().getName() + ": " + e.getMessage();
        }
    }

    // each in a try of its own, for what each says it may throw
    static String unsigned(int a, int b) {
        String quotient;
        try {
            quotient = Integer.toString(Integer.divideUnsigned(a, b));
        } catch (ArithmeticException e) {
            quotient = e.getClass().getName() + ": " + e.getMessage();
        }
        try {
            return quotient + " " + Integer.remainderUnsigned(a, b);
        } catch (ArithmeticException e) {
            return quotient + " " + e.getClass().getName() + ": " + e.getMessage();
        }
    }

    static String unsigned(long a, long b) {
        String quotient;
        try {
            quotient = Long.toString(Long.divideUnsigned(a, b));
        } catch (ArithmeticException e) {
            quotient = e.getClass().getName() + ": " + e.getMessage();
        }
        try {
            return quotient + " " + Long.remainderUnsigned(a, b);
        } catch (ArithmeticException e) {
            return quotient + " " + e.getClass().getName() + ": " + e.getMessage();
        }
    }

    static int order;

    static int operand(String what, int v) {
        System.out.println("  " + what + " evaluated " + ++order);
        return v;
    }

    static int zeroDivisor(int a) {
        try {
            return a / 0;
        } catch (ArithmeticException e) {
            System.out.println("  / 0: " + e.getMessage());
            return -1;
        }
    }

    static long zeroDivisor(long a) {
        try {
            return a % 0L;
        } catch (ArithmeticException e) {
            System.out.println("  % 0L: " + e.getMessage());
            return -1;
        }
    }

    public static void main(String[] args) {
        for (int a : INTS) {
            for (int b : INTS) {
                System.out.println(a + " " + b + ": " + quotient(a, b) + ", " + remainder(a, b)
                        + ", " + unsigned(a, b));
            }
        }
        for (long a : LONGS) {
            for (long b : LONGS) {
                System.out.println(a + " " + b + ": " + quotient(a, b) + ", " + remainder(a, b)
                        + ", " + unsigned(a, b));
            }
        }

        System.out.println("constant divisors");
        for (int a : INTS) {
            System.out.println(a + ": " + (a / -1) + " " + (a % -1) + " " + (a / 3) + " " + (a % 3) + " "
                    + Integer.divideUnsigned(a, 3) + " " + zeroDivisor(a));
        }
        for (long a : LONGS) {
            System.out.println(a + ": " + (a / -1L) + " " + (a % -1L) + " " + (a / 3L) + " "
                    + Long.remainderUnsigned(a, 3L) + " " + zeroDivisor(a));
        }
        try {
            System.out.println(Integer.divideUnsigned(INTS[3], 0));
        } catch (ArithmeticException e) {
            System.out.println("divideUnsigned by 0: " + e.getMessage());
        }

        // both operands are evaluated before the divisor is found to be zero
        System.out.println("evaluation order");
        try {
            System.out.println(operand("dividend", INTS[6]) / operand("divisor", INTS[5]));
        } catch (ArithmeticException e) {
            System.out.println("  then " + e.getMessage());
        }
        // a quotient used further, in a loop
        int sum = 0;
        for (int i = 1; i < INTS.length; i++) {
            int d = INTS[i - 1];
            if (d != 0) {
                sum += INTS[i] / d + INTS[i] % d;
            }
        }
        System.out.println("sum " + sum);
    }
}
