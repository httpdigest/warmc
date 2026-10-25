// TreeMap<Integer, Integer>: a red-black tree of boxed keys.
//
// Every comparison unboxes two Integers through compareTo, every insert may
// rotate, and the nodes are objects chained by reference.
import java.util.Map;
import java.util.TreeMap;

public class Main {
    public static void main(String[] args) {
        long check = 0;
        for (int round = 0; round < 4; round++) {
            TreeMap<Integer, Integer> m = new TreeMap<>();
            int seed = round + 1;
            for (int i = 0; i < 40000; i++) {
                seed = seed * 1103515245 + 12345;
                int k = (seed >>> 8) % 50000;
                m.put(k, m.getOrDefault(k, 0) + i);
            }
            for (int i = 0; i < 50000; i += 3) {
                m.remove(i);
            }
            Map.Entry<Integer, Integer> e = m.ceilingEntry(25000);
            check = check * 31 + m.size() + (e == null ? 0 : e.getKey()) + m.firstKey() + m.lastKey();
            for (int v : m.headMap(1000).values()) {
                check += v & 0xff;
            }
        }
        System.out.println("check = " + check);
    }
}
