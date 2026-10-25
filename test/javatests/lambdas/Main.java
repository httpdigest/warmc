// Lambdas and method references: capture of locals, fields and this, each
// kind of method reference (static, bound, unbound, constructor, array
// constructor), the java.util.function interfaces and their combinators,
// currying, recursion through a field, closures over loop variables, and
// lambdas stored in collections and called later.
import java.util.*;
import java.util.function.*;

public class Main {
    private int base = 100;
    static IntUnaryOperator fact;

    IntSupplier fromThis() {
        return () -> base + 1;
    }

    static int twice(int x) {
        return 2 * x;
    }

    String greet(String who) {
        return "hi " + who + " from " + base;
    }

    public static void main(String[] args) {
        System.out.println("== captures ==");
        int k = 7;
        String sep = "-";
        IntUnaryOperator addK = x -> x + k;
        Function<String, String> wrap = s -> sep + s + sep;
        Main m = new Main();
        IntSupplier ts = m.fromThis();
        m.base = 200;
        System.out.println(addK.applyAsInt(3) + " " + wrap.apply("x") + " " + ts.getAsInt());
        List<Supplier<Integer>> later = new ArrayList<>();
        for (int i = 0; i < 4; i++) {
            final int j = i;
            later.add(() -> j * j);
        }
        StringBuilder sb = new StringBuilder();
        for (Supplier<Integer> s : later) {
            sb.append(s.get()).append(' ');
        }
        System.out.println(sb.toString().trim());

        System.out.println("== method references ==");
        IntUnaryOperator st = Main::twice;
        Function<String, String> bound = m::greet;
        Function<String, Integer> unbound = String::length;
        BiFunction<String, Integer, Character> charAt = String::charAt;
        Supplier<ArrayList<String>> ctor = ArrayList::new;
        IntFunction<int[]> arr = int[]::new;
        Function<Integer, StringBuilder> sbctor = StringBuilder::new;
        System.out.println(st.applyAsInt(21) + " " + bound.apply("ann") + " " + unbound.apply("four") + " " + charAt.apply("hey", 1)
                + " " + ctor.get().size() + " " + arr.apply(5).length + " " + sbctor.apply(16).capacity());
        Comparator<String> byLen = Comparator.comparing(String::length);
        List<String> ws = new ArrayList<>(List.of("ccc", "a", "bb", "dd", "e"));
        ws.sort(byLen.thenComparing(Comparator.naturalOrder()));
        System.out.println(ws);

        System.out.println("== combinators ==");
        Function<Integer, Integer> inc = x -> x + 1, dbl = x -> x * 2;
        System.out.println(inc.andThen(dbl).apply(5) + " " + inc.compose(dbl).apply(5) + " " + Function.<Integer>identity().apply(9));
        Predicate<String> empty = String::isEmpty, longer = s -> s.length() > 3;
        System.out.println(empty.negate().and(longer).test("hello") + " " + empty.or(longer).test("ab") + " "
                + Predicate.not(empty).test("") + " " + Predicate.isEqual("x").test("x"));
        BinaryOperator<Integer> mx = BinaryOperator.maxBy(Comparator.naturalOrder());
        System.out.println(mx.apply(3, 8) + " " + BinaryOperator.<Integer>minBy(Comparator.naturalOrder()).apply(3, 8));
        UnaryOperator<String> up = String::toUpperCase;
        Consumer<List<String>> addA = l -> l.add("a");
        Consumer<List<String>> addB = l -> l.add("b");
        List<String> target = new ArrayList<>();
        addA.andThen(addB).andThen(addA).accept(target);
        System.out.println(up.apply("shout") + " " + target);
        BiFunction<Integer, Integer, Integer> pow = (a, b) -> {
            int r = 1;
            for (int i = 0; i < b; i++) {
                r *= a;
            }
            return r;
        };
        System.out.println(pow.andThen(x -> x - 1).apply(2, 10));
        ToIntFunction<String> parse = Integer::parseInt;
        DoubleUnaryOperator half = d -> d / 2;
        LongBinaryOperator lmul = (a, b) -> a * b;
        System.out.println(parse.applyAsInt("123") + " " + half.andThen(Math::sqrt).applyAsDouble(32) + " " + lmul.applyAsLong(1L << 31, 4));

        System.out.println("== currying and recursion ==");
        Function<Integer, Function<Integer, Function<Integer, Integer>>> add3 = a -> b -> c -> a + b * 10 + c * 100;
        System.out.println(add3.apply(1).apply(2).apply(3));
        fact = n -> n <= 1 ? 1 : n * fact.applyAsInt(n - 1);
        System.out.println(fact.applyAsInt(10));
        Map<String, BinaryOperator<Integer>> ops = new TreeMap<>();
        ops.put("add", Integer::sum);
        ops.put("max", Math::max);
        ops.put("sub", (a, b) -> a - b);
        ops.put("hyp", (a, b) -> (int) Math.hypot(a, b));
        for (Map.Entry<String, BinaryOperator<Integer>> e : ops.entrySet()) {
            System.out.print(e.getKey() + "=" + e.getValue().apply(12, 5) + " ");
        }
        System.out.println();
        Runnable r = () -> System.out.println("runnable ran");
        r.run();
        Optional<String> o = Optional.of("value");
        System.out.println(o.map(String::length).filter(n -> n > 3).orElse(-1) + " " + Optional.empty().or(() -> Optional.of("alt")).get());
    }
}
