// Allocation pressure with checks: linked lists rebuilt and walked, trees of
// varying depth, arrays of arrays, maps that grow and are cleared, objects
// kept alive across many collections and verified at the end, and weak
// references, which the WASI adapter keeps strong.
//
// Everything here is wasm GC objects the engine collects; a wrong root or a
// moved object read through a stale reference shows as a wrong sum.
import java.lang.ref.WeakReference;
import java.util.*;

public class Main {
    static final class Cell {
        final int v;
        Cell next;

        Cell(int v, Cell next) {
            this.v = v;
            this.next = next;
        }
    }

    static final class Tree {
        final Tree l, r;
        final long v;

        Tree(int d, long v) {
            this.v = v;
            l = d > 0 ? new Tree(d - 1, v * 2) : null;
            r = d > 0 ? new Tree(d - 1, v * 2 + 1) : null;
        }

        long sum() {
            return v + (l == null ? 0 : l.sum()) + (r == null ? 0 : r.sum());
        }
    }

    public static void main(String[] args) {
        System.out.println("== lists ==");
        Cell keep = null;
        long total = 0;
        for (int round = 0; round < 200; round++) {
            Cell head = null;
            for (int i = 0; i < 2000; i++) {
                head = new Cell(i ^ round, head);
            }
            long s = 0;
            for (Cell c = head; c != null; c = c.next) {
                s += c.v;
            }
            total += s;
            if (round % 50 == 0) {
                Cell k = new Cell(round, keep);
                keep = k;
            }
        }
        StringBuilder kept = new StringBuilder();
        for (Cell c = keep; c != null; c = c.next) {
            kept.append(c.v).append(' ');
        }
        System.out.println(total + " kept " + kept.toString().trim());

        System.out.println("== trees ==");
        Tree survivor = new Tree(10, 1);
        long ts = 0;
        for (int d = 4; d < 16; d++) {
            ts += new Tree(d, d).sum();
        }
        System.out.println(ts + " " + survivor.sum());

        System.out.println("== arrays and maps ==");
        long[][] rows = new long[64][];
        long as = 0;
        for (int round = 0; round < 50; round++) {
            for (int i = 0; i < rows.length; i++) {
                rows[i] = new long[100 + (i * round) % 300];
                rows[i][rows[i].length - 1] = i + round;
            }
            for (long[] row : rows) {
                as += row[row.length - 1] + row.length;
            }
        }
        Map<Integer, int[]> m = new HashMap<>();
        long ms = 0;
        for (int round = 0; round < 20; round++) {
            for (int i = 0; i < 3000; i++) {
                m.put(i, new int[] {i, round});
            }
            for (int i = 0; i < 3000; i += 7) {
                ms += m.get(i)[0] + m.get(i)[1];
            }
            if (round % 5 == 4) {
                m.clear();
            }
        }
        System.out.println(as + " " + ms + " " + m.size());

        System.out.println("== strings ==");
        List<String> strs = new ArrayList<>();
        for (int i = 0; i < 20000; i++) {
            String s = "item-" + i + "-" + (i * 31 % 97);
            if (i % 1000 == 0) {
                strs.add(s);
            }
        }
        System.out.println(strs.size() + " " + strs.get(7) + " " + String.join(",", strs).length());

        System.out.println("== weak references ==");
        Object strong = new int[] {42};
        WeakReference<Object> wr = new WeakReference<>(strong);
        for (int i = 0; i < 100000; i++) {
            Object garbage = new long[8];
        }
        System.out.println(wr.get() == strong ? "still there" : "gone");
        WeakReference<Cell> wc = new WeakReference<>(new Cell(5, null));
        Cell c = wc.get();
        System.out.println(c == null ? "collected" : "present " + c.v);
    }
}
