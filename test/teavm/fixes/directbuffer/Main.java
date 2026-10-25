// ByteBuffer.allocateDirect's bytes zero (test/teavm/patches/
// direct-buffer-zero.patch), as Java has a new buffer's: buffers of many
// sizes, each written over once checked, so that the heap's blocks come both
// fresh and carved from what is left of others.
import java.nio.ByteBuffer;

public class Main {
    public static void main(String[] args) {
        int bad = 0;
        long sum = 0;
        for (int round = 0; round < 300; round++) {
            int size = 1 + (round * 37) % 700;
            ByteBuffer b = ByteBuffer.allocateDirect(size);
            for (int i = 0; i < size; i++) {
                if (b.get(i) != 0) {
                    bad++;
                    break;
                }
            }
            for (int i = 0; i < size; i++) {
                b.put(i, (byte) (i + round));
            }
            sum += b.get(size - 1);
        }
        System.out.println("buffers not zero: " + bad + ", " + sum);
        ByteBuffer big = ByteBuffer.allocateDirect(1 << 20);
        int nonzero = 0;
        for (int i = 0; i < big.capacity(); i++) {
            nonzero += big.get(i) != 0 ? 1 : 0;
        }
        System.out.println("a megabyte, bytes not zero: " + nonzero);
    }
}
