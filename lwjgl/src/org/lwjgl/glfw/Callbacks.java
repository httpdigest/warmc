package org.lwjgl.glfw;

/** Freeing a window's callbacks: LWJGL's Callbacks, for warmc. */
public final class Callbacks {
    private Callbacks() {
    }

    /** Sets each of the window's callbacks to none and frees the one it had. */
    public static void glfwFreeCallbacks(long window) {
        for (int kind = 1; kind < 8; kind++) {
            Object callback = WarmcUpcalls.get(window, kind);
            if (callback != null) {
                WarmcUpcalls.set(window, kind, null);
                if (callback instanceof org.lwjgl.system.Callback) {
                    ((org.lwjgl.system.Callback) callback).free();
                }
            }
        }
    }
}
