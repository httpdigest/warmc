// Word counting in a HashMap<String, Integer>.
//
// String.hashCode walks the characters once and caches the result in the
// string, equals compares the char arrays, and the counts are boxed
// Integers replaced on every increment.
import java.util.HashMap;
import java.util.Map;

public class Main {
    public static void main(String[] args) {
        String[] syll = {"ka", "lo", "mi", "ne", "su", "ta", "ri", "po", "an", "el", "or", "uz"};
        String[] words = new String[5000];
        int seed = 7;
        for (int i = 0; i < words.length; i++) {
            StringBuilder sb = new StringBuilder();
            int n = 1 + (i % 4);
            for (int j = 0; j < n; j++) {
                seed = seed * 1103515245 + 12345;
                sb.append(syll[(seed >>> 16) % syll.length]);
            }
            words[i] = sb.toString();
        }
        HashMap<String, Integer> counts = new HashMap<>();
        for (int round = 0; round < 300; round++) {
            for (String w : words) {
                counts.merge(w, 1, Integer::sum);
            }
        }
        long h = 0;
        for (Map.Entry<String, Integer> e : counts.entrySet()) {
            h += (long) e.getKey().hashCode() * e.getValue();
        }
        System.out.println("distinct = " + counts.size() + " h = " + h);
    }
}
