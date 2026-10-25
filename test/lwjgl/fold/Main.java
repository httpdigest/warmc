// test/lwjgl/fold: what LWJGL's GLCapabilities checks, of this program's own
// GLCapabilities.java, look up, and what they answer, the checks of
// functions macOS's OpenGL framework has none of folded.
import java.nio.ByteBuffer;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.lwjgl.opengl.GLCapabilities;
import org.lwjgl.system.FunctionProvider;
import org.lwjgl.system.MemoryUtil;

public class Main {
    public static void main(String[] args) {
        StringBuilder looked = new StringBuilder();
        FunctionProvider p = new FunctionProvider() {
            @Override
            public long getFunctionAddress(CharSequence name) {
                looked.append(' ').append(name);
                return name.toString().equals("glEnable") ? MemoryUtil.NULL : 0x1000 + name.length();
            }

            @Override
            public long getFunctionAddress(ByteBuffer name) {
                return getFunctionAddress(MemoryUtil.memASCII(name));
            }
        };
        Set<String> ext = new HashSet<>(List.of("A", "B", "C", "D", "E"));
        System.out.println(GLCapabilities.run(p, ext));
        System.out.println("looked up:" + looked);
    }
}
