package org.lwjgl.system;

import java.nio.ByteBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import java.util.Arrays;

/**
 * A stack of native memory, allocated from its top down in frames: LWJGL's
 * MemoryStack, for warmc, in the module's linear memory.  One default stack,
 * the module's one thread's.
 */
public final class MemoryStack extends Pointer.Default implements AutoCloseable {
    private static final int DEFAULT_STACK_SIZE = 64 * 1024;
    private static final int DEFAULT_STACK_FRAMES = 8;

    private static MemoryStack stack;

    private final int size;
    private int pointer;
    private int[] frames;
    private int frameIndex;

    private MemoryStack(long address, int size) {
        super(address);
        this.size = size;
        this.pointer = size;
        this.frames = new int[DEFAULT_STACK_FRAMES];
    }

    public static MemoryStack create() {
        return create(DEFAULT_STACK_SIZE);
    }

    public static MemoryStack create(int capacity) {
        return new MemoryStack(MemoryUtil.nmemAlignedAlloc(16, capacity), capacity);
    }

    public static MemoryStack ncreate(long address, int size) {
        return new MemoryStack(address, size);
    }

    public static MemoryStack stackGet() {
        if (stack == null) {
            stack = create();
        }
        return stack;
    }

    public static MemoryStack stackPush() {
        return stackGet().push();
    }

    public static MemoryStack stackPop() {
        return stackGet().pop();
    }

    public MemoryStack push() {
        if (frameIndex == frames.length) {
            frames = Arrays.copyOf(frames, frames.length * 3 / 2);
        }
        frames[frameIndex++] = pointer;
        return this;
    }

    public MemoryStack pop() {
        pointer = frames[--frameIndex];
        return this;
    }

    @Override
    public void close() {
        pop();
    }

    public long getAddress() {
        return address;
    }

    public int getSize() {
        return size;
    }

    public int getFrameIndex() {
        return frameIndex;
    }

    public long getPointerAddress() {
        return address + (pointer & 0xFFFFFFFFL);
    }

    public int getPointer() {
        return pointer;
    }

    public void setPointer(int pointer) {
        if (pointer < 0 || pointer > size) {
            throw new IndexOutOfBoundsException("Invalid stack pointer");
        }
        this.pointer = pointer;
    }

    public long nmalloc(int size) {
        return nmalloc(POINTER_SIZE, size);
    }

    public long nmalloc(int alignment, int size) {
        long address = (this.address + pointer - size) & ~Integer.toUnsignedLong(alignment - 1);
        pointer = (int) (address - this.address);
        if (pointer < 0) {
            throw new OutOfMemoryError("Out of stack space.");
        }
        return address;
    }

    public long ncalloc(int alignment, int num, int size) {
        int bytes = num * size;
        long address = nmalloc(alignment, bytes);
        MemoryUtil.memSet(address, 0, bytes);
        return address;
    }

    public ByteBuffer malloc(int alignment, int size) {
        return MemoryUtil.memByteBuffer(nmalloc(alignment, size), size);
    }

    public ByteBuffer calloc(int alignment, int size) {
        return MemoryUtil.memByteBuffer(ncalloc(alignment, size, 1), size);
    }

    public ByteBuffer malloc(int size) {
        return malloc(POINTER_SIZE, size);
    }

    public ByteBuffer calloc(int size) {
        return calloc(POINTER_SIZE, size);
    }

    public ShortBuffer mallocShort(int size) {
        return MemoryUtil.memShortBuffer(nmalloc(2, size << 1), size);
    }

    public ShortBuffer callocShort(int size) {
        return MemoryUtil.memShortBuffer(ncalloc(2, size, 2), size);
    }

    public IntBuffer mallocInt(int size) {
        return MemoryUtil.memIntBuffer(nmalloc(4, size << 2), size);
    }

    public IntBuffer callocInt(int size) {
        return MemoryUtil.memIntBuffer(ncalloc(4, size, 4), size);
    }

    public LongBuffer mallocLong(int size) {
        return MemoryUtil.memLongBuffer(nmalloc(8, size << 3), size);
    }

    public LongBuffer callocLong(int size) {
        return MemoryUtil.memLongBuffer(ncalloc(8, size, 8), size);
    }

    public FloatBuffer mallocFloat(int size) {
        return MemoryUtil.memFloatBuffer(nmalloc(4, size << 2), size);
    }

