// Exceptions thrown and caught on a hot path.
//
// TeaVM throws with wasm's `throw` on its one tag and catches with try_table,
// testing the exception's class after the catch.  Constructing a Throwable
// fills in its stack trace, two host calls under test/teavm/wasi.py, which
// keeps none.  One in eight steps throws, from two calls down.
public class Main {
    static final class Odd extends RuntimeException {
        final int value;

        Odd(int value) {
            this.value = value;
        }
    }

    static int check(int v) {
        if ((v & 7) == 7) {
            throw new Odd(v);
        }
        return v >> 1;
    }

    static int step(int v) {
        return check(v * 5 + 3) + 1;
    }

    public static void main(String[] args) {
        long sum = 0;
        int caught = 0;
        for (int i = 0; i < 6_000_000; i++) {
            try {
                sum += step(i);
            } catch (Odd e) {
                caught++;
                sum -= e.value;
            }
        }
        System.out.println("sum = " + sum + " caught = " + caught);
    }
}
