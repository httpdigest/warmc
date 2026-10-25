package org.lwjgl.system.warmc;

import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.MemoryUtil;
import org.teavm.interop.Address;

/**
 * Java arrays handed to C, for the invokes of org.lwjgl.system.JNI that take
 * them, which lwjgl/build.py generates: an array is a GC object, which C
 * cannot address, so it is copied into linear memory on the thread's
 * MemoryStack before the call ({@code in}) and back after it ({@code out}),
 * C having perhaps written it.  A null array is NULL.  The element types are
 * those JNI's arrays have.
 */
public final class WarmcArrays {
    private WarmcArrays() {
    }

    private static Address at(long p) {
        return WarmcMemory.at(p);
    }

    public static long in(MemoryStack stack, short[] a) {
        if (a == null) {
            return MemoryUtil.NULL;
        }
        long p = stack.nmalloc(8, a.length << 1);
        for (int i = 0; i < a.length; i++) {
            at(p + ((long) i << 1)).putShort(a[i]);
        }
        return p;
    }

    public static void out(short[] a, long p) {
        for (int i = 0; a != null && i < a.length; i++) {
            a[i] = at(p + ((long) i << 1)).getShort();
        }
    }

    public static long in(MemoryStack stack, int[] a) {
        if (a == null) {
            return MemoryUtil.NULL;
        }
        long p = stack.nmalloc(8, a.length << 2);
        for (int i = 0; i < a.length; i++) {
            at(p + ((long) i << 2)).putInt(a[i]);
        }
        return p;
    }

    public static void out(int[] a, long p) {
        for (int i = 0; a != null && i < a.length; i++) {
            a[i] = at(p + ((long) i << 2)).getInt();
        }
    }

    public static long in(MemoryStack stack, long[] a) {
        if (a == null) {
            return MemoryUtil.NULL;
        }
        long p = stack.nmalloc(8, a.length << 3);
        for (int i = 0; i < a.length; i++) {
            at(p + ((long) i << 3)).putLong(a[i]);
        }
        return p;
    }

    public static void out(long[] a, long p) {
        for (int i = 0; a != null && i < a.length; i++) {
            a[i] = at(p + ((long) i << 3)).getLong();
        }
    }

    public static long in(MemoryStack stack, float[] a) {
        if (a == null) {
            return MemoryUtil.NULL;
        }
        long p = stack.nmalloc(8, a.length << 2);
        for (int i = 0; i < a.length; i++) {
            at(p + ((long) i << 2)).putFloat(a[i]);
        }
        return p;
    }

    public static void out(float[] a, long p) {
        for (int i = 0; a != null && i < a.length; i++) {
            a[i] = at(p + ((long) i << 2)).getFloat();
        }
    }

    public static long in(MemoryStack stack, double[] a) {
        if (a == null) {
            return MemoryUtil.NULL;
        }
        long p = stack.nmalloc(8, a.length << 3);
        for (int i = 0; i < a.length; i++) {
            at(p + ((long) i << 3)).putDouble(a[i]);
        }
        return p;
    }

    public static void out(double[] a, long p) {
        for (int i = 0; a != null && i < a.length; i++) {
            a[i] = at(p + ((long) i << 3)).getDouble();
        }
    }
}