    public FloatBuffer callocFloat(int size) {
        return MemoryUtil.memFloatBuffer(ncalloc(4, size, 4), size);
    }

    public DoubleBuffer mallocDouble(int size) {
        return MemoryUtil.memDoubleBuffer(nmalloc(8, size << 3), size);
    }

    public DoubleBuffer callocDouble(int size) {
        return MemoryUtil.memDoubleBuffer(ncalloc(8, size, 8), size);
    }

    public ByteBuffer bytes(byte... values) {
        ByteBuffer buffer = malloc(1, values.length).put(values);
        buffer.flip();
        return buffer;
    }

    public ShortBuffer shorts(short... values) {
        ShortBuffer buffer = mallocShort(values.length).put(values);
        buffer.flip();
        return buffer;
    }

    public IntBuffer ints(int x) {
        return mallocInt(1).put(0, x);
    }

    public IntBuffer ints(int... values) {
        IntBuffer buffer = mallocInt(values.length).put(values);
        buffer.flip();
        return buffer;
    }

    public LongBuffer longs(long... values) {
        LongBuffer buffer = mallocLong(values.length).put(values);
        buffer.flip();
        return buffer;
    }

    public FloatBuffer floats(float x) {
        return mallocFloat(1).put(0, x);
    }

    public FloatBuffer floats(float... values) {
        FloatBuffer buffer = mallocFloat(values.length).put(values);
        buffer.flip();
        return buffer;
    }

    public DoubleBuffer doubles(double... values) {
        DoubleBuffer buffer = mallocDouble(values.length).put(values);
        buffer.flip();
        return buffer;
    }

    /** Encodes {@code text} as UTF-8 here, the address at the stack's pointer; its bytes, the terminator not among them. */
    public int nUTF8(CharSequence text, boolean nullTerminated) {
        int length = MemoryUtil.memLengthUTF8(text, nullTerminated);
        long target = nmalloc(1, length);
        return MemoryUtil.encodeUTF8(text, nullTerminated, target);
    }

    public int nUTF8Safe(CharSequence text, boolean nullTerminated) {
        return text == null ? 0 : nUTF8(text, nullTerminated);
    }

    public ByteBuffer UTF8(CharSequence text) {
        return UTF8(text, true);
    }

    public ByteBuffer UTF8(CharSequence text, boolean nullTerminated) {
        int length = MemoryUtil.memLengthUTF8(text, nullTerminated);
        long target = nmalloc(1, length);
        MemoryUtil.encodeUTF8(text, nullTerminated, target);
        return MemoryUtil.memByteBuffer(target, length);
    }

    public ByteBuffer UTF8Safe(CharSequence text) {
        return text == null ? null : UTF8(text);
    }

    public int nASCII(CharSequence text, boolean nullTerminated) {
        return nUTF8(text, nullTerminated);
    }

    public ByteBuffer ASCII(CharSequence text) {
        return UTF8(text, true);
    }

    public ByteBuffer ASCII(CharSequence text, boolean nullTerminated) {
        return UTF8(text, nullTerminated);
    }

    public static ByteBuffer stackMalloc(int size) {
        return stackGet().malloc(size);
    }

    public static ByteBuffer stackCalloc(int size) {
        return stackGet().calloc(size);
    }

    public static IntBuffer stackMallocInt(int size) {
        return stackGet().mallocInt(size);
    }

    public static IntBuffer stackCallocInt(int size) {
        return stackGet().callocInt(size);
    }

    public static FloatBuffer stackMallocFloat(int size) {
        return stackGet().mallocFloat(size);
    }

    public static FloatBuffer stackCallocFloat(int size) {
        return stackGet().callocFloat(size);
    }

    public static LongBuffer stackMallocLong(int size) {
        return stackGet().mallocLong(size);
    }

    public static DoubleBuffer stackMallocDouble(int size) {
        return stackGet().mallocDouble(size);
    }

    public static IntBuffer stackInts(int... values) {
        return stackGet().ints(values);
    }

    public static FloatBuffer stackFloats(float... values) {
        return stackGet().floats(values);
    }

    public static ByteBuffer stackUTF8(CharSequence text) {
        return stackGet().UTF8(text);
    }

    public static ByteBuffer stackUTF8(CharSequence text, boolean nullTerminated) {
        return stackGet().UTF8(text, nullTerminated);
    }

    public static ByteBuffer stackASCII(CharSequence text) {
        return stackGet().ASCII(text);
    }
}
