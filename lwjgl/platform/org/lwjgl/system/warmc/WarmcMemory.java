package org.lwjgl.system.warmc;

import java.nio.Buffer;
import java.nio.BufferOverflowException;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.LwjglBuffers;
import java.nio.ShortBuffer;
import java.nio.charset.StandardCharsets;
import org.lwjgl.system.MemoryBackend;
import org.lwjgl.system.MemoryUtil;
import org.teavm.interop.Address;
import org.teavm.interop.Import;
import org.teavm.runtime.heap.Heap;

/**
 * LWJGL's off-heap memory, for warmc: the {@link MemoryBackend} that
 * {@link Setup} gives MemoryUtil, which LWJGL reads and writes all native
 * memory through.
 *
 * <p>An address is a native one, as in LWJGL, so that C takes it as it is.
 * The program's memory is the module's linear memory, which warmc keeps at a
 * native address that does not move, {@link #base()}, and reserves all 4 GiB a
 * 32-bit memory may grow to: an address there is read and written in place,
 * and a buffer made over it.  Another address is C's -- a GLFW string, the
 * arguments libffi hands an upcall -- and its bytes are copied through linear
 * memory with C's memcpy, the program having no other way to them.  A buffer
 * cannot be made over C's memory.
 */
public final class WarmcMemory implements MemoryBackend {
    // eight bytes of linear memory that C's memory is copied through, made when
    // first needed: a static initializer would have TeaVM check that the class
    // is initialized at every call of the class's methods
    private static int scratch;

    /**
     * Where linear memory is: not a call, but the register warmc keeps it in,
     * which a --native-module's warmc_memory_base is.
     */
    @Import(module = "lwjgl", name = "warmc_memory_base")
    public static native long base();

    @Import(module = "lwjgl", name = "memcpy")
    static native long nmemcpy(long dst, long src, long bytes);

    @Import(module = "lwjgl", name = "memset")
    static native long nmemset(long dst, int value, long bytes);

    // whether bytes at ptr are in linear memory
    static boolean linear(long ptr, long bytes) {
        long off = ptr - base();
        return off >= 0 && off + bytes <= 0x1_0000_0000L;
    }

    static Address at(long ptr) {
        return Address.fromInt((int) (ptr - base()));
    }

    // the native address of an offset in linear memory
    static long address(int offset) {
        return base() + (offset & 0xFFFFFFFFL);
    }

    private static long scratch() {
        if (scratch == 0) {
            scratch = Heap.alloc(8).toInt();
        }
        return address(scratch);
    }

    private static Address in(long ptr, int bytes) {
        if (linear(ptr, bytes)) {
            return at(ptr);
        }
        nmemcpy(scratch(), ptr, bytes);
        return at(scratch());
    }

    private static Address out(long ptr, int bytes) {
        return linear(ptr, bytes) ? at(ptr) : at(scratch());
    }

    private static void flush(long ptr, int bytes) {
        if (!linear(ptr, bytes)) {
            nmemcpy(ptr, scratch(), bytes);
        }
    }

    // --------------------------------------------------------------- reads

    @Override public boolean getBoolean(long ptr) { return getByte(ptr) != 0; }
    @Override public byte getByte(long ptr) { return in(ptr, 1).getByte(); }
    @Override public char getChar(long ptr) { return in(ptr, 2).getChar(); }
    @Override public char getCharUnaligned(long ptr) { return getChar(ptr); }
    @Override public short getShort(long ptr) { return in(ptr, 2).getShort(); }
    @Override public short getShortUnaligned(long ptr) { return getShort(ptr); }
    @Override public int getInt(long ptr) { return in(ptr, 4).getInt(); }
    @Override public int getIntUnaligned(long ptr) { return getInt(ptr); }
    @Override public long getLong(long ptr) { return in(ptr, 8).getLong(); }
    @Override public long getLongUnaligned(long ptr) { return getLong(ptr); }
    @Override public float getFloat(long ptr) { return in(ptr, 4).getFloat(); }
    @Override public float getFloatUnaligned(long ptr) { return getFloat(ptr); }
    @Override public double getDouble(long ptr) { return in(ptr, 8).getDouble(); }
    @Override public double getDoubleUnaligned(long ptr) { return getDouble(ptr); }
    // C's long and a pointer are 8 bytes on AArch64
    @Override public long getCLong(long ptr) { return getLong(ptr); }
    @Override public long getCLongUnaligned(long ptr) { return getLong(ptr); }
    @Override public long getAddress(long ptr) { return getLong(ptr); }
    @Override public long getAddressUnaligned(long ptr) { return getLong(ptr); }

