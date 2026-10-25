// ByteOrder.nativeOrder() without JavaScript (test/teavm/patches/
// byte-order.patch): wasm's linear memory is little-endian, which is what a
// buffer in native order writes.
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class Main {
    public static void main(String[] args) {
        System.out.println(ByteOrder.nativeOrder());
        ByteBuffer b = ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());
        b.putInt(0, 0x01020304);
        b.putShort(4, (short) 0x0506);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 6; i++) {
            sb.append(b.get(i)).append(' ');
        }
        System.out.println(sb.toString().trim() + " " + b.order() + " " + b.getInt(0));
        b.order(ByteOrder.BIG_ENDIAN);
        System.out.println(b.getInt(0) + " " + b.order());
    }
}
