package org.lwjgl.system;

import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import java.nio.LwjglBuffers;
import org.teavm.interop.Address;
import org.teavm.interop.Import;
import org.teavm.runtime.heap.Heap;

/**
 * Native memory: LWJGL's MemoryUtil, for warmc.
 *
 * <p>An address is a native one, as in LWJGL, so that C takes it as it is.
 * What Java allocates -- memAlloc, MemoryStack, the buffers of BufferUtils --
 * is in the module's linear memory, at native address {@code base()} plus its
 * offset there, and Java reads and writes it with the module's own loads and
 * stores, bounds-checked as wasm's are.  Memory C owns (a string GLFW or
 * OpenGL hands back) is outside it, and reached through memcpy.
 */
public final class MemoryUtil {
    public static final long NULL = 0L;
    public static final int PAGE_SIZE = 4096;
    public static final int CACHE_LINE_SIZE = 64;

    // eight bytes of linear memory that C memory is copied through, made when
    // first needed: a static initializer would have TeaVM check that the class
    // is initialized at every call of the class's methods
    private static int scratch;

    /**
     * Where linear memory is, a native address that does not move, as warmc
     * reserves all a 32-bit memory may grow to: not a call, but the register
     * warmc keeps it in, which a --native-module's warmc_memory_base is.
     */
    @Import(module = "lwjgl", name = "warmc_memory_base")
    static native long base();

    @Import(module = "lwjgl", name = "memcpy")
    private static native long nmemcpy(long dst, long src, long bytes);

    @Import(module = "lwjgl", name = "memset")
    private static native long nmemset(long dst, int value, long bytes);

    @Import(module = "lwjgl", name = "strlen")
    private static native long nstrlen(long str);

    private MemoryUtil() {
    }

    // the offset in linear memory of a native address there, or -1
    static int offsetOf(long address) {
        long off = address - base();
        return off >= 0 && off < 0x1_0000_0000L ? (int) off : -1;
    }

    private static Address at(long address) {
        return Address.fromInt((int) (address - base()));
    }

    private static boolean linear(long address, long bytes) {
        long off = address - base();
        return off >= 0 && off + bytes <= 0x1_0000_0000L;
    }

    private static long scratch() {
        if (scratch == 0) {
            scratch = Heap.alloc(8).toInt();
        }
        return base() + (scratch & 0xFFFFFFFFL);
    }

    // ----------------------------------------------------------- allocation

    public static long nmemAlloc(long size) {
        if (size < 0 || size > Integer.MAX_VALUE - 16) {
            return NULL;
        }
        Address a = Heap.alloc((int) Math.max(size, 1));
        return a.toInt() == 0 ? NULL : base() + (a.toInt() & 0xFFFFFFFFL);
    }

    public static long nmemAllocChecked(long size) {
        long address = nmemAlloc(size);
        if (address == NULL) {
            throw new OutOfMemoryError("Out of heap memory");
        }
        return address;
    }

    public static long nmemCalloc(long num, long size) {
        long bytes = num * size;
        long address = nmemAlloc(bytes);
        if (address != NULL) {
            memSet(address, 0, bytes);
        }
        return address;
    }

    public static long nmemRealloc(long ptr, long size) {
        if (ptr == NULL) {
            return nmemAlloc(size);
        }
        Address a = Heap.realloc(at(ptr), (int) Math.max(size, 1));
        return a.toInt() == 0 ? NULL : base() + (a.toInt() & 0xFFFFFFFFL);
    }

    public static void nmemFree(long ptr) {
        if (ptr != NULL) {
            Heap.release(at(ptr));
        }
    }

    // aligned: the block's own address goes in the 8 bytes before the one
    // handed out
    public static long nmemAlignedAlloc(long alignment, long size) {
        long raw = nmemAlloc(size + alignment + 8);
        if (raw == NULL) {
            return NULL;
        }
        long address = (raw + 8 + alignment - 1) & -alignment;
        memPutLong(address - 8, raw);
        return address;
    }

