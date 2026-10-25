/*
 * What javac compiles org.lwjgl against, in java.base (--patch-module): the
 * signatures of lwjgl/src/org/teavm/classlib/java/nio/TLwjglBuffers.java, which
 * TeaVM substitutes for this class.  Never run.
 */
package java.nio;

public final class LwjglBuffers {
    private LwjglBuffers() {
    }

    public static ByteBuffer bytes(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static ShortBuffer shorts(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static CharBuffer chars(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static IntBuffer ints(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static LongBuffer longs(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static FloatBuffer floats(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static DoubleBuffer doubles(int address, int capacity) {
        throw new UnsupportedOperationException();
    }

    public static int address(Buffer buffer) {
        throw new UnsupportedOperationException();
    }
}