    @Override
    public long getLong(byte[] o, long offset) {
        int i = (int) offset;
        long v = 0;
        for (int k = 7; k >= 0; k--) {
            v = v << 8 | (o[i + k] & 0xFF);
        }
        return v;
    }

    // -------------------------------------------------------------- writes

    @Override public void putBoolean(long ptr, boolean value) { putByte(ptr, (byte) (value ? 1 : 0)); }
    @Override public void putByte(long ptr, byte value) { out(ptr, 1).putByte(value); flush(ptr, 1); }
    @Override public void putChar(long ptr, char value) { out(ptr, 2).putChar(value); flush(ptr, 2); }
    @Override public void putCharUnaligned(long ptr, char value) { putChar(ptr, value); }
    @Override public void putShort(long ptr, short value) { out(ptr, 2).putShort(value); flush(ptr, 2); }
    @Override public void putShortUnaligned(long ptr, short value) { putShort(ptr, value); }
    @Override public void putInt(long ptr, int value) { out(ptr, 4).putInt(value); flush(ptr, 4); }
    @Override public void putIntUnaligned(long ptr, int value) { putInt(ptr, value); }
    @Override public void putLong(long ptr, long value) { out(ptr, 8).putLong(value); flush(ptr, 8); }
    @Override public void putLongUnaligned(long ptr, long value) { putLong(ptr, value); }
    @Override public void putFloat(long ptr, float value) { out(ptr, 4).putFloat(value); flush(ptr, 4); }
    @Override public void putFloatUnaligned(long ptr, float value) { putFloat(ptr, value); }
    @Override public void putDouble(long ptr, double value) { out(ptr, 8).putDouble(value); flush(ptr, 8); }
    @Override public void putDoubleUnaligned(long ptr, double value) { putDouble(ptr, value); }
    @Override public void putCLong(long ptr, long value) { putLong(ptr, value); }
    @Override public void putCLongUnaligned(long ptr, long value) { putLong(ptr, value); }
    @Override public void putAddress(long ptr, long value) { putLong(ptr, value); }
    @Override public void putAddressUnaligned(long ptr, long value) { putLong(ptr, value); }

    // ------------------------------------------------------------- buffers

    // the native address of a buffer's element 0
    private static long start(Buffer buffer) {
        int off = LwjglBuffers.address(buffer);
        if (off == -1) {
            throw new IllegalArgumentException("not a direct buffer");
        }
        return address(off);
    }

    private static int shift(Buffer buffer) {
        if (buffer instanceof ByteBuffer) {
            return 0;
        }
        if (buffer instanceof ShortBuffer || buffer instanceof CharBuffer) {
            return 1;
        }
        if (buffer instanceof IntBuffer || buffer instanceof FloatBuffer) {
            return 2;
        }
        return 3;
    }

