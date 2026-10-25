package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's Key callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWKeyCallbackI extends CallbackI {
    void invoke(long window, int key, int scancode, int action, int mods);
}
