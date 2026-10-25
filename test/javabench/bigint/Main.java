// java.math.BigInteger: factorials, Fibonacci numbers and a power modulo.
//
// TeaVM's BigInteger is the class library's arbitrary-precision arithmetic
// over int[] magnitudes: a fresh array per result, and carry loops in long
// arithmetic.
import java.math.BigInteger;

public class Main {
    public static void main(String[] args) {
        BigInteger m = BigInteger.valueOf(1_000_000_007);
        long check = 0;
        for (int round = 0; round < 36; round++) {
            BigInteger f = BigInteger.ONE;
            for (int i = 2; i <= 1500 + round; i++) {
                f = f.multiply(BigInteger.valueOf(i));
            }
            BigInteger a = BigInteger.valueOf(round), b = BigInteger.ONE;
            for (int i = 0; i < 6000; i++) {
                BigInteger t = a.add(b);
                a = b;
                b = t;
            }
            BigInteger p = BigInteger.valueOf(3 + round).modPow(f.mod(m.subtract(BigInteger.ONE)), m);
            check = check * 31 + f.bitLength() + b.mod(m).longValue() + p.longValue()
                    + f.toString().length() + f.divide(b.shiftRight(2000)).bitLength();
        }
        System.out.println("check = " + check);
    }
}
