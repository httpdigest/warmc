package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's Error callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWErrorCallbackI extends CallbackI {
    void invoke(int error, long description);
}
