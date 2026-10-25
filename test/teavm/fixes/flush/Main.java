// System.out.flush() with bytes from write() waiting (test/teavm/patches/
// console-flush.patch): TeaVM's console stream keeps them until a flush,
// whose action it cleared before calling.  Each group of writes is flushed
// before the next print, which TeaVM's stream sends on at once.
import java.nio.charset.StandardCharsets;

public class Main {
    public static void main(String[] args) {
        System.out.print("print ");
        System.out.flush();
        System.out.write('w');
        System.out.flush();
        System.out.flush();
        System.out.print(" then ");
        // a character split between two writes, and one past the BMP
        byte[] text = "é€😀 und ß".getBytes(StandardCharsets.UTF_8);
        System.out.write(text, 0, 1);
        System.out.write(text, 1, text.length - 1);
        System.out.flush();
        byte[] many = new byte[100];
        for (int i = 0; i < many.length; i++) {
            many[i] = (byte) ('a' + i % 26);
        }
        System.out.write(many, 0, many.length);
        System.out.write('\n');
        System.out.flush();
        System.err.write('e');
        System.err.write('\n');
        System.err.flush();
        System.out.println("done");
    }
}
