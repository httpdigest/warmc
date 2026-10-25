package org.lwjgl.system;

/** Something native that has to be freed: LWJGL's, for warmc. */
public interface NativeResource extends AutoCloseable {
    void free();

    @Override
    default void close() {
        free();
    }
}
