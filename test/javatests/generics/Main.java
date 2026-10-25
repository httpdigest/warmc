// Generics: bounded type parameters, wildcards in both directions, generic
// methods with inferred and explicit types, a generic class hierarchy with
// bridge methods, recursive bounds, erasure and the casts it inserts, and
// heap pollution caught where the value is used.
import java.util.*;
import java.util.function.*;

public class Main {
    static class Box<T> {
        private T value;

        Box(T value) {
            this.value = value;
        }

        T get() {
            return value;
        }

        void set(T v) {
            value = v;
        }

        <R> Box<R> map(Function<? super T, ? extends R> f) {
            return new Box<>(f.apply(value));
        }

        @Override
        public String toString() {
            return "Box(" + value + ")";
        }
    }

    static class IntBox extends Box<Integer> {
        IntBox(int v) {
            super(v);
        }

        @Override
        Integer get() {
            return super.get() * 10;
        }
    }

    static <T extends Comparable<? super T>> T max(Collection<? extends T> xs) {
        Iterator<? extends T> it = xs.iterator();
        T best = it.next();
        while (it.hasNext()) {
            T x = it.next();
            if (x.compareTo(best) > 0) {
                best = x;
            }
        }
        return best;
    }

    static double sum(List<? extends Number> xs) {
        double s = 0;
        for (Number n : xs) {
            s += n.doubleValue();
        }
        return s;
    }

    static void fill(List<? super Integer> out, int n) {
        for (int i = 0; i < n; i++) {
            out.add(i);
        }
    }

    abstract static class Shape<S extends Shape<S>> implements Comparable<S> {
        abstract double size();

        public int compareTo(S o) {
            return Double.compare(size(), o.size());
        }

        @SuppressWarnings("unchecked")
        S self() {
            return (S) this;
        }
    }

    static final class Sq extends Shape<Sq> {
        final double s;

        Sq(double s) {
            this.s = s;
        }

        double size() {
            return s * s;
        }

        public String toString() {
            return "Sq" + s;
        }
    }

    static <K, V extends Comparable<V>> K argmax(Map<K, V> m) {
        K best = null;
        V bv = null;
        for (Map.Entry<K, V> e : new TreeMap<>(m).entrySet()) {
            if (bv == null || e.getValue().compareTo(bv) > 0) {
                best = e.getKey();
                bv = e.getValue();
            }
        }
        return best;
    }

    @SafeVarargs
    static <T> List<T> listOf(T... xs) {
        return new ArrayList<>(Arrays.asList(xs));
    }

    static <A, B, C> Function<A, C> compose(Function<A, B> f, Function<B, C> g) {
        return a -> g.apply(f.apply(a));
    }

    public static void main(String[] args) {
        System.out.println("== boxes ==");
        Box<String> bs = new Box<>("hello");
        Box<Integer> bl = bs.map(String::length);
        System.out.println(bs + " " + bl + " " + bl.map(n -> n * 1.5) + " " + bs.<Object>map(s -> s.charAt(0)).get());
        Box<Integer> ib = new IntBox(4);
        System.out.println(ib.get() + " " + ib);
        ib.set(7);
        System.out.println(ib.get());

        System.out.println("== bounds and wildcards ==");
        System.out.println(max(List.of(3, 9, 2)) + " " + max(Set.of("pear", "apple")) + " " + max(List.of(new Sq(2), new Sq(1))));
        System.out.println(sum(List.of(1, 2.5, 3L, (short) 4, 1.5f)));
        List<Number> nums = new ArrayList<>();
        List<Object> objs = new ArrayList<>();
        fill(nums, 3);
        fill(objs, 2);
        System.out.println(nums + " " + objs);
        System.out.println(argmax(Map.of("a", 3, "b", 7, "c", 5)) + " " + new Sq(3).self().size());
        List<Sq> sqs = listOf(new Sq(3), new Sq(1), new Sq(2));
        Collections.sort(sqs);
        System.out.println(sqs);

        System.out.println("== functions ==");
        Function<String, Integer> parse = Integer::parseInt;
        Function<String, String> roundTrip = compose(compose(parse, i -> i + 1), Object::toString);
        System.out.println(roundTrip.apply("41") + " " + compose(String::trim, String::length).apply("  ab  "));
        BiFunction<Integer, Integer, Integer> add = Integer::sum;
        Supplier<List<String>> mk = ArrayList::new;
        List<String> made = mk.get();
        made.add("x");
        System.out.println(add.andThen(x -> x * 2).apply(3, 4) + " " + made);
        UnaryOperator<String> twice = s -> s + s;
        System.out.println(twice.andThen(String::toUpperCase).apply("ab"));

        System.out.println("== erasure ==");
        List<String> strings = new ArrayList<>();
        @SuppressWarnings({"unchecked", "rawtypes"})
        List<Integer> alias = (List) strings;
        alias.add(42);
        System.out.println("raw size " + strings.size() + " " + (strings.getClass() == ArrayList.class));
        try {
            String s = strings.get(0);
            System.out.println("got " + s);
        } catch (ClassCastException e) {
            System.out.println("ClassCastException at the use");
        }
        Object o = new Box<>(1.5);
        if (o instanceof Box<?> b) {
            System.out.println("box of " + b.get().getClass().getSimpleName());
        }
    }
}
