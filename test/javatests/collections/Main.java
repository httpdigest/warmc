// Collections: ArrayList, LinkedList, ArrayDeque, HashMap, LinkedHashMap,
// TreeMap with navigation, HashSet, TreeSet, PriorityQueue, EnumMap,
// Collections' sort, reverse, shuffle with a seed, min and max, frequency,
// nCopies, unmodifiable views, List.of and Map.of, iterators with remove,
// subList views, ConcurrentModificationException, and computeIfAbsent,
// merge and getOrDefault.  Unordered collections are printed sorted.
import java.util.*;

public class Main {
    enum Color { RED, GREEN, BLUE }

    static <T extends Comparable<T>> List<T> sorted(Collection<T> c) {
        List<T> l = new ArrayList<>(c);
        Collections.sort(l);
        return l;
    }

    public static void main(String[] args) {
        System.out.println("== lists ==");
        List<Integer> a = new ArrayList<>();
        for (int i = 0; i < 12; i++) {
            a.add(i * i % 17);
        }
        System.out.println(a + " size " + a.size());
        a.remove(Integer.valueOf(16));
        a.remove(0);
        a.add(3, -1);
        a.set(5, 100);
        System.out.println(a + " contains 9 " + a.contains(9) + " indexOf 13 " + a.indexOf(13));
        List<Integer> sub = a.subList(2, 6);
        sub.set(0, 55);
        System.out.println(sub + " " + a);
        Collections.sort(a);
        System.out.println("sorted " + a + " min " + Collections.min(a) + " max " + Collections.max(a));
        Collections.reverse(a);
        System.out.println("reversed " + a);
        Collections.shuffle(a, new Random(7));
        System.out.println("shuffled " + a.size() + " " + sorted(a).equals(sorted(new ArrayList<>(a))));
        LinkedList<String> ll = new LinkedList<>(List.of("b", "c"));
        ll.addFirst("a");
        ll.addLast("d");
        ll.add(2, "x");
        System.out.println(ll + " " + ll.getFirst() + ll.getLast() + " " + ll.removeFirst() + ll.pollLast() + " " + ll);
        Iterator<String> it = ll.iterator();
        while (it.hasNext()) {
            if (it.next().equals("x")) {
                it.remove();
            }
        }
        System.out.println(ll);
        try {
            for (Integer v : a) {
                if (v > 50) {
                    a.remove(v);
                }
            }
            System.out.println("no CME");
        } catch (ConcurrentModificationException e) {
            System.out.println("ConcurrentModificationException");
        }
        System.out.println(Collections.nCopies(3, "ab") + " " + Collections.frequency(List.of(1, 2, 1, 1), 1));

        System.out.println("== deques and queues ==");
        ArrayDeque<Integer> dq = new ArrayDeque<>();
        for (int i = 0; i < 6; i++) {
            if (i % 2 == 0) {
                dq.offerFirst(i);
            } else {
                dq.offerLast(i);
            }
        }
        System.out.println(dq + " " + dq.peekFirst() + " " + dq.pollLast() + " " + dq.pop() + " " + dq);
        PriorityQueue<String> pq = new PriorityQueue<>(Comparator.comparing(String::length).thenComparing(Comparator.reverseOrder()));
        pq.addAll(List.of("ccc", "a", "bb", "b", "dddd", "aa"));
        StringBuilder order = new StringBuilder();
        while (!pq.isEmpty()) {
            order.append(pq.poll()).append(' ');
        }
        System.out.println(order.toString().trim());

        System.out.println("== maps ==");
        Map<String, Integer> hm = new HashMap<>();
        String text = "the cat and the hat and the bat";
        for (String w : text.split(" ")) {
            hm.merge(w, 1, Integer::sum);
        }
        System.out.println(new TreeMap<>(hm) + " " + hm.getOrDefault("dog", -1) + " " + hm.containsKey("cat"));
        Map<Integer, List<String>> byLen = new TreeMap<>();
        for (String w : text.split(" ")) {
            byLen.computeIfAbsent(w.length(), k -> new ArrayList<>()).add(w);
        }
        System.out.println(byLen);
        LinkedHashMap<String, Integer> lhm = new LinkedHashMap<>();
        for (String w : new String[] {"zeta", "alpha", "mid", "alpha"}) {
            lhm.put(w, w.length());
        }
        System.out.println(lhm + " " + lhm.keySet() + " " + lhm.values());
        TreeMap<Integer, String> tm = new TreeMap<>();
        for (int i = 0; i < 10; i++) {
            tm.put(i * 3, "v" + i);
        }
        System.out.println(tm.firstKey() + " " + tm.lastEntry() + " " + tm.floorKey(10) + " " + tm.ceilingKey(10) + " "
                + tm.headMap(9) + " " + tm.tailMap(21, false) + " " + tm.descendingMap().firstKey());
        tm.remove(12);
        tm.pollFirstEntry();
        System.out.println(tm + " " + tm.higherEntry(26) + " " + tm.lowerKey(3));
        EnumMap<Color, Integer> em = new EnumMap<>(Color.class);
        em.put(Color.BLUE, 3);
        em.put(Color.RED, 1);
        System.out.println(em + " " + EnumSet.complementOf(EnumSet.of(Color.GREEN)));
        System.out.println(Map.of("k", 1).get("k") + " " + new TreeMap<>(Map.of(2, "b", 1, "a")));

        System.out.println("== sets ==");
        Set<Integer> hs = new HashSet<>();
        for (int i = 0; i < 30; i++) {
            hs.add(i * 7 % 11);
        }
        System.out.println(sorted(hs) + " " + hs.size());
        TreeSet<String> ts = new TreeSet<>(List.of("pear", "apple", "fig", "kiwi", "date"));
        System.out.println(ts + " " + ts.first() + " " + ts.ceiling("g") + " " + ts.headSet("fig") + " " + ts.descendingSet());
        Set<Integer> s1 = new TreeSet<>(List.of(1, 2, 3, 4)), s2 = new TreeSet<>(List.of(3, 4, 5));
        Set<Integer> union = new TreeSet<>(s1), inter = new TreeSet<>(s1), diff = new TreeSet<>(s1);
        union.addAll(s2);
        inter.retainAll(s2);
        diff.removeAll(s2);
        System.out.println(union + " " + inter + " " + diff);

        System.out.println("== unmodifiable ==");
        List<Integer> ro = Collections.unmodifiableList(new ArrayList<>(List.of(1, 2)));
        try {
            ro.add(3);
        } catch (UnsupportedOperationException e) {
            System.out.println("UnsupportedOperationException " + ro);
        }
        try {
            List.of(1).set(0, 2);
        } catch (UnsupportedOperationException e) {
            System.out.println("List.of is immutable");
        }
    }
}
