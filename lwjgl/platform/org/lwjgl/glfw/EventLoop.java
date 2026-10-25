package org.lwjgl.glfw;

/**
 * GLFW's check that it runs on the process's first thread, for warmc, where
 * it does: warmc's WASI host runs the program there, as macOS's GLFW needs
 * and as LWJGL's check would ask Objective-C on the JVM.
 */
final class EventLoop {
    private EventLoop() {
    }

    static void check() {
    }
}
