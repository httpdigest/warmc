package org.lwjgl.glfw;

import org.lwjgl.system.CallbackI;

/** GLFW's MouseButton callback: LWJGL's, for warmc. */
@FunctionalInterface
public interface GLFWMouseButtonCallbackI extends CallbackI {
    void invoke(long window, int button, int action, int mods);
}
