package org.lwjgl.system;

/** A struct in native memory: LWJGL's, for warmc. */
public abstract class Struct<SELF extends Struct<SELF>> extends Pointer.Default {
    protected Struct(long address, java.nio.ByteBuffer container) {
        super(address);
    }

    public abstract int sizeof();

    public void clear() {
        MemoryUtil.memSet(address(), 0, sizeof());
    }

    public boolean isNull(int memberOffset) {
        return MemoryUtil.memGetAddress(address() + memberOffset) == MemoryUtil.NULL;
    }
}
