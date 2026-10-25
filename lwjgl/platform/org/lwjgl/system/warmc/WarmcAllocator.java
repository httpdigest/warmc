package org.lwjgl.system.warmc;

import org.lwjgl.system.MemoryUtil;
import org.teavm.interop.Address;
import org.teavm.runtime.heap.Heap;

/**
 * LWJGL's memory allocator, for warmc: what memAlloc, memCalloc,
 * MemoryStack and the rest hand out is in linear memory, from TeaVM's heap
 * there, so that the program reads and writes it in place and makes buffers
 * over it, and C takes its native address as it is.
 *
 * <p>Its functions have no C address: a C library that LWJGL hands an
 * allocator to (GLFW's glfwInitAllocator, among others) is not handed this
 * one.
 */
public final class WarmcAllocator implements MemoryUtil.MemoryAllocator {
    @Override public long getMalloc() { return MemoryUtil.NULL; }
    @Override public long getCalloc() { return MemoryUtil.NULL; }
    @Override public long getRealloc() { return MemoryUtil.NULL; }
    @Override public long getFree() { return MemoryUtil.NULL; }
    @Override public long getAlignedAlloc() { return MemoryUtil.NULL; }
    @Override public long getAlignedFree() { return MemoryUtil.NULL; }

    private static long nativeAddress(Address a) {
        return a.toInt() == 0 ? MemoryUtil.NULL : WarmcMemory.address(a.toInt());
    }

    @Override
    public long malloc(long size) {
        if (size < 0 || size > Integer.MAX_VALUE - 16) {
            return MemoryUtil.NULL;
        }
        return nativeAddress(Heap.alloc((int) Math.max(size, 1)));
    }

    @Override
    public long calloc(long num, long size) {
        long bytes = num * size;
        long address = malloc(bytes);
        if (address != MemoryUtil.NULL) {
            WarmcMemory.nmemset(address, 0, bytes);
        }
        return address;
    }

    @Override
    public long realloc(long ptr, long size) {
        if (ptr == MemoryUtil.NULL) {
            return malloc(size);
        }
        if (size < 0 || size > Integer.MAX_VALUE - 16) {
            return MemoryUtil.NULL;
        }
        return nativeAddress(Heap.realloc(WarmcMemory.at(ptr), (int) Math.max(size, 1)));
    }

    @Override
    public void free(long ptr) {
        if (ptr != MemoryUtil.NULL) {
            Heap.release(WarmcMemory.at(ptr));
        }
    }

    // the block's own address goes in the 8 bytes before the one handed out
    @Override
    public long aligned_alloc(long alignment, long size) {
        long raw = malloc(size + alignment + 8);
        if (raw == MemoryUtil.NULL) {
            return MemoryUtil.NULL;
        }
        long address = (raw + 8 + alignment - 1) & -alignment;
        WarmcMemory.at(address - 8).putLong(raw);
        return address;
    }

    @Override
    public void aligned_free(long ptr) {
        if (ptr != MemoryUtil.NULL) {
            free(WarmcMemory.at(ptr - 8).getLong());
        }
    }
}
