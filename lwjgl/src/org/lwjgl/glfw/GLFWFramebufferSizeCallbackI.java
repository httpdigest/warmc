package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's FramebufferSize callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWFramebufferSizeCallbackI extends CallbackI {
    void invoke(long window, int width, int height);
}
