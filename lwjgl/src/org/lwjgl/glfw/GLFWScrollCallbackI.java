package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's Scroll callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWScrollCallbackI extends CallbackI {
    void invoke(long window, double xoffset, double yoffset);
}
