package org.lwjgl.system;

/** A pointer to native memory: LWJGL's, for warmc. */
public interface Pointer {
    int POINTER_SIZE = 8;
    int POINTER_SHIFT = 3;
    boolean BITS32 = false;
    boolean BITS64 = true;

    long address();

    abstract class Default implements Pointer {
        protected long address;

        protected Default(long address) {
            this.address = address;
        }

        @Override
        public long address() {
            return address;
        }

        @Override
        public boolean equals(Object o) {
            return o instanceof Pointer && ((Pointer) o).address() == address;
        }

        @Override
        public int hashCode() {
            return (int) (address ^ (address >>> 32));
        }

        @Override
        public String toString() {
            return String.format("%s pointer [0x%X]", getClass().getSimpleName(), address);
        }
    }
}
