// Plain recursion: Fibonacci, Ackermann and a recursive tree count.
//
// Static calls TeaVM leaves as direct calls, deep enough that the call and
// return sequence is the cost.
public class Main {
    static int fib(int n) {
        return n < 2 ? n : fib(n - 1) + fib(n - 2);
    }

    static int ack(int m, int n) {
        if (m == 0) {
            return n + 1;
        }
        if (n == 0) {
            return ack(m - 1, 1);
        }
        return ack(m - 1, ack(m, n - 1));
    }

    static long paths(int x, int y) {
        if (x == 0 || y == 0) {
            return 1;
        }
        return paths(x - 1, y) + paths(x, y - 1);
    }

    public static void main(String[] args) {
        long s = fib(34);
        s += ack(2, 2000) + ack(3, 7);
        s += paths(14, 13);
        System.out.println("s = " + s);
    }
}
