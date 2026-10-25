// ArrayList and LinkedList: append, index, iterate, remove.
//
// The element type is Integer, so the lists hold references to boxes; an
// ArrayList's get is a bounds check against its size and then one against
// the backing array, and a LinkedList's iteration a chase through node
// objects.
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedList;

public class Main {
    public static void main(String[] args) {
        long sum = 0;
        for (int round = 0; round < 8; round++) {
            ArrayList<Integer> a = new ArrayList<>();
            for (int i = 0; i < 20000; i++) {
                a.add(i * 3 + round);
            }
            for (int i = 0; i < a.size(); i += 7) {
                sum += a.get(i);
            }
            LinkedList<Integer> l = new LinkedList<>(a.subList(0, 5000));
            for (Iterator<Integer> it = l.iterator(); it.hasNext(); ) {
                if ((it.next() & 3) == 0) {
                    it.remove();
                }
            }
            for (int v : l) {
                sum += v & 0xff;
            }
            a.removeIf(v -> v % 5 == 0);
            sum += a.size();
        }
        System.out.println("sum = " + sum);
    }
}
