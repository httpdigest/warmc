package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's Char callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWCharCallbackI extends CallbackI {
    void invoke(long window, int codepoint);
}
