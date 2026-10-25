// Records as hash keys: equals and hashCode the compiler generates.
//
// javac writes a record's equals, hashCode and toString as an invokedynamic of
// ObjectMethods, which TeaVM expands into ordinary field-by-field code; a
// HashMap of them then spends its time in those and in its own buckets.
import java.util.HashMap;

public class Main {
    record Key(int a, int b, String tag) {
    }

    public static void main(String[] args) {
        String[] tags = {"red", "green", "blue", "cyan"};
        HashMap<Key, Integer> m = new HashMap<>();
        long sum = 0;
        for (int round = 0; round < 36; round++) {
            for (int i = 0; i < 20000; i++) {
                Key k = new Key(i % 997, i % 13, tags[i & 3]);
                m.merge(k, 1, Integer::sum);
            }
            for (int i = 0; i < 20000; i += 3) {
                Integer v = m.get(new Key(i % 997, i % 13, tags[i & 3]));
                if (v != null) {
                    sum += v;
                }
            }
        }
        // not a record's hashCode, whose algorithm Java leaves unspecified: the
        // JDK's and TeaVM's differ
        System.out.println("size = " + m.size() + " sum = " + sum);
    }
}