    @Override public long getAddress0(Buffer buffer) { return start(buffer); }
    @Override public long getAddress0(ByteBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(CharBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(ShortBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(IntBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(LongBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(FloatBuffer buffer) { return start(buffer); }
    @Override public long getAddress0(DoubleBuffer buffer) { return start(buffer); }

    @Override
    public long getAddress(Buffer buffer) {
        return start(buffer) + ((long) buffer.position() << shift(buffer));
    }

    @Override
    public long getAddress(Buffer buffer, int position) {
        return start(buffer) + ((long) position << shift(buffer));
    }

    @Override public long getAddress(ByteBuffer buffer) { return start(buffer) + buffer.position(); }
    @Override public long getAddress(ByteBuffer buffer, int position) { return start(buffer) + position; }
    @Override public long getAddress(CharBuffer buffer) { return start(buffer) + ((long) buffer.position() << 1); }
    @Override public long getAddress(CharBuffer buffer, int position) { return start(buffer) + ((long) position << 1); }
    @Override public long getAddress(ShortBuffer buffer) { return start(buffer) + ((long) buffer.position() << 1); }
    @Override public long getAddress(ShortBuffer buffer, int position) { return start(buffer) + ((long) position << 1); }
    @Override public long getAddress(IntBuffer buffer) { return start(buffer) + ((long) buffer.position() << 2); }
    @Override public long getAddress(IntBuffer buffer, int position) { return start(buffer) + ((long) position << 2); }
    @Override public long getAddress(LongBuffer buffer) { return start(buffer) + ((long) buffer.position() << 3); }
    @Override public long getAddress(LongBuffer buffer, int position) { return start(buffer) + ((long) position << 3); }
    @Override public long getAddress(FloatBuffer buffer) { return start(buffer) + ((long) buffer.position() << 2); }
    @Override public long getAddress(FloatBuffer buffer, int position) { return start(buffer) + ((long) position << 2); }
    @Override public long getAddress(DoubleBuffer buffer) { return start(buffer) + ((long) buffer.position() << 3); }
    @Override public long getAddress(DoubleBuffer buffer, int position) { return start(buffer) + ((long) position << 3); }

    // the offset of address in linear memory, for a buffer of bytes there
    private static int over(long address, long bytes) {
        if (!linear(address, bytes)) {
            throw new UnsupportedOperationException("a buffer over C's memory, which warmc's program cannot address");
        }
        return (int) (address - base());
    }

    @Override public ByteBuffer wrapBufferByte(long address, int capacity) { return LwjglBuffers.bytes(over(address, capacity), capacity); }
    @Override public CharBuffer wrapBufferChar(long address, int capacity) { return LwjglBuffers.chars(over(address, (long) capacity << 1), capacity); }
    @Override public ShortBuffer wrapBufferShort(long address, int capacity) { return LwjglBuffers.shorts(over(address, (long) capacity << 1), capacity); }
    @Override public IntBuffer wrapBufferInt(long address, int capacity) { return LwjglBuffers.ints(over(address, (long) capacity << 2), capacity); }
    @Override public LongBuffer wrapBufferLong(long address, int capacity) { return LwjglBuffers.longs(over(address, (long) capacity << 3), capacity); }
    @Override public FloatBuffer wrapBufferFloat(long address, int capacity) { return LwjglBuffers.floats(over(address, (long) capacity << 2), capacity); }
    @Override public DoubleBuffer wrapBufferDouble(long address, int capacity) { return LwjglBuffers.doubles(over(address, (long) capacity << 3), capacity); }

    // a duplicate keeps a byte buffer's order, as LWJGL's backends do
    @Override public ByteBuffer duplicate(ByteBuffer buffer) { return buffer.duplicate().order(buffer.order()); }
    @Override public CharBuffer duplicate(CharBuffer buffer) { return buffer.duplicate(); }
    @Override public ShortBuffer duplicate(ShortBuffer buffer) { return buffer.duplicate(); }
    @Override public IntBuffer duplicate(IntBuffer buffer) { return buffer.duplicate(); }
    @Override public LongBuffer duplicate(LongBuffer buffer) { return buffer.duplicate(); }
    @Override public FloatBuffer duplicate(FloatBuffer buffer) { return buffer.duplicate(); }
    @Override public DoubleBuffer duplicate(DoubleBuffer buffer) { return buffer.duplicate(); }

    // offset elements past the position, length of them
    private static long sliced(Buffer buffer, int offset, int length) {
        int position = buffer.position() + offset;
        if ((offset | position) < 0 || buffer.limit() < position || length < 0
                || buffer.capacity() - position < length) {
            throw new IllegalArgumentException();
        }
        return start(buffer) + ((long) position << shift(buffer));
    }

    @Override public ByteBuffer slice(ByteBuffer buffer) { return wrapBufferByte(getAddress(buffer), buffer.remaining()).order(buffer.order()); }
    @Override public ByteBuffer slice(ByteBuffer buffer, int offset, int length) { return wrapBufferByte(sliced(buffer, offset, length), length).order(buffer.order()); }
    @Override public CharBuffer slice(CharBuffer buffer) { return wrapBufferChar(getAddress(buffer), buffer.remaining()); }
    @Override public CharBuffer slice(CharBuffer buffer, int offset, int length) { return wrapBufferChar(sliced(buffer, offset, length), length); }
    @Override public ShortBuffer slice(ShortBuffer buffer) { return wrapBufferShort(getAddress(buffer), buffer.remaining()); }
    @Override public ShortBuffer slice(ShortBuffer buffer, int offset, int length) { return wrapBufferShort(sliced(buffer, offset, length), length); }
    @Override public IntBuffer slice(IntBuffer buffer) { return wrapBufferInt(getAddress(buffer), buffer.remaining()); }
    @Override public IntBuffer slice(IntBuffer buffer, int offset, int length) { return wrapBufferInt(sliced(buffer, offset, length), length); }
    @Override public LongBuffer slice(LongBuffer buffer) { return wrapBufferLong(getAddress(buffer), buffer.remaining()); }
    @Override public LongBuffer slice(LongBuffer buffer, int offset, int length) { return wrapBufferLong(sliced(buffer, offset, length), length); }
    @Override public FloatBuffer slice(FloatBuffer buffer) { return wrapBufferFloat(getAddress(buffer), buffer.remaining()); }
    @Override public FloatBuffer slice(FloatBuffer buffer, int offset, int length) { return wrapBufferFloat(sliced(buffer, offset, length), length); }
    @Override public DoubleBuffer slice(DoubleBuffer buffer) { return wrapBufferDouble(getAddress(buffer), buffer.remaining()); }
    @Override public DoubleBuffer slice(DoubleBuffer buffer, int offset, int length) { return wrapBufferDouble(sliced(buffer, offset, length), length); }

    // ---------------------------------------------------------- bulk copies

    @Override
    public void memset(long ptr, int value, long bytes) {
        nmemset(ptr, value, bytes);
    }

    @Override
    public void memcpy(long src, long dst, long bytes) {
        nmemcpy(dst, src, bytes);
    }

    private static void check(int offset, int size, int length) {
        if ((offset | size) < 0 || length - offset < size) {
            throw new ArrayIndexOutOfBoundsException();
        }
    }

    // A copy from an array to native memory goes straight to linear memory, or
    // through a block of it to C's.
    private static Address to(long dst, long bytes) {
        return linear(dst, bytes) ? at(dst) : Heap.alloc((int) Math.max(bytes, 1));
    }

    private static void done(long dst, Address a, long bytes) {
        if (!linear(dst, bytes)) {
            nmemcpy(dst, address(a.toInt()), bytes);
            Heap.release(a);
        }
    }

    // and one from native memory to an array, from linear memory or from a
    // block of it that C's is copied to
    private static Address from(long src, long bytes) {
        if (linear(src, bytes)) {
            return at(src);
        }
        Address a = Heap.alloc((int) Math.max(bytes, 1));
        nmemcpy(address(a.toInt()), src, bytes);
        return a;
    }

    private static void read(long src, Address a, long bytes) {
        if (!linear(src, bytes)) {
            Heap.release(a);
        }
    }

    @Override
    public void memcpy(byte[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        Address a = to(dst, size);
        for (int i = 0; i < size; i++) {
            a.add(i).putByte(src[offset + i]);
        }
        done(dst, a, size);
    }

    @Override
    public void memcpy(char[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 1;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 1).putChar(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(short[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 1;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 1).putShort(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(int[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 2;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 2).putInt(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(long[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 3;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 3).putLong(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(float[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 2;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 2).putFloat(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(double[] src, long dst, int offset, int size) {
        check(offset, size, src.length);
        long bytes = (long) size << 3;
        Address a = to(dst, bytes);
        for (int i = 0; i < size; i++) {
            a.add(i << 3).putDouble(src[offset + i]);
        }
        done(dst, a, bytes);
    }

    @Override
    public void memcpy(long src, byte[] dst, int offset, int size) {
        check(offset, size, dst.length);
        Address a = from(src, size);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i).getByte();
        }
        read(src, a, size);
    }

    @Override
    public void memcpy(long src, char[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 1;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 1).getChar();
        }
        read(src, a, bytes);
    }

    @Override
    public void memcpy(long src, short[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 1;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 1).getShort();
        }
        read(src, a, bytes);
    }

    @Override
    public void memcpy(long src, int[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 2;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 2).getInt();
        }
        read(src, a, bytes);
    }

    @Override
    public void memcpy(long src, long[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 3;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 3).getLong();
        }
        read(src, a, bytes);
    }

    @Override
    public void memcpy(long src, float[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 2;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 2).getFloat();
        }
        read(src, a, bytes);
    }

    @Override
    public void memcpy(long src, double[] dst, int offset, int size) {
        check(offset, size, dst.length);
        long bytes = (long) size << 3;
        Address a = from(src, bytes);
        for (int i = 0; i < size; i++) {
            dst[offset + i] = a.add(i << 3).getDouble();
        }
        read(src, a, bytes);
    }

    // ------------------------------------------------------------- strings

    private void putBytes(byte[] bytes, boolean nullTerminated, long target) {
        memcpy(bytes, target, 0, bytes.length);
        if (nullTerminated) {
            putByte(target + bytes.length, (byte) 0);
        }
    }

    @Override
    public void putStringASCII(String text, boolean nullTerminated, long target) {
        putStringASCII((CharSequence) text, nullTerminated, target);
    }

    @Override
    public void putStringASCII(CharSequence text, boolean nullTerminated, long target) {
        int n = text.length();
        byte[] bytes = new byte[n];
        for (int i = 0; i < n; i++) {
            bytes[i] = (byte) text.charAt(i);
        }
        putBytes(bytes, nullTerminated, target);
    }

    @Override
    public ByteBuffer allocateUTF8(String text, boolean nullTerminated, MemoryUtil.MemoryAllocator allocator) {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        int length = bytes.length + (nullTerminated ? 1 : 0);
        long target = allocator.malloc(length);
        if (target == MemoryUtil.NULL) {
            throw new OutOfMemoryError();
        }
        putBytes(bytes, nullTerminated, target);
        return wrapBufferByte(target, length);
    }

    @Override
    public void putStringUTF8(CharSequence text, boolean nullTerminated, long target) {
        putBytes(text.toString().getBytes(StandardCharsets.UTF_8), nullTerminated, target);
    }

    @Override
    public int putStringUTF8(String text, boolean nullTerminated, long target, int maxLength) {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        int length = bytes.length + (nullTerminated ? 1 : 0);
        if (maxLength < length) {
            throw new BufferOverflowException();
        }
        putBytes(bytes, nullTerminated, target);
        return length;
    }

    @Override
    public void putStringUTF16(String text, boolean nullTerminated, long target) {
        putStringUTF16((CharSequence) text, nullTerminated, target);
    }

    @Override
    public void putStringUTF16(CharSequence text, boolean nullTerminated, long target) {
        int n = text.length();
        char[] chars = new char[n];
        for (int i = 0; i < n; i++) {
            chars[i] = text.charAt(i);
        }
        memcpy(chars, target, 0, n);
        if (nullTerminated) {
            putChar(target + ((long) n << 1), (char) 0);
        }
    }
}
