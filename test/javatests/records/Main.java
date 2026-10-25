// Records: canonical and compact constructors with validation, accessors,
// generated equals, hashCode and toString, records as map keys and set
// members, nested and generic records, static members, records implementing
// interfaces, and record patterns in instanceof and switch.
import java.util.*;

public class Main {
    record Point(int x, int y) {
        static final Point ORIGIN = new Point(0, 0);

        Point {
            if (x < -1000) {
                throw new IllegalArgumentException("x too small: " + x);
            }
        }

        Point(int v) {
            this(v, v);
        }

        double dist(Point o) {
            return Math.sqrt((double) (x - o.x) * (x - o.x) + (double) (y - o.y) * (y - o.y));
        }

        Point plus(Point o) {
            return new Point(x + o.x, y + o.y);
        }
    }

    record Range(int lo, int hi) implements Comparable<Range> {
        Range {
            if (lo > hi) {
                int t = lo;
                lo = hi;
                hi = t;
            }
        }

        int length() {
            return hi - lo;
        }

        public int compareTo(Range o) {
            return lo != o.lo ? Integer.compare(lo, o.lo) : Integer.compare(hi, o.hi);
        }
    }

    record Pair<A, B>(A first, B second) {
    }

    record Line(Point from, Point to) {
    }

    record Named(String name, double[] values, List<String> tags) {
    }

    static String describe(Object o) {
        if (o instanceof Line(Point(var x1, var y1), Point p2) && x1 == 0) {
            return "line from the y axis at " + y1 + " to " + p2;
        }
        return switch (o) {
            case Point(int x, int y) when x == y -> "diagonal point " + x;
            case Point p -> "point " + p.x() + "," + p.y();
            case Pair<?, ?>(String s, Integer i) -> "string-int pair " + s + i;
            case Pair<?, ?> p -> "pair of " + p.first() + " and " + p.second();
            default -> "something else";
        };
    }

    public static void main(String[] args) {
        System.out.println("== basics ==");
        Point p = new Point(3, 4), q = new Point(3, 4), r = new Point(7);
        System.out.println(p + " " + p.x() + " " + p.y() + " " + p.equals(q) + " " + (p == q) + " " + (p.hashCode() == q.hashCode()));
        System.out.println(r + " " + p.dist(Point.ORIGIN) + " " + p.plus(r) + " " + p.equals(r) + " " + p.equals(null));
        try {
            new Point(-2000, 0);
        } catch (IllegalArgumentException e) {
            System.out.println("rejected: " + e.getMessage());
        }
        Range rg = new Range(9, 2);
        System.out.println(rg + " length " + rg.length());

        System.out.println("== as keys ==");
        Map<Point, String> names = new HashMap<>();
        names.put(new Point(1, 2), "a");
        names.put(new Point(1, 2), "b");
        names.put(new Point(2, 1), "c");
        System.out.println(names.size() + " " + names.get(new Point(1, 2)) + " " + names.containsKey(new Point(2, 1)));
        TreeSet<Range> ranges = new TreeSet<>(List.of(new Range(5, 9), new Range(1, 3), new Range(5, 6), new Range(3, 1)));
        System.out.println(ranges);

        System.out.println("== generic and nested ==");
        Pair<String, Integer> pi = new Pair<>("n", 42);
        Pair<Pair<String, Integer>, List<Integer>> nested = new Pair<>(pi, List.of(1, 2));
        System.out.println(pi + " " + nested + " " + nested.first().second());
        Named nm = new Named("vec", new double[] {1, 2}, List.of("x"));
        Named nm2 = new Named("vec", new double[] {1, 2}, List.of("x"));
        System.out.println(nm.name() + " " + nm.tags() + " arrays compare by identity: " + nm.equals(nm2));

        System.out.println("== patterns ==");
        Object[] things = {new Point(5, 5), new Point(1, 2), new Pair<>("s", 3), new Pair<>(1.5, 'c'),
            new Line(new Point(0, 4), new Point(9, 9)), new Line(new Point(1, 1), new Point(2, 2)), "text"};
        for (Object o : things) {
            System.out.println(describe(o));
        }
    }
}
