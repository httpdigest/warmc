// Streams: IntStream and LongStream ranges and reductions, boxed streams
// with map, filter, flatMap, distinct, sorted, limit, skip, peek, and the
// collectors: toList, toSet, toMap with merges, groupingBy with downstream
// counting, summing and mapping, partitioningBy, joining, averaging and
// summarizing; reduce in each form, anyMatch, allMatch, noneMatch, findFirst,
// min and max, and iterate and generate with limits.
import java.util.*;
import java.util.function.Function;
import java.util.stream.*;

public class Main {
    record Person(String name, int age, String city) {
    }

    public static void main(String[] args) {
        System.out.println("== primitive streams ==");
        System.out.println(IntStream.range(0, 10).sum() + " " + IntStream.rangeClosed(1, 5).reduce(1, (a, b) -> a * b) + " "
                + IntStream.of(3, 1, 4, 1, 5, 9).max().getAsInt() + " " + IntStream.of(3, 1, 4).average().getAsDouble() + " "
                + LongStream.rangeClosed(1, 20).reduce(1L, (a, b) -> a * b) + " " + IntStream.empty().max().isPresent());
        IntSummaryStatistics st = IntStream.of(4, 8, 15, 16, 23, 42).summaryStatistics();
        System.out.println(st.getMin() + " " + st.getMax() + " " + st.getSum() + " " + st.getCount() + " " + st.getAverage());
        System.out.println(Arrays.toString(IntStream.iterate(1, x -> x * 3).limit(8).toArray()) + " "
                + Arrays.toString(IntStream.range(0, 20).filter(x -> x % 3 == 0).map(x -> x * x).skip(2).toArray()));
        System.out.println(DoubleStream.of(1.5, 2.5, 3.0).map(Math::sqrt).sum() + " " + IntStream.range(0, 5).mapToObj(i -> "i" + i)
                .collect(Collectors.joining(",", "[", "]")));

        System.out.println("== object streams ==");
        List<String> words = List.of("delta", "alpha", "charlie", "bravo", "alpha", "echo", "delta", "foxtrot");
        System.out.println(words.stream().distinct().sorted().collect(Collectors.toList()));
        System.out.println(words.stream().filter(w -> w.length() == 5).map(String::toUpperCase).sorted(Comparator.reverseOrder()).toList());
        System.out.println(words.stream().map(String::length).reduce(0, Integer::sum) + " "
                + words.stream().reduce("", (a, b) -> a.isEmpty() ? b : a + "+" + b).length() + " "
                + words.stream().reduce((a, b) -> a.compareTo(b) > 0 ? a : b).get());
        System.out.println(words.stream().anyMatch(w -> w.startsWith("e")) + " " + words.stream().allMatch(w -> w.length() > 3) + " "
                + words.stream().noneMatch(String::isEmpty) + " " + words.stream().filter(w -> w.contains("r")).findFirst().orElse("-")
                + " " + words.stream().min(Comparator.comparing(String::length)).get() + " " + words.stream().count());
        System.out.println(Stream.of(List.of(1, 2), List.of(3), List.<Integer>of(), List.of(4, 5, 6)).flatMap(List::stream)
                .map(x -> x * 10).collect(Collectors.toList()));
        List<Integer> peeked = new ArrayList<>();
        int s = Stream.iterate(1, x -> x + 2).limit(6).peek(peeked::add).mapToInt(Integer::intValue).sum();
        System.out.println(s + " " + peeked + " " + Stream.generate(() -> "z").limit(3).collect(Collectors.joining()));

        System.out.println("== collectors ==");
        List<Person> ps = List.of(new Person("ann", 31, "oslo"), new Person("bob", 25, "rome"), new Person("cat", 31, "oslo"),
                new Person("dan", 42, "lima"), new Person("eve", 25, "rome"), new Person("fay", 19, "oslo"));
        Map<String, Long> perCity = ps.stream().collect(Collectors.groupingBy(Person::city, TreeMap::new, Collectors.counting()));
        System.out.println(perCity);
        Map<Integer, List<String>> byAge = ps.stream().collect(Collectors.groupingBy(Person::age, TreeMap::new,
                Collectors.mapping(Person::name, Collectors.toList())));
        System.out.println(byAge);
        Map<Boolean, List<String>> adults = ps.stream().collect(Collectors.partitioningBy(p -> p.age() >= 30,
                Collectors.mapping(Person::name, Collectors.toList())));
        System.out.println(adults);
        Map<String, Integer> ageSum = ps.stream().collect(Collectors.toMap(Person::city, Person::age, Integer::sum, TreeMap::new));
        System.out.println(ageSum);
        System.out.println(ps.stream().collect(Collectors.averagingInt(Person::age)) + " "
                + ps.stream().collect(Collectors.summingInt(Person::age)) + " "
                + ps.stream().map(Person::name).collect(Collectors.joining("|")) + " "
                + new TreeSet<>(ps.stream().map(Person::city).collect(Collectors.toSet())));
        Map<String, Person> oldest = ps.stream().collect(Collectors.toMap(Person::city, Function.identity(),
                (a, b) -> a.age() >= b.age() ? a : b, TreeMap::new));
        oldest.forEach((city, p) -> System.out.print(city + ":" + p.name() + " "));
        System.out.println();
        System.out.println(ps.stream().sorted(Comparator.comparingInt(Person::age).thenComparing(Person::name, Comparator.reverseOrder()))
                .map(Person::name).toList());
    }
}
