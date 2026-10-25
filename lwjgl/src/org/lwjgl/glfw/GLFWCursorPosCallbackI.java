package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's CursorPos callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWCursorPosCallbackI extends CallbackI {
    void invoke(long window, double xpos, double ypos);
}