    public static void nmemAlignedFree(long ptr) {
        if (ptr != NULL) {
            nmemFree(memGetLong(ptr - 8));
        }
    }

    public static ByteBuffer memAlloc(int size) {
        return memByteBuffer(nmemAllocChecked(size), size);
    }

    public static ShortBuffer memAllocShort(int size) {
        return memShortBuffer(nmemAllocChecked((long) size << 1), size);
    }

    public static IntBuffer memAllocInt(int size) {
        return memIntBuffer(nmemAllocChecked((long) size << 2), size);
    }

    public static LongBuffer memAllocLong(int size) {
        return memLongBuffer(nmemAllocChecked((long) size << 3), size);
    }

    public static FloatBuffer memAllocFloat(int size) {
        return memFloatBuffer(nmemAllocChecked((long) size << 2), size);
    }

    public static DoubleBuffer memAllocDouble(int size) {
        return memDoubleBuffer(nmemAllocChecked((long) size << 3), size);
    }

    public static ByteBuffer memCalloc(int num, int size) {
        return memByteBuffer(nmemCalloc(num, size), num * size);
    }

    public static ByteBuffer memCalloc(int num) {
        return memCalloc(num, 1);
    }

    public static IntBuffer memCallocInt(int num) {
        return memIntBuffer(nmemCalloc(num, 4), num);
    }

    public static FloatBuffer memCallocFloat(int num) {
        return memFloatBuffer(nmemCalloc(num, 4), num);
    }

    public static ByteBuffer memRealloc(ByteBuffer ptr, int size) {
        long address = nmemRealloc(ptr == null ? NULL : memAddress0(ptr), size);
        if (address == NULL) {
            throw new OutOfMemoryError("Out of heap memory");
        }
        ByteBuffer result = memByteBuffer(address, size);
        if (ptr != null) {
            result.position(Math.min(ptr.position(), size));
        }
        return result;
    }

    public static void memFree(Buffer ptr) {
        if (ptr != null) {
            nmemFree(memAddress0(ptr));
        }
    }

    // ------------------------------------------------------------- addresses

    /** The address of a direct buffer's element 0. */
    public static long memAddress0(Buffer buffer) {
        int offset = LwjglBuffers.address(buffer);
        if (offset == -1) {
            throw new IllegalArgumentException("The buffer is not a direct buffer");
        }
        return base() + (offset & 0xFFFFFFFFL);
    }

    public static long memAddress0Safe(Buffer buffer) {
        return buffer == null ? NULL : memAddress0(buffer);
    }

    public static long memAddress(ByteBuffer buffer) {
        return memAddress0(buffer) + buffer.position();
    }

    public static long memAddress(ByteBuffer buffer, int position) {
        return memAddress0(buffer) + position;
    }

