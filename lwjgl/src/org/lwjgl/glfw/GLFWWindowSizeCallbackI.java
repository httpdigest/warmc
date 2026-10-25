package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's WindowSize callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWWindowSizeCallbackI extends CallbackI {
    void invoke(long window, int width, int height);
}
