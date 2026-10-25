// Integer arithmetic: overflow wrapping in int and long, division and
// remainder signs, shifts with counts past the width, unsigned operations,
// Integer's and Long's bit helpers, narrowing casts, compound assignment's
// implicit narrowing, char arithmetic, Math's exact operations, parsing and
// printing in other radixes, and hashing mixes.
public class Main {
    public static void main(String[] args) {
        System.out.println("== overflow ==");
        int imax = Integer.MAX_VALUE, imin = Integer.MIN_VALUE;
        long lmax = Long.MAX_VALUE;
        System.out.println((imax + 1) + " " + (imin - 1) + " " + (imax * 2) + " " + (-imin) + " " + (imin / -1) + " " + (imin % -1));
        System.out.println((lmax + 1) + " " + (lmax * 3) + " " + (Long.MIN_VALUE / -1) + " " + Math.abs(Long.MIN_VALUE));
        int x = 1;
        for (int i = 0; i < 40; i++) {
            x = x * 3 + i;
        }
        System.out.println("wrapped " + x + " " + (x * 0x9E3779B9));

        System.out.println("== division ==");
        int[][] pairs = {{7, 2}, {-7, 2}, {7, -2}, {-7, -2}, {0, 5}, {imin, 7}, {imax, -3}};
        for (int[] p : pairs) {
            System.out.print(p[0] / p[1] + "," + p[0] % p[1] + "," + Math.floorDiv(p[0], p[1]) + "," + Math.floorMod(p[0], p[1]) + " ");
        }
        System.out.println();
        System.out.println((-17L / 5) + " " + (-17L % 5) + " " + Long.divideUnsigned(-17L, 5) + " " + Long.remainderUnsigned(-17L, 5)
                + " " + Integer.divideUnsigned(-1, 3) + " " + Integer.remainderUnsigned(-1, 3));

        System.out.println("== shifts ==");
        int v = 0x80000001;
        System.out.println((v << 1) + " " + (v >> 1) + " " + (v >>> 1) + " " + (1 << 32) + " " + (1 << 33) + " " + (1 << -1)
                + " " + (v >> 31) + " " + (v >>> 63));
        long lv = 0x8000000000000001L;
        System.out.println((lv << 1) + " " + (lv >> 1) + " " + (lv >>> 1) + " " + (1L << 64) + " " + (1L << 65) + " " + (lv >>> 127));
        byte b = (byte) 0xF0;
        System.out.println((b >> 2) + " " + (b >>> 2) + " " + ((b & 0xff) >>> 2) + " " + (byte) (b << 1));

        System.out.println("== bit helpers ==");
        int[] samples = {0, 1, -1, 0x12345678, imin, 0x00F0F000};
        for (int s : samples) {
            System.out.print(Integer.bitCount(s) + "," + Integer.numberOfLeadingZeros(s) + "," + Integer.numberOfTrailingZeros(s)
                    + "," + Integer.highestOneBit(s) + "," + Integer.lowestOneBit(s) + "," + Integer.reverse(s) + ","
                    + Integer.reverseBytes(s) + "," + Integer.rotateLeft(s, 5) + "," + Integer.signum(s) + " ");
        }
        System.out.println();
        long ls = 0x0123456789ABCDEFL;
        System.out.println(Long.bitCount(ls) + " " + Long.numberOfLeadingZeros(ls) + " " + Long.numberOfTrailingZeros(ls) + " "
                + Long.reverse(ls) + " " + Long.reverseBytes(ls) + " " + Long.rotateRight(ls, 12) + " " + Long.highestOneBit(ls));

        System.out.println("== unsigned ==");
        System.out.println(Integer.toUnsignedString(-1) + " " + Integer.toUnsignedLong(-2) + " " + Integer.compareUnsigned(-1, 1)
                + " " + Long.toUnsignedString(-1L) + " " + Long.compareUnsigned(1, -1));

        System.out.println("== narrowing ==");
        int big = 0x12345678;
        System.out.println((byte) big + " " + (short) big + " " + (int) (char) big + " " + (byte) 128 + " " + (short) 65535
                + " " + (int) 3000000000L + " " + (char) 65 + (char) ('A' + 25));
        byte cb = 10;
        cb += 300;
        short cs = 1000;
        cs *= 1000;
        char cc = 'z';
        cc += 1;
        int ci = 7;
        ci /= 2.5;
        System.out.println(cb + " " + cs + " " + (int) cc + " " + ci);
        char c = 'a';
        c++;
        System.out.println(c + " " + (c + 1) + " " + (char) (c + 1) + " " + ('a' + 'b') + " " + (char) ('a' ^ ' ') + " " + Character.getNumericValue('Z'));

        System.out.println("== exact ==");
        try {
            Math.addExact(imax, 1);
        } catch (ArithmeticException e) {
            System.out.println("addExact: " + e.getMessage());
        }
        try {
            Math.multiplyExact(1L << 40, 1L << 40);
        } catch (ArithmeticException e) {
            System.out.println("multiplyExact: " + e.getMessage());
        }
        System.out.println(Math.addExact(5, 6) + " " + Math.negateExact(-5L) + " " + Math.toIntExact(123L));

        System.out.println("== radix ==");
        System.out.println(Integer.toString(255, 16) + " " + Integer.toString(-255, 2) + " " + Integer.toHexString(-1) + " "
                + Integer.toOctalString(8) + " " + Long.toString(Long.MIN_VALUE, 36) + " " + Long.toBinaryString(5L));
        System.out.println(Integer.parseInt("-zz", 36) + " " + Integer.valueOf("0777".substring(1), 8) + " " + Long.parseLong("7fffffffffffffff", 16)
                + " " + Integer.decode("0x1F") + " " + Integer.decode("-010") + " " + Short.parseShort("-32768"));
        try {
            Integer.parseInt("2147483648");
        } catch (NumberFormatException e) {
            System.out.println("NumberFormatException");
        }

        System.out.println("== hashing ==");
        long h = 1125899906842597L;
        for (int i = 0; i < 100; i++) {
            h = 31 * h + i;
            h ^= h >>> 29;
        }
        int m = 0;
        for (int i = 0; i < 1000; i++) {
            m = Integer.rotateLeft(m ^ i, 13) * 5 + 0xe6546b64;
        }
        System.out.println(h + " " + m + " " + Long.hashCode(h) + " " + Integer.hashCode(m) + " " + Character.hashCode('x')
                + " " + Short.hashCode((short) -1) + " " + Byte.hashCode((byte) -1));
    }
}
