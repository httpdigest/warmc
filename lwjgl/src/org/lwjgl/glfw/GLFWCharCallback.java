package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's Char callback as an object: LWJGL's, for warmc. */
public abstract class GLFWCharCallback extends Callback implements GLFWCharCallbackI {
    protected GLFWCharCallback() {
    }

    public static GLFWCharCallback create(GLFWCharCallbackI instance) {
        return instance instanceof GLFWCharCallback
                ? (GLFWCharCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWCharCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWCharCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWCharCallback at that address");
        }
        return create((GLFWCharCallbackI) callback);
    }

    public static GLFWCharCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWCharCallback wrap(GLFWCharCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWCharCallback set(long window) {
        GLFW.glfwSetCharCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWCharCallback {
        private final GLFWCharCallbackI delegate;

        Container(GLFWCharCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, int codepoint) {
            delegate.invoke(window, codepoint);
        }
    }
}