    public static long memAddress(ShortBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 1);
    }

    public static long memAddress(ShortBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 1);
    }

    public static long memAddress(CharBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 1);
    }

    public static long memAddress(CharBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 1);
    }

    public static long memAddress(IntBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 2);
    }

    public static long memAddress(IntBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 2);
    }

    public static long memAddress(FloatBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 2);
    }

    public static long memAddress(FloatBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 2);
    }

    public static long memAddress(LongBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 3);
    }

    public static long memAddress(LongBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 3);
    }

    public static long memAddress(DoubleBuffer buffer) {
        return memAddress0(buffer) + ((long) buffer.position() << 3);
    }

    public static long memAddress(DoubleBuffer buffer, int position) {
        return memAddress0(buffer) + ((long) position << 3);
    }

    public static long memAddress(Buffer buffer) {
        int shift;
        if (buffer instanceof ByteBuffer) {
            shift = 0;
        } else if (buffer instanceof ShortBuffer || buffer instanceof CharBuffer) {
            shift = 1;
        } else if (buffer instanceof IntBuffer || buffer instanceof FloatBuffer) {
            shift = 2;
        } else {
            shift = 3;
        }
        return memAddress0(buffer) + ((long) buffer.position() << shift);
    }

    public static long memAddressSafe(ByteBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(ShortBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(CharBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(IntBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(FloatBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(LongBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(DoubleBuffer buffer) {
        return buffer == null ? NULL : memAddress(buffer);
    }

    public static long memAddressSafe(Pointer pointer) {
        return pointer == null ? NULL : pointer.address();
    }

    // ---------------------------------------------- buffers over an address

    private static int linearOffset(long address, long bytes) {
        if (!linear(address, bytes)) {
            throw new IllegalArgumentException(
                    "warmc: a buffer of native memory outside the module's linear memory");
        }
        return (int) (address - base());
    }

    public static ByteBuffer memByteBuffer(long address, int capacity) {
        return LwjglBuffers.bytes(linearOffset(address, capacity), capacity);
    }

    public static ByteBuffer memByteBufferSafe(long address, int capacity) {
        return address == NULL ? null : memByteBuffer(address, capacity);
    }

    public static ShortBuffer memShortBuffer(long address, int capacity) {
        return LwjglBuffers.shorts(linearOffset(address, (long) capacity << 1), capacity);
    }

    public static CharBuffer memCharBuffer(long address, int capacity) {
        return LwjglBuffers.chars(linearOffset(address, (long) capacity << 1), capacity);
    }

    public static IntBuffer memIntBuffer(long address, int capacity) {
        return LwjglBuffers.ints(linearOffset(address, (long) capacity << 2), capacity);
    }

    public static IntBuffer memIntBufferSafe(long address, int capacity) {
        return address == NULL ? null : memIntBuffer(address, capacity);
    }

    public static LongBuffer memLongBuffer(long address, int capacity) {
        return LwjglBuffers.longs(linearOffset(address, (long) capacity << 3), capacity);
    }

    public static FloatBuffer memFloatBuffer(long address, int capacity) {
        return LwjglBuffers.floats(linearOffset(address, (long) capacity << 2), capacity);
    }

    public static FloatBuffer memFloatBufferSafe(long address, int capacity) {
        return address == NULL ? null : memFloatBuffer(address, capacity);
    }

    public static DoubleBuffer memDoubleBuffer(long address, int capacity) {
        return LwjglBuffers.doubles(linearOffset(address, (long) capacity << 3),
                capacity);
    }

    public static ByteBuffer memSlice(ByteBuffer buffer) {
        return memByteBuffer(memAddress(buffer), buffer.remaining());
    }

    public static ByteBuffer memSlice(ByteBuffer buffer, int offset, int capacity) {
        return memByteBuffer(memAddress(buffer) + offset, capacity);
    }

    // ------------------------------------------------- reads and writes

    public static void memSet(long ptr, int value, long bytes) {
        if (linear(ptr, bytes)) {
            Address a = at(ptr);
            for (long i = 0; i < bytes; i++) {
                a.add((int) i).putByte((byte) value);
            }
        } else {
            nmemset(ptr, value, bytes);
        }
    }

    public static void memSet(ByteBuffer ptr, int value) {
        memSet(memAddress(ptr), value, ptr.remaining());
    }

    public static void memCopy(long src, long dst, long bytes) {
        nmemcpy(dst, src, bytes);
    }

    public static void memCopy(ByteBuffer src, ByteBuffer dst) {
        memCopy(memAddress(src), memAddress(dst), src.remaining());
    }

    // through linear memory's scratch bytes, for memory C owns
    private static long scratchIn(long ptr, int bytes) {
        nmemcpy(scratch(), ptr, bytes);
        return scratch();
    }

    public static boolean memGetBoolean(long ptr) {
        return memGetByte(ptr) != 0;
    }

    public static byte memGetByte(long ptr) {
        return linear(ptr, 1) ? at(ptr).getByte() : at(scratchIn(ptr, 1)).getByte();
    }

    public static short memGetShort(long ptr) {
        return linear(ptr, 2) ? at(ptr).getShort() : at(scratchIn(ptr, 2)).getShort();
    }

    public static int memGetInt(long ptr) {
        return linear(ptr, 4) ? at(ptr).getInt() : at(scratchIn(ptr, 4)).getInt();
    }

    public static long memGetLong(long ptr) {
        return linear(ptr, 8) ? at(ptr).getLong() : at(scratchIn(ptr, 8)).getLong();
    }

    public static float memGetFloat(long ptr) {
        return linear(ptr, 4) ? at(ptr).getFloat() : at(scratchIn(ptr, 4)).getFloat();
    }

    public static double memGetDouble(long ptr) {
        return linear(ptr, 8) ? at(ptr).getDouble() : at(scratchIn(ptr, 8)).getDouble();
    }

    public static long memGetAddress(long ptr) {
        return memGetLong(ptr);
    }

    private static void scratchOut(long ptr, int bytes) {
        nmemcpy(ptr, scratch(), bytes);
    }

    public static void memPutByte(long ptr, byte value) {
        if (linear(ptr, 1)) {
            at(ptr).putByte(value);
        } else {
            at(scratch()).putByte(value);
            scratchOut(ptr, 1);
        }
    }

    public static void memPutShort(long ptr, short value) {
        if (linear(ptr, 2)) {
            at(ptr).putShort(value);
        } else {
            at(scratch()).putShort(value);
            scratchOut(ptr, 2);
        }
    }

    public static void memPutInt(long ptr, int value) {
        if (linear(ptr, 4)) {
            at(ptr).putInt(value);
        } else {
            at(scratch()).putInt(value);
            scratchOut(ptr, 4);
        }
    }

    public static void memPutLong(long ptr, long value) {
        if (linear(ptr, 8)) {
            at(ptr).putLong(value);
        } else {
            at(scratch()).putLong(value);
            scratchOut(ptr, 8);
        }
    }

    public static void memPutFloat(long ptr, float value) {
        if (linear(ptr, 4)) {
            at(ptr).putFloat(value);
        } else {
            at(scratch()).putFloat(value);
            scratchOut(ptr, 4);
        }
    }

    public static void memPutDouble(long ptr, double value) {
        if (linear(ptr, 8)) {
            at(ptr).putDouble(value);
        } else {
            at(scratch()).putDouble(value);
            scratchOut(ptr, 8);
        }
    }

    public static void memPutAddress(long ptr, long value) {
        memPutLong(ptr, value);
    }

    // ------------------------------------------------------------- strings

    /** The bytes {@code text} takes as UTF-8, and its null terminator if asked. */
    public static int memLengthUTF8(CharSequence value, boolean nullTerminated) {
        int length = 0;
        int n = value.length();
        for (int i = 0; i < n; i++) {
            char c = value.charAt(i);
            if (c < 0x80) {
                length++;
            } else if (c < 0x800) {
                length += 2;
            } else if (Character.isHighSurrogate(c) && i + 1 < n && Character.isLowSurrogate(value.charAt(i + 1))) {
                length += 4;
                i++;
            } else {
                length += 3;
            }
        }
        return nullTerminated ? length + 1 : length;
    }

    /** Encodes {@code text} as UTF-8 at {@code target}; the bytes written, the terminator not among them. */
    static int encodeUTF8(CharSequence text, boolean nullTerminated, long target) {
        int p = 0;
        int n = text.length();
        for (int i = 0; i < n; i++) {
            int c = text.charAt(i);
            if (Character.isHighSurrogate((char) c) && i + 1 < n && Character.isLowSurrogate(text.charAt(i + 1))) {
                c = Character.toCodePoint((char) c, text.charAt(++i));
            }
            if (c < 0x80) {
                memPutByte(target + p++, (byte) c);
            } else if (c < 0x800) {
                memPutByte(target + p++, (byte) (0xC0 | (c >> 6)));
                memPutByte(target + p++, (byte) (0x80 | (c & 0x3F)));
            } else if (c < 0x10000) {
                memPutByte(target + p++, (byte) (0xE0 | (c >> 12)));
                memPutByte(target + p++, (byte) (0x80 | ((c >> 6) & 0x3F)));
                memPutByte(target + p++, (byte) (0x80 | (c & 0x3F)));
            } else {
                memPutByte(target + p++, (byte) (0xF0 | (c >> 18)));
                memPutByte(target + p++, (byte) (0x80 | ((c >> 12) & 0x3F)));
                memPutByte(target + p++, (byte) (0x80 | ((c >> 6) & 0x3F)));
                memPutByte(target + p++, (byte) (0x80 | (c & 0x3F)));
            }
        }
        if (nullTerminated) {
            memPutByte(target + p, (byte) 0);
        }
        return p;
    }

    public static ByteBuffer memUTF8(CharSequence text) {
        return memUTF8(text, true);
    }

    public static ByteBuffer memUTF8(CharSequence text, boolean nullTerminated) {
        int length = memLengthUTF8(text, nullTerminated);
        long target = nmemAllocChecked(length);
        encodeUTF8(text, nullTerminated, target);
        return memByteBuffer(target, length);
    }

    public static ByteBuffer memUTF8Safe(CharSequence text) {
        return text == null ? null : memUTF8(text);
    }

    public static ByteBuffer memASCII(CharSequence text) {
        return memUTF8(text, true);
    }

    public static ByteBuffer memASCII(CharSequence text, boolean nullTerminated) {
        return memUTF8(text, nullTerminated);
    }

    /** The null-terminated string at {@code address}'s length in bytes. */
    public static int memLengthNT1(long address) {
        if (offsetOf(address) == -1) {
            return (int) nstrlen(address);
        }
        int n = 0;
        while (memGetByte(address + n) != 0) {
            n++;
        }
        return n;
    }

    public static String memUTF8(long address) {
        return memUTF8(address, memLengthNT1(address));
    }

    public static String memUTF8(long address, int length) {
        if (offsetOf(address) == -1) {
            // copied into linear memory first: one call to C rather than a call a byte
            long copy = nmemAllocChecked(Math.max(length, 1));
            nmemcpy(copy, address, length);
            String result = memUTF8(copy, length);
            nmemFree(copy);
            return result;
        }
        StringBuilder sb = new StringBuilder(length);
        int i = 0;
        while (i < length) {
            int b = memGetByte(address + i) & 0xFF;
            int c;
            int extra;
            if (b < 0x80) {
                c = b;
                extra = 0;
            } else if (b >= 0xF0) {
                c = b & 0x07;
                extra = 3;
            } else if (b >= 0xE0) {
                c = b & 0x0F;
                extra = 2;
            } else {
                c = b & 0x1F;
                extra = 1;
            }
            i++;
            for (int k = 0; k < extra && i < length; k++) {
                c = (c << 6) | (memGetByte(address + i++) & 0x3F);
            }
            sb.appendCodePoint(c);
        }
        return sb.toString();
    }

    public static String memUTF8Safe(long address) {
        return address == NULL ? null : memUTF8(address);
    }

    public static String memUTF8(ByteBuffer buffer) {
        int length = 0;
        while (length < buffer.remaining() && buffer.get(buffer.position() + length) != 0) {
            length++;
        }
        return memUTF8(memAddress(buffer), length);
    }

    public static String memUTF8Safe(ByteBuffer buffer) {
        return buffer == null ? null : memUTF8(buffer);
    }

    public static String memASCII(long address) {
        return memUTF8(address);
    }

    public static String memASCIISafe(long address) {
        return memUTF8Safe(address);
    }
}
