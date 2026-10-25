// Breadth-first search over a grid with ArrayDeque, boxed Integers in it.
//
// An ArrayDeque<Integer> holds references, so every offer boxes the cell
// index (Integer.valueOf, with its cache of small values) and every poll
// unboxes it: the boxing traffic is most of the work.
import java.util.ArrayDeque;

public class Main {
    public static void main(String[] args) {
        int n = 300;
        boolean[] wall = new boolean[n * n];
        int seed = 12345;
        for (int i = 0; i < wall.length; i++) {
            seed = seed * 1103515245 + 12345;
            wall[i] = ((seed >>> 16) & 7) == 0;
        }
        wall[0] = false;
        long total = 0;
        for (int round = 0; round < 36; round++) {
            int[] dist = new int[n * n];
            java.util.Arrays.fill(dist, -1);
            ArrayDeque<Integer> q = new ArrayDeque<>();
            int start = (round * 7919) % (n * n);
            if (wall[start]) {
                start = 0;
            }
            dist[start] = 0;
            q.add(start);
            while (!q.isEmpty()) {
                int c = q.poll();
                int x = c % n, y = c / n;
                int[] nb = {x > 0 ? c - 1 : -1, x < n - 1 ? c + 1 : -1, y > 0 ? c - n : -1, y < n - 1 ? c + n : -1};
                for (int d : nb) {
                    if (d >= 0 && !wall[d] && dist[d] < 0) {
                        dist[d] = dist[c] + 1;
                        q.add(d);
                    }
                }
            }
            for (int d : dist) {
                total += d;
            }
        }
        System.out.println("total = " + total);
    }
}
