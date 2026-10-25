// LWJGL's debug allocator and debug stack (Configuration.DEBUG_MEMORY_ALLOCATOR
// and DEBUG_STACK), which walk no stack under warmc: a block not freed is
// reported, without where it was allocated, by the shutdown hook the
// allocator adds, which runs when main returns; a block freed is not.
import static org.lwjgl.system.MemoryUtil.*;

import java.nio.ByteBuffer;
import org.lwjgl.system.Configuration;
import org.lwjgl.system.MemoryStack;

public class Main {
    public static void main(String[] args) {
        Configuration.DEBUG_MEMORY_ALLOCATOR.set(true);
        Configuration.DEBUG_STACK.set(true);
        ByteBuffer kept = memAlloc(24);
        ByteBuffer freed = memAlloc(40);
        memFree(freed);
        // a frame popped, which the debug stack checks, and one closed, which
        // it does not
        MemoryStack stack = MemoryStack.stackPush();
        stack.mallocInt(4);
        try (MemoryStack inner = stack.push()) {
            inner.mallocInt(4);
        }
        stack.pop();
        System.out.println("main returns with " + kept.capacity() + " bytes not freed");
    }
}
