// Recursion: deep linear recursion, mutual recursion, recursion with
// memoisation in a HashMap and in an array, backtracking (N queens,
// permutations, subset sums), divide and conquer (merge sort, fast power),
// and the Ackermann function.
import java.util.*;

public class Main {
    static long sumTo(int n) {
        return n == 0 ? 0 : n + sumTo(n - 1);
    }

    static boolean isEven(int n) {
        return n == 0 || isOdd(n - 1);
    }

    static boolean isOdd(int n) {
        return n != 0 && isEven(n - 1);
    }

    static final Map<Integer, Long> memo = new HashMap<>();

    static long fib(int n) {
        if (n < 2) {
            return n;
        }
        Long c = memo.get(n);
        if (c != null) {
            return c;
        }
        long v = fib(n - 1) + fib(n - 2);
        memo.put(n, v);
        return v;
    }

    static long[] partitions = new long[101];

    static long partition(int n, int max) {
        if (n == 0) {
            return 1;
        }
        long r = 0;
        for (int k = Math.min(n, max); k >= 1; k--) {
            r += partition(n - k, k);
        }
        return r;
    }

    static int queens(int n, int row, int cols, int d1, int d2) {
        if (row == n) {
            return 1;
        }
        int count = 0;
        for (int c = 0; c < n; c++) {
            if ((cols >> c & 1) == 0 && (d1 >> (row + c) & 1) == 0 && (d2 >> (row - c + n) & 1) == 0) {
                count += queens(n, row + 1, cols | 1 << c, d1 | 1 << (row + c), d2 | 1 << (row - c + n));
            }
        }
        return count;
    }

    static void permute(char[] cs, int k, List<String> out) {
        if (k == cs.length) {
            out.add(new String(cs));
            return;
        }
        for (int i = k; i < cs.length; i++) {
            char t = cs[k];
            cs[k] = cs[i];
            cs[i] = t;
            permute(cs, k + 1, out);
            cs[i] = cs[k];
            cs[k] = t;
        }
    }

    static int subsets(int[] xs, int i, int target) {
        if (target == 0) {
            return 1;
        }
        if (i == xs.length || target < 0) {
            return 0;
        }
        return subsets(xs, i + 1, target - xs[i]) + subsets(xs, i + 1, target);
    }

    static long power(long b, int e, long mod) {
        if (e == 0) {
            return 1;
        }
        long h = power(b, e / 2, mod);
        h = h * h % mod;
        return e % 2 == 1 ? h * b % mod : h;
    }

    static int[] mergeSort(int[] a) {
        if (a.length < 2) {
            return a;
        }
        int[] l = mergeSort(Arrays.copyOfRange(a, 0, a.length / 2));
        int[] r = mergeSort(Arrays.copyOfRange(a, a.length / 2, a.length));
        int[] out = new int[a.length];
        for (int i = 0, j = 0, k = 0; k < out.length; k++) {
            out[k] = j >= r.length || (i < l.length && l[i] <= r[j]) ? l[i++] : r[j++];
        }
        return out;
    }

    static int ack(int m, int n) {
        return m == 0 ? n + 1 : n == 0 ? ack(m - 1, 1) : ack(m - 1, ack(m, n - 1));
    }

    public static void main(String[] args) {
        System.out.println(sumTo(5000) + " " + isEven(1001) + " " + isOdd(777));
        System.out.println(fib(90) + " " + memo.size() + " " + partition(40, 40) + " " + partition(60, 60));
        for (int n = 1; n <= 9; n++) {
            System.out.print(queens(n, 0, 0, 0, 0) + " ");
        }
        System.out.println();
        List<String> perms = new ArrayList<>();
        permute("abcd".toCharArray(), 0, perms);
        Collections.sort(perms);
        System.out.println(perms.size() + " " + perms.subList(0, 6) + " " + perms.get(23));
        System.out.println(subsets(new int[] {3, 34, 4, 12, 5, 2, 7, 9, 11, 1}, 0, 20) + " " + power(3, 1000, 1_000_000_007L)
                + " " + power(2, 62, Long.MAX_VALUE));
        System.out.println(Arrays.toString(mergeSort(new int[] {9, -3, 5, 0, 12, 7, -8, 5, 1})) + " " + ack(2, 3) + " " + ack(3, 5));
    }
}
