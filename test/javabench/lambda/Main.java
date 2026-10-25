// Lambdas and their composition.
//
// A lambda is a class TeaVM generates for the invokedynamic javac wrote, with
// the captured values as fields; composing them with andThen builds a chain
// of such objects, each applied through an interface call.
import java.util.function.IntBinaryOperator;
import java.util.function.IntUnaryOperator;

public class Main {
    public static void main(String[] args) {
        int[] ks = {3, 5, 7, 11};
        IntUnaryOperator f = x -> x;
        for (int k : ks) {
            IntUnaryOperator g = x -> x * k + 1;
            f = f.andThen(g).andThen(x -> x ^ (x >>> 7));
        }
        IntBinaryOperator mix = (a, b) -> a * 31 + b;
        int acc = 0;
        for (int i = 0; i < 1_500_000; i++) {
            acc = mix.applyAsInt(acc, f.applyAsInt(i));
        }
        System.out.println("acc = " + acc);
    }
}
