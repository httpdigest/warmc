// Bit manipulation through Integer's and Long's static helpers.
//
// bitCount, numberOfLeadingZeros, rotateLeft and reverse are intrinsics or
// short loops in TeaVM's class library, depending on which; what reaches the
// engine is either a single wasm instruction or the library's code inline.
public class Main {
    public static void main(String[] args) {
        long acc = 0;
        int x = 0x12345678;
        long y = 0x9E3779B97F4A7C15L;
        for (int i = 0; i < 32_000_000; i++) {
            x = Integer.rotateLeft(x * 0x2545F491 + i, 7) ^ (x >>> 3);
            y ^= y << 13;
            y ^= y >>> 7;
            y ^= y << 17;
            acc += Integer.bitCount(x) + Long.numberOfLeadingZeros(y | 1) + Integer.numberOfTrailingZeros(x | 0x10000)
                    + (Long.bitCount(y) & 3);
            if ((i & 1023) == 0) {
                acc ^= Integer.reverse(x) ^ Long.reverseBytes(y);
            }
        }
        System.out.println("acc = " + acc + " x = " + x + " y = " + y);
    }
}
