// java.util.stream pipelines.
//
// TeaVM's streams are its own implementation: each stage an object wrapping
// the one before, pulled one element at a time through interface calls, with
// the lambdas as further objects.  Boxed and primitive streams both.
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

public class Main {
    public static void main(String[] args) {
        long total = 0;
        for (int round = 0; round < 100; round++) {
            final int r = round;
            total += IntStream.range(0, 50_000).filter(i -> (i ^ r) % 3 == 0).map(i -> i * 7 % 1013).sum();
            List<Integer> xs = IntStream.range(0, 5000).boxed().collect(Collectors.toList());
            total += xs.stream().filter(x -> x % 7 != r % 7).mapToLong(x -> (long) x * x).reduce(0L, Long::sum);
            total += xs.stream().map(x -> x % 100).distinct().count();
        }
        System.out.println("total = " + total);
    }
}
