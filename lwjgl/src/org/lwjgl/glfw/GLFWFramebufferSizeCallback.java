package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's FramebufferSize callback as an object: LWJGL's, for warmc. */
public abstract class GLFWFramebufferSizeCallback extends Callback implements GLFWFramebufferSizeCallbackI {
    protected GLFWFramebufferSizeCallback() {
    }

    public static GLFWFramebufferSizeCallback create(GLFWFramebufferSizeCallbackI instance) {
        return instance instanceof GLFWFramebufferSizeCallback
                ? (GLFWFramebufferSizeCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWFramebufferSizeCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWFramebufferSizeCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWFramebufferSizeCallback at that address");
        }
        return create((GLFWFramebufferSizeCallbackI) callback);
    }

    public static GLFWFramebufferSizeCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWFramebufferSizeCallback wrap(GLFWFramebufferSizeCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWFramebufferSizeCallback set(long window) {
        GLFW.glfwSetFramebufferSizeCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWFramebufferSizeCallback {
        private final GLFWFramebufferSizeCallbackI delegate;

        Container(GLFWFramebufferSizeCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, int width, int height) {
            delegate.invoke(window, width, height);
        }
    }
}
