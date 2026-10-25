// Boxing: autoboxing and unboxing in arithmetic, collections and
// conditionals, the Integer cache's identity at its edges, valueOf versus
// new values, equals across box types, compare and compareTo, unboxing null,
// Character and Boolean boxes, numeric conversions between boxes, and boxes
// as map keys.
import java.util.*;

public class Main {
    public static void main(String[] args) {
        System.out.println("== identity ==");
        Integer a = 127, b = 127, c = 128, d = 128, e = -128, f = -128, g = -129, h = -129;
        System.out.println((a == b) + " " + (c == d) + " " + (e == f) + " " + (g == h) + " " + c.equals(d) + " " + (c.intValue() == d));
        Long la = 127L, lb = 127L, lc = 1000L, ld = 1000L;
        System.out.println((la == lb) + " " + (lc == ld) + " " + lc.equals(ld) + " " + lc.equals(1000) + " " + Long.valueOf(5).equals(5L));
        Character ca = 'a', cb = 'a';
        Boolean t1 = true, t2 = Boolean.valueOf("TRUE");
        System.out.println((ca == cb) + " " + (t1 == t2) + " " + Boolean.TRUE.equals(t2) + " " + Character.valueOf('z').compareTo('a'));

        System.out.println("== arithmetic ==");
        Integer x = 10;
        x++;
        x += 5;
        Double dx = 2.5;
        dx *= x;
        Long big = 1L << 40;
        System.out.println(x + " " + dx + " " + (big + x) + " " + (x / 4) + " " + (x % 4) + " " + (x > 15 ? "gt" : "le"));
        List<Integer> nums = new ArrayList<>();
        for (int i = 0; i < 10; i++) {
            nums.add(i * i);
        }
        int sum = 0;
        for (Integer n : nums) {
            sum += n;
        }
        nums.remove(Integer.valueOf(49));
        nums.remove(0);
        System.out.println(sum + " " + nums + " " + nums.contains(81) + " " + nums.indexOf(16));

        System.out.println("== conversions ==");
        Number n = 3.75;
        Object o = 42;
        System.out.println(n.intValue() + " " + n.longValue() + " " + n.floatValue() + " " + ((Integer) o).doubleValue() + " "
                + Integer.valueOf(65).byteValue() + " " + Integer.valueOf(300).byteValue() + " " + Double.valueOf(1e20).longValue()
                + " " + Short.valueOf((short) -1).intValue() + " " + Byte.toUnsignedInt((byte) -1));
        System.out.println(Integer.compare(3, 7) + " " + Integer.valueOf(9).compareTo(2) + " " + Double.compare(0.0, -0.0) + " "
                + Long.signum(-99) + " " + Integer.max(4, 9) + " " + Integer.sum(4, 9) + " " + Double.sum(0.5, 0.25)
                + " " + Character.compare('b', 'a') + " " + Boolean.compare(true, false));

        System.out.println("== null unboxing ==");
        Integer nothing = null;
        try {
            int bad = nothing;
            System.out.println(bad);
        } catch (NullPointerException ex) {
            System.out.println("NullPointerException");
        }
        Map<String, Integer> counts = new HashMap<>();
        try {
            int v = counts.get("missing");
            System.out.println(v);
        } catch (NullPointerException ex) {
            System.out.println("NullPointerException from a map");
        }

        System.out.println("== as keys ==");
        Map<Object, String> mixed = new HashMap<>();
        mixed.put(1, "int");
        mixed.put(1L, "long");
        mixed.put(1.0, "double");
        mixed.put('1', "char");
        mixed.put("1", "string");
        System.out.println(mixed.size() + " " + mixed.get(1) + " " + mixed.get(1L) + " " + mixed.get(1.0) + " " + mixed.get('1')
                + " " + mixed.get((short) 1));
        Set<Integer> s = new TreeSet<>(Arrays.asList(5, 3, 5, 1, 3));
        System.out.println(s + " " + Integer.valueOf(1000).hashCode() + " " + Long.valueOf(1L << 35).hashCode() + " "
                + Double.valueOf(1.5).hashCode() + " " + Boolean.FALSE.hashCode() + " " + Character.valueOf('A').hashCode());
    }
}
