// LWJGL's off-heap memory through warmc's backend and allocator
// (lwjgl/platform/org/lwjgl/system/warmc): reads and writes of every type in
// the program's memory and in C's, the bytes they make, bulk copies between
// the two and Java arrays, strings each way, MemoryUtil's allocations,
// buffers over memory, and MemoryStack's frames.  What it prints is what the
// JVM does with LWJGL's own natives (expected.txt).
import static org.lwjgl.system.MemoryUtil.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.CharBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import java.util.Arrays;
import org.lwjgl.PointerBuffer;
import org.lwjgl.system.MemoryBackend;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.libc.LibCStdlib;

public class Main {
    static String hex(long p, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            sb.append(Integer.toHexString((memGetByte(p + i) & 0xFF) | 0x100).substring(1));
        }
        return sb.toString();
    }

    // the units before a unit of zero
    static int strlen(long p, int unit) {
        int n = 0;
        while ((unit == 1 ? memGetByte(p + n) : memGetShort(p + 2L * n)) != 0) {
            n++;
        }
        return n;
    }

    // every type at p, read back, and the bytes it made; and two unaligned
    static void types(String where, long p) {
        memPutByte(p, (byte) -2);
        memPutBoolean(p + 1, true);
        memPutShort(p + 2, (short) -12345);
        memPutInt(p + 4, 0x12345678);
        memPutLong(p + 8, 0x0102030405060708L);
        memPutFloat(p + 16, -1.5f);
        memPutInt(p + 20, 0);
        memPutDouble(p + 24, Math.PI);
        memPutAddress(p + 32, 0x1122334455667788L);
        memPutCLong(p + 40, -3);
        memPutInt(p + 49, 0xCAFEBABE);
        memPutLong(p + 53, -0x123456789ABCDEFL);
        System.out.println(where + ": " + memGetByte(p) + " " + memGetBoolean(p + 1) + " " + memGetShort(p + 2) + " "
                + Integer.toHexString(memGetInt(p + 4)) + " " + Long.toHexString(memGetLong(p + 8)) + " "
                + memGetFloat(p + 16) + " " + memGetDouble(p + 24) + " " + Long.toHexString(memGetAddress(p + 32)) + " "
                + memGetCLong(p + 40) + " " + Integer.toHexString(memGetInt(p + 49)) + " " + memGetLong(p + 53));
        System.out.println(where + ": " + hex(p, 61));
    }

    // a backend copy of each array type to p and back, at an offset
    static void arrays(String where, MemoryBackend be, long p) {
        short[] s = {1, -2, 3};
        char[] c = {'a', 'é', '✓'};
        int[] i = {10, -20, 30};
        long[] l = {1L << 40, -5, 7};
        float[] f = {0.5f, -1.25f, 3e9f};
        double[] d = {Math.E, -0.0, 1e300};
        byte[] b = {9, -9, 127};
        short[] s2 = new short[3];
        char[] c2 = new char[3];
        int[] i2 = new int[3];
        long[] l2 = new long[3];
        float[] f2 = new float[3];
        double[] d2 = new double[3];
        byte[] b2 = new byte[3];
        be.memcpy(b, p, 1, 2);
        be.memcpy(p, b2, 0, 2);
        be.memcpy(s, p, 0, 3);
        be.memcpy(p, s2, 1, 2);
        be.memcpy(c, p, 0, 3);
        be.memcpy(p, c2, 0, 3);
        be.memcpy(i, p, 1, 2);
        be.memcpy(p, i2, 0, 3);
        String ints = hex(p, 12);
        be.memcpy(l, p, 0, 3);
        be.memcpy(p, l2, 0, 3);
        be.memcpy(f, p, 0, 3);
        be.memcpy(p, f2, 0, 3);
        be.memcpy(d, p, 0, 3);
        be.memcpy(p, d2, 0, 3);
        System.out.println(where + ": " + Arrays.toString(b2) + Arrays.toString(s2) + Arrays.toString(c2)
                + Arrays.toString(i2) + Arrays.toString(l2) + Arrays.toString(f2) + Arrays.toString(d2) + " " + ints);
        memPutLong(p, 0);
        try {
            be.memcpy(i, p, 2, 2);
            System.out.println(where + ": past the array's end copied");
        } catch (IndexOutOfBoundsException e) {
            System.out.println(where + ": past the array's end refused, " + hex(p, 8));
        }
    }

    // strings written to p and read back
    static void strings(String where, MemoryBackend be, long p) {
        String text = "héllo ✓ 𝄞";
        be.putStringASCII("ascii", true, p);
        System.out.println(where + ": " + memASCII(p) + " " + memASCII(p, 3) + " " + strlen(p, 1));
        be.putStringUTF8(text, true, p);
        System.out.println(where + ": " + memUTF8(p) + " " + memUTF8(p, 6) + " " + strlen(p, 1) + " "
                + hex(p, strlen(p, 1) + 1));
        be.putStringUTF16(text, true, p);
        System.out.println(where + ": " + memUTF16(p) + " " + memUTF16(p, 2) + " " + strlen(p, 2));
        System.out.println(where + ": " + be.putStringUTF8(text, false, p, 100) + " " + memUTF8(p, 3));
        ByteBuffer owned = memUTF8(text);
        ByteBuffer bare = memUTF8(text, false);
        System.out.println(where + ": " + memUTF8(owned, owned.remaining() - 1) + " " + owned.remaining() + " "
                + bare.remaining() + " " + owned.get(owned.remaining() - 1));
        memFree(owned);
        memFree(bare);
        try {
            be.putStringUTF8(text, true, p, 5);
            System.out.println(where + ": a long string written short");
        } catch (java.nio.BufferOverflowException e) {
            System.out.println(where + ": a long string refused");
        }
    }

    public static void main(String[] args) {
        MemoryBackend be = memBackend();
        System.out.println("page " + PAGE_SIZE + ", cache line " + CACHE_LINE_SIZE + ", pointer " + org.lwjgl.system.Pointer.POINTER_SIZE
                + ", C long " + org.lwjgl.system.Pointer.CLONG_SIZE);

        // the program's memory, and C's
        ByteBuffer mine = memAlloc(128);
        long c = LibCStdlib.nmalloc(128);
        types("mine", memAddress(mine));
        types("C's", c);
        memSet(mine, 0x5A);
        memCopy(memAddress(mine), c, 64);
        System.out.println("copied to C's: " + hex(c, 8) + " " + hex(c + 56, 9));
        memSet(c, 0xC3, 16);
        memCopy(c + 8, memAddress(mine) + 1, 8);
        System.out.println("copied from C's: " + hex(memAddress(mine), 12));
        arrays("mine", be, memAddress(mine));
        arrays("C's", be, c);
        strings("mine", be, memAddress(mine));
        strings("C's", be, c);
        byte[] le = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        System.out.println("a long from bytes: " + Long.toHexString(be.getLong(le, 1)));
        System.out.println("a null string: " + memASCIISafe(NULL) + " " + memUTF8Safe(NULL));
        LibCStdlib.nfree(c);
        memFree(mine);

        // allocations: zeroed when asked, kept across a realloc, aligned
        ByteBuffer dirty = memAlloc(100);
        memSet(dirty, 0x77);
        memFree(dirty);
        ByteBuffer zero = memCalloc(100);
        boolean allZero = true;
        for (int i = 0; i < 100; i++) {
            allZero &= zero.get(i) == 0;
        }
        System.out.println("calloc: " + allZero + " " + zero.capacity() + " " + (zero.order() == ByteOrder.nativeOrder()));
        memFree(zero);
        IntBuffer grow = memAllocInt(4);
        grow.put(0, 1).put(1, 2).put(2, 3).put(3, 4);
        grow = memRealloc(grow, 1000);
        System.out.print("realloc: " + grow.capacity() + " " + grow.get(0) + grow.get(1) + grow.get(2) + grow.get(3));
        grow = memRealloc(grow, 2);
        System.out.println(" " + grow.capacity() + " " + grow.get(0) + grow.get(1));
        memFree(grow);
        long fresh = nmemRealloc(NULL, 16);
        System.out.println("realloc of NULL: " + (fresh != NULL));
        nmemFree(fresh);
        nmemFree(NULL);
        memFree((ByteBuffer) null);
        StringBuilder aligned = new StringBuilder("aligned:");
        ByteBuffer[] blocks = new ByteBuffer[4];
        int[] alignments = {16, 64, 256, 4096};
        for (int k = 0; k < 4; k++) {
            blocks[k] = memAlignedAlloc(alignments[k], 100 + k);
            memSet(blocks[k], k);
            aligned.append(" ").append(memAddress(blocks[k]) % alignments[k] == 0).append(blocks[k].capacity());
        }
        for (int k = 0; k < 4; k++) {
            aligned.append(" ").append(blocks[k].get(99));
            memAlignedFree(blocks[k]);
        }
        System.out.println(aligned);
        // many small aligned blocks among plain ones, freed in turn: the heap
        // they come from stays whole
        boolean whole = true;
        ByteBuffer[] small = new ByteBuffer[64];
        ByteBuffer[] plain = new ByteBuffer[64];
        for (int round = 0; round < 4; round++) {
            for (int k = 0; k < 64; k++) {
                small[k] = memAlignedAlloc(8 << (k % 3), 8 + k);
                plain[k] = memAlloc(8 + k);
                memSet(small[k], k);
                memSet(plain[k], 255 - k);
            }
            for (int k = 0; k < 64; k++) {
                whole &= small[k].get(7 + k) == (byte) k && plain[k].get(7 + k) == (byte) (255 - k)
                        && memAddress(small[k]) % (8 << (k % 3)) == 0;
                memAlignedFree(small[k]);
                memFree(plain[k]);
            }
        }
        System.out.println("many aligned blocks: " + whole);

        // buffers over memory, in native order, their addresses each
        // element's, a slice or duplicate keeping its order
        ByteBuffer bb = memCalloc(64);
        memPutInt(memAddress(bb) + 8, 0x01020304);
        IntBuffer ints = memIntBuffer(memAddress(bb), 16);
        LongBuffer longs = memLongBuffer(memAddress(bb), 8);
        System.out.println("buffers: " + (bb.order() == ByteOrder.nativeOrder()) + " " + memByteBuffer(memAddress(bb), 16).getInt(8)
                + " " + ints.get(2) + " " + Long.toHexString(longs.get(1)) + " " + (memAddress(bb, 3) - memAddress(bb))
                + " " + (memAddress(ints, 5) - memAddress(bb)) + " " + (memAddress(longs, 3) - memAddress(bb)));
        bb.position(4);
        System.out.println("a duplicate's order: " + memDuplicate(bb).order());
        ByteBuffer slice = memSlice(bb, 2, 8);
        ByteBuffer dup = memDuplicate(bb.order(ByteOrder.BIG_ENDIAN));
        System.out.println("big-endian slices: " + memSlice(bb).order() + " " + memSlice(bb, 1, 2).order());
        System.out.println("slices: " + (memAddress(slice) - memAddress0(bb)) + " " + slice.capacity() + " "
                + slice.order() + " " + memSlice(bb).capacity() + " " + dup.order() + " " + dup.position()
                + " " + (memAddress(memSlice(ints, 1, 3)) - memAddress(ints)));
        try {
            memSlice(bb, 2, 100);
            System.out.println("a slice past the end made");
        } catch (IllegalArgumentException e) {
            System.out.println("a slice past the end refused");
        }
        bb.order(ByteOrder.nativeOrder()).position(0);
        int[] src = {5, 6, 7};
        int[] back = new int[3];
        memCopy(src, ints);
        memCopy(ints, back);
        System.out.println("array copies: " + Arrays.toString(back) + " " + memGetInt(memAddress(bb) + 4));
        // each element type's size in its addresses
        ShortBuffer shorts = memAllocShort(4);
        CharBuffer chars = memAllocChar(4);
        FloatBuffer floats = memAllocFloat(4);
        DoubleBuffer doubles = memAllocDouble(4);
        LongBuffer longs2 = memAllocLong(4);
        java.nio.Buffer[] typed = {shorts, chars, ints, floats, doubles, longs2};
        StringBuilder sizes = new StringBuilder("element sizes:");
        for (java.nio.Buffer t : typed) {
            long at0 = memAddress(t);
            t.position(1);
            sizes.append(" ").append(memAddress(t) - at0);
            t.position(0);
        }
        sizes.append(" ").append(memAddress(memSlice(shorts, 1, 2)) - memAddress(shorts))
                .append(memAddress(memSlice(chars, 1, 2)) - memAddress(chars))
                .append(memAddress(memSlice(floats, 1, 2)) - memAddress(floats))
                .append(memAddress(memSlice(doubles, 1, 2)) - memAddress(doubles))
                .append(memAddress(memSlice(longs2, 1, 2)) - memAddress(longs2))
                .append(" ").append(memSlice(shorts).capacity()).append(memSlice(doubles, 2, 2).capacity());
        System.out.println(sizes);
        memFree(shorts);
        memFree(chars);
        memFree(floats);
        memFree(doubles);
        memFree(longs2);
        PointerBuffer pointers = memAllocPointer(3);
        pointers.put(0, 42).put(2, -1);
        System.out.println("pointers: " + pointers.get(0) + " " + pointers.get(2) + " "
                + memGetAddress(pointers.address(2)));
        memFree(pointers);
        memFree(bb);

        // the stack: frames give back what they took
        try (MemoryStack stack = MemoryStack.stackPush()) {
            int sp = stack.getPointer();
            try (MemoryStack inner = stack.push()) {
                IntBuffer three = inner.mallocInt(3);
                LongBuffer zeros = inner.callocLong(4);
                ByteBuffer utf8 = inner.UTF8("été", false);
                ByteBuffer ascii = inner.ASCII("abc");
                System.out.println("stack: " + three.capacity() + " " + zeros.get(3) + " " + utf8.remaining() + " "
                        + ascii.remaining() + " " + memASCII(ascii, 3) + " " + inner.ints(1, 2, 3).get(2) + " "
                        + (sp - inner.getPointer() >= 12 + 32 + 5 + 4 + 12) + " "
                        + (memAddress(three) % 4 == 0) + " " + (memAddress(zeros) % 8 == 0));
            }
            System.out.println("popped: " + (stack.getPointer() == sp));
        }
    }
}
