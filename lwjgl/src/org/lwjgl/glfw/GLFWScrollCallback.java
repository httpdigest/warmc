package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's Scroll callback as an object: LWJGL's, for warmc. */
public abstract class GLFWScrollCallback extends Callback implements GLFWScrollCallbackI {
    protected GLFWScrollCallback() {
    }

    public static GLFWScrollCallback create(GLFWScrollCallbackI instance) {
        return instance instanceof GLFWScrollCallback
                ? (GLFWScrollCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWScrollCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWScrollCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWScrollCallback at that address");
        }
        return create((GLFWScrollCallbackI) callback);
    }

    public static GLFWScrollCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWScrollCallback wrap(GLFWScrollCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWScrollCallback set(long window) {
        GLFW.glfwSetScrollCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWScrollCallback {
        private final GLFWScrollCallbackI delegate;

        Container(GLFWScrollCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, double xoffset, double yoffset) {
            delegate.invoke(window, xoffset, yoffset);
        }
    }
}
