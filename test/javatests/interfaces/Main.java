// Interfaces: default methods and their resolution through superinterfaces,
// a class overriding a default and calling it with Iface.super, static and
// private interface methods, constants, functional interfaces implemented by
// lambdas, method references and classes, generic interfaces with bridge
// methods, diamond inheritance, and an interface call at a site that sees
// many classes.
import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

public class Main {
    interface Named {
        String PREFIX = "<";

        String name();

        default String tag() {
            return PREFIX + name() + suffix();
        }

        private String suffix() {
            return ">";
        }

        static Named of(String n) {
            return () -> n;
        }
    }

    interface Loud extends Named {
        @Override
        default String tag() {
            return Named.super.tag().toUpperCase();
        }
    }

    interface Quiet extends Named {
        @Override
        default String tag() {
            return Named.super.tag().toLowerCase();
        }
    }

    static class Both implements Loud, Quiet {
        public String name() {
            return "Both";
        }

        @Override
        public String tag() {
            return Loud.super.tag() + "/" + Quiet.super.tag();
        }
    }

    interface Shape extends Comparable<Shape> {
        double area();

        default int compareTo(Shape o) {
            return Double.compare(area(), o.area());
        }
    }

    record Sq(double s) implements Shape {
        public double area() {
            return s * s;
        }
    }

    record Circ(double r) implements Shape {
        public double area() {
            return 3 * r * r;
        }
    }

    interface Transformer<T, R> {
        R apply(T t);

        default <V> Transformer<T, V> then(Transformer<? super R, ? extends V> next) {
            return t -> next.apply(apply(t));
        }
    }

    static class Length implements Transformer<String, Integer> {
        public Integer apply(String s) {
            return s.length();
        }
    }

    interface Visitor<R> {
        R num(int n);

        R add(Node a, Node b);
    }

    interface Node {
        <R> R accept(Visitor<R> v);
    }

    static Node num(int n) {
        return new Node() {
            public <R> R accept(Visitor<R> v) {
                return v.num(n);
            }
        };
    }

    static Node add(Node a, Node b) {
        return new Node() {
            public <R> R accept(Visitor<R> v) {
                return v.add(a, b);
            }
        };
    }

    public static void main(String[] args) {
        System.out.println("== defaults ==");
        Named n = Named.of("plain");
        Loud l = () -> "loud";
        Quiet q = () -> "QUIET";
        System.out.println(n.tag() + " " + l.tag() + " " + q.tag() + " " + new Both().tag() + " " + Named.PREFIX);

        System.out.println("== generic interfaces ==");
        List<Shape> shapes = new ArrayList<>(List.of(new Sq(3), new Circ(1), new Sq(1.5), new Circ(2)));
        shapes.sort(null);
        System.out.println(shapes);
        Comparable<Shape> c = new Sq(2);
        System.out.println(c.compareTo(new Circ(1)) + " " + c.compareTo(new Sq(2)));
        Transformer<String, Integer> len = new Length();
        Transformer<String, String> pipeline = len.then(i -> i * i).then(i -> "#" + i);
        System.out.println(pipeline.apply("hello") + " " + len.then(Integer::toBinaryString).apply("abcdef"));
        Function<Integer, Integer> f = Math::abs;
        System.out.println(f.andThen(x -> x + 1).compose((Integer x) -> x * -3).apply(5));

        System.out.println("== visitors ==");
        Node tree = add(add(num(1), num(2)), add(num(3), add(num(4), num(5))));
        int sum = tree.accept(new Visitor<Integer>() {
            public Integer num(int k) {
                return k;
            }

            public Integer add(Node a, Node b) {
                return a.accept(this) + b.accept(this);
            }
        });
        String shown = tree.accept(new Visitor<String>() {
            public String num(int k) {
                return Integer.toString(k);
            }

            public String add(Node a, Node b) {
                return "(" + a.accept(this) + "+" + b.accept(this) + ")";
            }
        });
        System.out.println(shown + " = " + sum);

        System.out.println("== megamorphic ==");
        List<Named> many = new ArrayList<>();
        for (int i = 0; i < 12; i++) {
            final int k = i;
            many.add(switch (i % 4) {
                case 0 -> Named.of("n" + k);
                case 1 -> (Loud) () -> "l" + k;
                case 2 -> (Quiet) () -> "Q" + k;
                default -> new Both();
            });
        }
        StringBuilder sb = new StringBuilder();
        for (Named m : many) {
            sb.append(m.tag()).append(' ');
        }
        System.out.println(sb.toString().trim());
    }
}
