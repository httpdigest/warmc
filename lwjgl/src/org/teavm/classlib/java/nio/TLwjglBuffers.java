/*
 * Direct buffers over the module's linear memory, for org.lwjgl.system's
 * MemoryUtil and MemoryStack: TeaVM's own, whose constructors only this
 * package reaches.  It is java.nio.LwjglBuffers to the program: TeaVM
 * substitutes a class of its library for the java.* one a program names, its
 * T-classes renamed within it, and lwjgl/stub/java/nio/LwjglBuffers.java is
 * what javac compiles org.lwjgl against.
 */
package org.teavm.classlib.java.nio;

import org.teavm.classlib.java.nio.file.TAddressBasedBuffer;
import org.teavm.interop.Address;

public final class TLwjglBuffers {
    private TLwjglBuffers() {
    }

    // native order, as LWJGL makes every buffer
    public static TByteBuffer bytes(int address, int capacity) {
        var result = new TByteBufferWasmGC(null, null, 0, null, Address.fromInt(address), capacity, false);
        result.limit = capacity;
        result.order(TByteOrder.LITTLE_ENDIAN);
        return result;
    }

    public static TShortBuffer shorts(int address, int capacity) {
        return new TShortBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity,
                false);
    }

    public static TCharBuffer chars(int address, int capacity) {
        return new TCharBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity,
                false);
    }

    public static TIntBuffer ints(int address, int capacity) {
        return new TIntBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity, false);
    }

    public static TLongBuffer longs(int address, int capacity) {
        return new TLongBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity,
                false);
    }

    public static TFloatBuffer floats(int address, int capacity) {
        return new TFloatBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity,
                false);
    }

    public static TDoubleBuffer doubles(int address, int capacity) {
        return new TDoubleBufferWasmGC(null, null, 0, capacity, false, null, Address.fromInt(address), capacity,
                false);
    }

    // where a direct buffer's element 0 is in linear memory, or -1 for one that
    // is not direct
    public static int address(TBuffer buffer) {
        Object b = buffer;
        if (b instanceof TAddressBasedBuffer) {
            return ((TAddressBasedBuffer) b).getDataAddress().toInt();
        }
        return -1;
    }
}
