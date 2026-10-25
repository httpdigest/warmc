package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's WindowSize callback as an object: LWJGL's, for warmc. */
public abstract class GLFWWindowSizeCallback extends Callback implements GLFWWindowSizeCallbackI {
    protected GLFWWindowSizeCallback() {
    }

    public static GLFWWindowSizeCallback create(GLFWWindowSizeCallbackI instance) {
        return instance instanceof GLFWWindowSizeCallback
                ? (GLFWWindowSizeCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWWindowSizeCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWWindowSizeCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWWindowSizeCallback at that address");
        }
        return create((GLFWWindowSizeCallbackI) callback);
    }

    public static GLFWWindowSizeCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWWindowSizeCallback wrap(GLFWWindowSizeCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWWindowSizeCallback set(long window) {
        GLFW.glfwSetWindowSizeCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWWindowSizeCallback {
        private final GLFWWindowSizeCallbackI delegate;

        Container(GLFWWindowSizeCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, int width, int height) {
            delegate.invoke(window, width, height);
        }
    }
}
