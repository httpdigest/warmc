// The sieve of Eratosthenes over boolean[] and over BitSet.
//
// A boolean[] is a wasm array of i8, a BitSet a long[] the class library
// indexes by shifts and masks.
import java.util.BitSet;

public class Main {
    public static void main(String[] args) {
        int n = 2_000_000;
        long total = 0;
        for (int round = 0; round < 6; round++) {
            boolean[] composite = new boolean[n + 1];
            int count = 0;
            for (int i = 2; i <= n; i++) {
                if (!composite[i]) {
                    count++;
                    for (long j = (long) i * i; j <= n; j += i) {
                        composite[(int) j] = true;
                    }
                }
            }
            BitSet bs = new BitSet(n / 2);
            bs.set(2, n / 2);
            for (int i = 2; (long) i * i < n / 2; i = bs.nextSetBit(i + 1)) {
                for (int j = i * i; j < n / 2; j += i) {
                    bs.clear(j);
                }
            }
            total += count + bs.cardinality() * 3L + round;
        }
        System.out.println("total = " + total);
    }
}
