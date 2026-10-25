// A native of LWJGL's that takes an object, which C cannot be handed: the
// transformer leaves it no import, and TeaVM refuses the program, naming it.
import org.lwjgl.system.jni.JNINativeInterface;

public class Main {
    public static void main(String[] args) {
        System.out.println(JNINativeInterface.NewGlobalRef("a string"));
    }
}
