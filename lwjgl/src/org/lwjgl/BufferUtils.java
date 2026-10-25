package org.lwjgl;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import org.lwjgl.system.MemoryUtil;

/**
 * Buffers of native memory the program keeps: LWJGL's BufferUtils, for warmc.
 * They are zeroed, in native order, and in the module's linear memory, which
 * nothing frees: the garbage collector that frees LWJGL's has no hold on it.
 */
public final class BufferUtils {
    private BufferUtils() {
    }

    public static ByteBuffer createByteBuffer(int capacity) {
        return MemoryUtil.memByteBuffer(MemoryUtil.nmemCalloc(capacity, 1), capacity);
    }

    public static ShortBuffer createShortBuffer(int capacity) {
        return MemoryUtil.memShortBuffer(MemoryUtil.nmemCalloc(capacity, 2), capacity);
    }

    public static CharBuffer createCharBuffer(int capacity) {
        return MemoryUtil.memCharBuffer(MemoryUtil.nmemCalloc(capacity, 2), capacity);
    }

    public static IntBuffer createIntBuffer(int capacity) {
        return MemoryUtil.memIntBuffer(MemoryUtil.nmemCalloc(capacity, 4), capacity);
    }

    public static LongBuffer createLongBuffer(int capacity) {
        return MemoryUtil.memLongBuffer(MemoryUtil.nmemCalloc(capacity, 8), capacity);
    }

    public static FloatBuffer createFloatBuffer(int capacity) {
        return MemoryUtil.memFloatBuffer(MemoryUtil.nmemCalloc(capacity, 4), capacity);
    }

    public static DoubleBuffer createDoubleBuffer(int capacity) {
        return MemoryUtil.memDoubleBuffer(MemoryUtil.nmemCalloc(capacity, 8), capacity);
    }
}
