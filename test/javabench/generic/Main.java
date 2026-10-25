// Generic containers over boxed values.
//
// Erasure makes every T an Object: a Pair<Integer, Long> holds two
// references, each get is a cast, and each arithmetic step unboxes and boxes
// again.  Integer.valueOf's cache covers -128..127, so most of the boxes are
// new objects.
import java.util.ArrayList;
import java.util.List;
import java.util.function.BinaryOperator;

public class Main {
    static final class Pair<A, B> {
        final A first;
        final B second;

        Pair(A first, B second) {
            this.first = first;
            this.second = second;
        }
    }

    static <T> T fold(List<T> xs, T zero, BinaryOperator<T> f) {
        T acc = zero;
        for (T x : xs) {
            acc = f.apply(acc, x);
        }
        return acc;
    }

    public static void main(String[] args) {
        List<Pair<Integer, Long>> ps = new ArrayList<>();
        for (int i = 0; i < 2000; i++) {
            ps.add(new Pair<>(i, (long) i * i));
        }
        long total = 0;
        for (int round = 0; round < 800; round++) {
            List<Integer> firsts = new ArrayList<>(ps.size());
            for (Pair<Integer, Long> p : ps) {
                firsts.add(p.first + round);
            }
            total += fold(firsts, 0, Integer::sum);
            total += fold(ps, new Pair<>(0, 0L), (u, v) -> new Pair<>(u.first ^ v.first, u.second + v.second)).second;
        }
        System.out.println("total = " + total);
    }
}
