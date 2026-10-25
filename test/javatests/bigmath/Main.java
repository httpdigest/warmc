// java.math: BigInteger arithmetic, division and remainder with signs, gcd,
// modPow and modInverse, bit operations, primality, conversions and radix
// strings; BigDecimal arithmetic with scales, division with rounding modes,
// comparison against equals, and stripping trailing zeros.
import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;
import java.math.RoundingMode;

public class Main {
    public static void main(String[] args) {
        System.out.println("== BigInteger ==");
        BigInteger f = BigInteger.ONE;
        for (int i = 1; i <= 50; i++) {
            f = f.multiply(BigInteger.valueOf(i));
        }
        System.out.println("50! = " + f + " bits " + f.bitLength() + " lowest " + f.getLowestSetBit());
        BigInteger a = new BigInteger("-123456789012345678901234567890"), b = new BigInteger("987654321");
        System.out.println(a.add(b) + " " + a.subtract(b) + " " + a.multiply(b) + " " + a.divide(b) + " " + a.remainder(b) + " " + a.mod(b));
        BigInteger[] qr = a.divideAndRemainder(b.negate());
        System.out.println(qr[0] + " " + qr[1] + " " + a.signum() + " " + a.abs().negate() + " " + a.compareTo(b) + " " + a.pow(3));
        BigInteger m = BigInteger.valueOf(1_000_000_007);
        System.out.println(BigInteger.TWO.modPow(BigInteger.valueOf(1000), m) + " " + BigInteger.valueOf(3).modInverse(m) + " "
                + f.gcd(BigInteger.valueOf(2).pow(60).multiply(BigInteger.valueOf(77))) + " " + m.isProbablePrime(20) + " "
                + BigInteger.valueOf(1_000_000_005).isProbablePrime(20) + " " + m.nextProbablePrime());
        BigInteger bits = new BigInteger("f0f0f0f0f0f0f0f0f0f0", 16);
        System.out.println(bits.and(BigInteger.valueOf(0xffff)) + " " + bits.or(BigInteger.ONE) + " " + bits.xor(bits.shiftRight(4)).toString(16)
                + " " + bits.not().toString(16) + " " + bits.bitCount() + " " + bits.testBit(4) + " " + bits.setBit(0).clearBit(4).toString(2).length()
                + " " + bits.shiftLeft(70).toString(36) + " " + BigInteger.valueOf(-5).shiftRight(1));
        System.out.println(f.longValue() + " " + f.intValue() + " " + f.doubleValue() + " " + new BigInteger("255").toByteArray().length + " "
                + BigInteger.valueOf(Long.MIN_VALUE).subtract(BigInteger.ONE) + " " + BigInteger.TEN.sqrt() + " " + f.sqrt());

        System.out.println("== BigDecimal ==");
        BigDecimal x = new BigDecimal("10.25"), y = new BigDecimal("3");
        System.out.println(x.add(y) + " " + x.subtract(y) + " " + x.multiply(y) + " " + x.divide(y, 10, RoundingMode.HALF_UP) + " "
                + x.divide(y, MathContext.DECIMAL64) + " " + x.remainder(y) + " " + x.negate().abs() + " " + x.pow(3));
        BigDecimal half = new BigDecimal("2.5");
        for (RoundingMode rm : new RoundingMode[] {RoundingMode.UP, RoundingMode.DOWN, RoundingMode.CEILING, RoundingMode.FLOOR,
            RoundingMode.HALF_UP, RoundingMode.HALF_DOWN, RoundingMode.HALF_EVEN}) {
            System.out.print(rm + "=" + half.setScale(0, rm) + "/" + half.negate().setScale(0, rm) + " ");
        }
        System.out.println();
        BigDecimal p = new BigDecimal("1.50"), q = new BigDecimal("1.5");
        System.out.println(p.equals(q) + " " + (p.compareTo(q) == 0) + " " + p.scale() + " " + p.stripTrailingZeros() + " "
                + new BigDecimal("1200").stripTrailingZeros().toPlainString() + " " + new BigDecimal("1E+3").toPlainString() + " "
                + new BigDecimal("0.000001234").toString() + " " + p.precision() + " " + p.unscaledValue());
        try {
            BigDecimal.ONE.divide(y);
        } catch (ArithmeticException ex) {
            System.out.println("non-terminating: " + ex.getMessage());
        }
        BigDecimal sum = BigDecimal.ZERO;
        for (int i = 0; i < 10; i++) {
            sum = sum.add(new BigDecimal("0.1"));
        }
        System.out.println(sum + " " + sum.equals(BigDecimal.ONE) + " " + (sum.compareTo(BigDecimal.ONE) == 0) + " " + sum.doubleValue()
                + " " + BigDecimal.valueOf(2.5).toBigInteger() + " " + new BigDecimal(0.1).setScale(20, RoundingMode.HALF_EVEN));
    }
}
